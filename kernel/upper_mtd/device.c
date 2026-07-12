/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mtd/nand.h>
#include <linux/vmalloc.h>

#include "backing_mtd/device.h"
#include "proxy_device/class.h"
#include "proxy_device/device.h"
#include "proxy_device/io.h"
#include "upper_mtd/device.h"

static int upper_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	WARN_ON(mtd->priv == NULL);
	struct upper_mtd_device *dev = mtd->priv;
	return mtd_erase(dev->backend, instr);
}

static int ensure_safe_environment(struct mtd_info *mtd, struct mtd_oob_ops *ops,
	struct ufedm_proxy_device **proxy_dev_ptr,
	struct upper_mtd_device **dev_ptr)
{
	WARN_ON_ONCE(mtd->priv == NULL);
	if (mtd->priv == NULL)
		return -EIO;

	/* RAW mode is dangerous and eliminates
	 * any safe guard of this module. Don't allow it, the
	 * user can just use the backing MTD device in RAW mode.
	 * This should prevent a disaster waiting from happening due
	 * malfunctioning filesystem or userspace program.
	 *
	 * And yes, I know this can be a read function we validate, but
	 * goddammit if someone wants to use this in RAW mode, this is still
	 * invalid.
	 *
	 * PLACE_OOB mode is dangerous as well. It is less dangerous than
	 * RAW mode, because the user still relies on our driver to put
	 * ECC & other important metadata on the NAND flash chip, but it
	 * can do quite a bit of damage if used improperly.
	 *
	 * Besides that, ChatGPT says that PLACE_OOB mode is used by
	 * bootloaders and raw flash writers to sometimes write a specific
	 * bad block markers or put boot metadata at specific OOB offsets.
	 * However, the entire point of this module is to give userspace
	 * the possibility of intervention with ECC & layout to essentially
	 * implement a raw flash writer/reader, based on a known & managed
	 * policy and not allowing random userspace programs to do whatever
	 * they want in that regard.
	 *
	 * If the user needs to do this anyway, they can probably just open
	 * the _original_ backing MTD device and invoke their program on it.
	 * 
	 * It should be noted that some userspace program that does read(2) or
	 * write(2) might invoke a request with MTD_OPS_PLACE_OOB but with oob
	 * buffer equals to NULL - in such case we do allow such request to
	 * be processed.
	 */
	if (ops->ooblen != 0 && ops->oobbuf == NULL) {
		pr_warn_ratelimited("%s: ops->ooblen != 0 but ops->oobbuf == NULL, "
				    "Abort.\n",
		    mtd->name);
		return -EINVAL;
	}

	if (ops->ooblen != 0 && ops->mode != MTD_OPS_AUTO_OOB) {
		pr_warn_ratelimited("%s: Only MTD_OPS_AUTO_OOB mode access is "
				    "allowed by this device.\n",
		    mtd->name);
		return -EOPNOTSUPP;
	}

	*dev_ptr = mtd->priv;

	*proxy_dev_ptr = (*dev_ptr)->proxy_dev;
	if (!*proxy_dev_ptr)
		return -EAGAIN;

	return 0;
}

static int upper_read_oob(
    struct mtd_info *mtd, loff_t to, struct mtd_oob_ops *ops)
{
	int ret;
	struct upper_mtd_device *dev;
	struct proxy_io_slot *slot;
	struct shm_packet *pkt;
	struct ufedm_proxy_device *proxy_dev;
	struct nand_io_iter iter;
	struct nand_device *nand;
	struct mtd_info *backend;

	ret = ensure_safe_environment(mtd, ops, &proxy_dev, &dev);
	if (ret < 0)
		return ret;

	BUG_ON(proxy_dev->page_data_size == 0);
	BUG_ON(proxy_dev->page_oob_size == 0);

	nand = mtd_to_nanddev(proxy_dev->backend_dev);
	backend = dev->backend;

	ret = proxy_device_get_slot(proxy_dev, NAND_PAGE_READ, &slot);
	if (ret < 0)
		return ret;

	pkt = get_shm_packet(proxy_dev->shm_mapping.kaddr, &proxy_dev->info,
	    SHM_READ_QUEUE_IDX, slot->slot_idx);

	/* Reading is **SIGNIFICANTLY HARDER** than writing. We should read
	 * a whole raw page - the data and OOB together, and immediately memcpy
	 * it to the shared memory interface, and wait for userspace to apply
	 * the changes based on that.
	 *
	 * And only then we can copy it back to the original request buffer.
	 *
	 * It happens to be harder and more I/O intensive, because when the
	 * user wants to write, what is given is what we must respect to write,
	 * but when reading, userspace might need to check info in the OOB
	 * section (especially for metadata or ECC) before it can apply
	 * correction on the **WHOLE** data buffer and only then we can send the
	 * requested chunk back.
	 *
	 * In some way it's expected to be this way, as NAND chips don't really
	 * allow partially reading pages in terms of byte granularity.
	 * So for, sadly, it seems like there's no real way around this
	 * behavior...
	 */

	struct simple_nand_page_io_req simple_req = {
	    .datalen = proxy_dev->page_data_size,
	    .ooblen = proxy_dev->page_oob_size,
	    .databuf = slot->shadow_data_and_oob_buf,
	    .oobbuf = slot->shadow_data_and_oob_buf,
	};

	BUG_ON(slot->shadow_data_and_oob_buf_size !=
	       (proxy_dev->page_data_size + proxy_dev->page_oob_size));

	nanddev_io_for_each_page(nand, NAND_PAGE_READ, to, ops, &iter)
	{
		BUG_ON(iter.req.datalen > proxy_dev->page_data_size);
		BUG_ON(iter.req.ooblen > proxy_dev->page_oob_size);

		// Start with a "cleaned-up" NAND shadow page buffer.
		memset(slot->shadow_data_and_oob_buf, 0xFF,
		    proxy_dev->page_data_size + proxy_dev->page_data_size);

		struct mtd_oob_ops raw_ops;
		raw_ops.mode = MTD_OPS_RAW;
		raw_ops.len = proxy_dev->page_data_size;
		raw_ops.ooblen = proxy_dev->page_oob_size;
		raw_ops.datbuf = (u8 *)slot->shadow_data_and_oob_buf;
		raw_ops.oobbuf = (u8 *)slot->shadow_data_and_oob_buf +
				 proxy_dev->page_data_size;

		/* We can also fail right here as well.
		 * Common reasons are I/O issues in hardware, etc.
		 */
		ret = backend->_read_oob(backend, to, &raw_ops);
		if (ret < 0)
			goto exit;

		proxy_device_io_slot_pub_new_packet(slot, &simple_req);

		wait_for_completion(&slot->done);

		/* We might have a failure - exit now if that's the case
		 * This might be NACK from userspace, or a timeout!
		 * If that's the case we fail right here and don't proceed.
		 * There might be other reasons than timeout, which are equally
		 * treated.
		 */
		if (slot->status != 0) {
			ret = slot->status;
			goto exit;
		}

		memcpy(iter.req.databuf.in, pkt->buf, iter.req.datalen);
		memcpy(iter.req.oobbuf.in,
		    (const u8 *)pkt->buf + proxy_dev->page_data_size,
		    iter.req.ooblen);

		ops->retlen += iter.req.datalen;
		ops->oobretlen += iter.req.ooblen;
	}

exit:
	proxy_device_put_slot(slot);
	return ret;
}

static int upper_write_oob(struct mtd_info *mtd, loff_t to,
                               struct mtd_oob_ops *ops)
{
	int ret;
	struct upper_mtd_device *dev;
	struct proxy_io_slot *slot;
	struct shm_packet *pkt;
	struct ufedm_proxy_device *proxy_dev;
	struct nand_io_iter iter;
	struct nand_device *nand;
	struct mtd_info *backend;
	size_t user_ret_datalen;
	size_t user_ret_ooblen;
	struct mtd_oob_ops per_page_raw_ops;

	ret = ensure_safe_environment(mtd, ops, &proxy_dev, &dev);
	if (ret < 0)
		return ret;

	BUG_ON(proxy_dev->page_data_size == 0);
	BUG_ON(proxy_dev->page_oob_size == 0);

	nand = mtd_to_nanddev(proxy_dev->backend_dev);
	backend = dev->backend;

	ret = proxy_device_get_slot(proxy_dev, NAND_PAGE_WRITE, &slot);
	if (ret < 0)
		return ret;

	pkt = get_shm_packet(proxy_dev->shm_mapping.kaddr, &proxy_dev->info,
	    SHM_WRITE_QUEUE_IDX, slot->slot_idx);

	struct simple_nand_page_io_req simple_req;

	nanddev_io_for_each_page(nand, NAND_PAGE_WRITE, to, ops, &iter)
	{
		simple_req.datalen = iter.req.datalen;
		simple_req.ooblen = iter.req.ooblen;
		simple_req.databuf = iter.req.databuf.in;
		simple_req.oobbuf = iter.req.oobbuf.in;
		proxy_device_io_slot_pub_new_packet(slot, &simple_req);

		wait_for_completion(&slot->done);

		/* We might have a failure - exit now if that's the case
		 * This might be NACK from userspace, or a timeout!
		 * If that's the case we fail right here and don't proceed.
		 * There might be other reasons than timeout, which are equally
		 * treated.
		 */
		if (slot->status != 0) {
			ret = slot->status;
			goto exit;
		}

		user_ret_datalen = pkt->header.datalen;
		user_ret_ooblen = pkt->header.ooblen;

		if (user_ret_datalen > proxy_dev->page_data_size) {
			pr_warn_ratelimited(
			    "ufedm: packet (slot %zu) returned data len which "
			    "is higher than allowed "
			    "(tried %zu, max %zu)\n",
			    slot->slot_idx, user_ret_datalen,
			    proxy_dev->page_data_size);
			return -EINVAL;
		}

		if (user_ret_ooblen > proxy_dev->page_oob_size) {
			pr_warn_ratelimited(
			    "ufedm: packet (slot %zu) returned ooblen which is "
			    "higher than allowed "
			    "(tried %zu, max %zu)\n",
			    slot->slot_idx, user_ret_ooblen,
			    proxy_dev->page_oob_size);
			return -EINVAL;
		}

		per_page_raw_ops.mode = MTD_OPS_RAW;
		per_page_raw_ops.len = user_ret_datalen;
		per_page_raw_ops.ooblen = user_ret_ooblen;
		per_page_raw_ops.datbuf = pkt->buf;
		per_page_raw_ops.oobbuf = pkt->buf + proxy_dev->page_data_size;

		/* We can also fail right here as well.
		 * Common reasons are I/O issues in hardware, etc.
		 */
		ret = backend->_write_oob(backend, to, &per_page_raw_ops);
		if (ret < 0)
			goto exit;

		/* It's tempting to do this:
		 * ```
		 * ops->retlen += user_ret_datalen;
		 * ops->oobretlen += user_ret_ooblen;
		 * ```
		 * However, that could severely confuse the MTD client as it
		 * thought it sent a known set of bytes (for example, only
		 * data buffer, and we applied our own OOB during the
		 * transaction) and got a result of "more" bytes than it
		 * expected. So just tell the MTD client that everything is
		 * "fine" and hide the _ugly_ truth.
		 */
		ops->retlen += iter.req.datalen;
		ops->oobretlen += iter.req.ooblen;
	}

exit:
	proxy_device_put_slot(slot);
	return ret;
}

static int create_device(struct upper_mtd_device *dev, struct mtd_info *backend, struct ufedm_proxy_device *proxy_dev)
{
	int ret;

	dev->upper = kvzalloc(sizeof(struct mtd_info), GFP_KERNEL);
	if (!dev->upper) {
		return -ENOMEM;
	}

	dev->backend = backend;
	if (dev->backend == NULL) {
		return -EINVAL;
	}

	/* Basic identity */
	dev->upper->name = "upper-mtd";
	dev->upper->type = backend->type;
	dev->upper->flags = backend->flags;
	dev->upper->size = backend->size;
	dev->upper->erasesize = backend->erasesize;
	dev->upper->writesize = backend->writesize;

	dev->upper->_erase = upper_erase;

	dev->upper->_write_oob = upper_write_oob;
    dev->upper->_read_oob  = upper_read_oob;

	dev->upper->priv = dev;

	// Connect a proxy_dev into our upper device
	// so it can deref it later on when doing I/O.
	dev->proxy_dev = proxy_dev;

	ret = mtd_device_register(dev->upper, NULL, 0);
	if (ret != 0) {
		pr_err("ufedm: failed to register upper MTD\n");
		return ret;
	}

	return 0;
}

static void destroy_device(struct upper_mtd_device *dev)
{
	mtd_device_unregister(dev->upper);
	dev->proxy_dev = NULL;
	kvfree(dev->upper);
}

void upper_mtd_destroy_devices(struct upper_mtd_device *dev_array, size_t count)
{
	for (size_t idx = 0; idx < count; idx++)
		destroy_device(&dev_array[idx]);
}

int upper_mtd_initialize_devices(
    struct upper_mtd_device *dev_array, size_t count)
{
	size_t i;
	int ret;
	struct ufedm_proxy_device *proxy_dev;

	for (i = 0; i < count; i++) {
		proxy_dev = proxy_device_resolve_by_minor(i);
		WARN_ON(proxy_dev == NULL);
		/* There's nothing sane we can do besides just
		 * exiting with a failure.
		 */
		if (!proxy_dev)
			goto error_create_device;

		// We send a `get_backend_mtd_device(i)` and rely on the fact
		// that function will check if it's a NULL pointer.
		ret = create_device(
		    &dev_array[i], get_backend_mtd_device(i), proxy_dev);
		if (ret != 0)
			goto error_create_device;
	}

	return 0;

error_create_device:
	upper_mtd_destroy_devices(dev_array, i);
	return ret;
}
