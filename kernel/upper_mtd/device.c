/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#include <asm-generic/errno.h>
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
#include "proxy_device/shm.h"
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
	struct shared_mem_slot *shm_slot;
	struct ufedm_proxy_device *proxy_dev;
	struct nand_io_iter iter;
	struct nand_device *nand;
	struct mtd_info *backend;
	struct mtd_oob_ops raw_ops;

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

	shm_slot = proxy_device_queue_and_slot_to_buf(
	    proxy_dev, slot->parentq->info.idx, slot->slot_idx);

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
	    .oobbuf = ((u8 *)slot->shadow_data_and_oob_buf) +
		      proxy_dev->page_data_size,
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

		raw_ops.mode = MTD_OPS_RAW;
		raw_ops.len = proxy_dev->page_data_size;
		raw_ops.ooblen = proxy_dev->page_oob_size;
		raw_ops.ooboffs = 0;
		raw_ops.datbuf = simple_req.databuf;
		raw_ops.oobbuf = simple_req.oobbuf;

		/* We can also fail right here as well.
		 * Common reasons are I/O issues in hardware, etc.
		 */
		ret = backend->_read_oob(backend, to, &raw_ops);
		if (ret < 0)
			goto exit;

		// FIXME: Can we integrate copy_nand_pos_to_to_io_pos_params
		// with proxy_device_io_slot_pub_new_packet tightly together?
		copy_nand_pos_to_to_io_pos_params(
		    &iter.req.pos, &simple_req.pos_params);
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

		/* It's completely legal for userspace to provide back **less**
		 * OOB bytes than actually requested. In such case, we might
		 * just adjust the ooblen of the current request and be done
		 * with it. This is equivalent to changing the mtd_oobavail
		 * parameter in runtime, just so it's done by userspace during
		 * an I/O request in correlation to a known policy.
		 */
		if (slot->header.ooblen < iter.req.ooblen)
			iter.req.ooblen = slot->header.ooblen;

		/* There's no sane way to handle more OOB bytes and insert them
		 * into a buffer range that doesn't exist. Reject it now.
		 */
		if (slot->header.ooblen > iter.req.ooblen) {
			pr_warn_ratelimited(
			    "ufedm: OOB bytes count exceeding for read request "
			    "(ACKed %zu OOB bytes, should be up to %zu "
			    "bytes)\n",
			    (size_t)slot->header.ooblen,
			    (size_t)iter.req.ooblen);
			ret = -EOPNOTSUPP;
			goto exit;
		}

		/* Returning less data (or more) than what is requested is kinda
		 * odd and should be normally rejected, because there's no way
		 * to handle it properly against the original request.
		 *
		 * We could technically allow userspace to ACK less data bytes,
		 * but there's no real reason to allow this, for example - a
		 * NAND page of 2048 bytes should have that exact amount of
		 * (2048) data bytes.
		 * This stands in contrast to OOB bytes, which have a concept of
		 * user, free or reserved ranges.
		 *
		 * This check still works for partial reads (where we read less
		 * than a whole range of the data bytes in a page), whether
		 * because there's an origianl request to read less than that,
		 * or just because the last iteration has left us with less than
		 * a "full" size of data bytes to be read.
		 */
		if (slot->header.datalen != iter.req.datalen) {
			pr_warn_ratelimited(
			    "ufedm: incomplete read attempted "
			    "(ACKed %zu data bytes, should be %zu bytes)\n",
			    (size_t)slot->header.datalen,
			    (size_t)iter.req.datalen);
			ret = -EOPNOTSUPP;
			goto exit;
		}

		memcpy(iter.req.databuf.in, shm_slot->buf, iter.req.datalen);
		memcpy(iter.req.oobbuf.in,
		    (const u8 *)shm_slot->buf + proxy_dev->page_data_size,
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
	struct shared_mem_slot *shm_slot;
	struct ufedm_proxy_device *proxy_dev;
	struct nand_io_iter iter;
	struct nand_device *nand;
	struct mtd_info *backend;
	struct mtd_oob_ops per_page_raw_ops;
	size_t page_oob_size;
	size_t page_data_size;

	ret = ensure_safe_environment(mtd, ops, &proxy_dev, &dev);
	if (ret < 0)
		return ret;

	page_oob_size = proxy_dev->page_oob_size;
	page_data_size = proxy_dev->page_data_size;

	BUG_ON(page_data_size == 0);
	BUG_ON(page_oob_size == 0);

	nand = mtd_to_nanddev(proxy_dev->backend_dev);
	backend = dev->backend;

	ret = proxy_device_get_slot(proxy_dev, NAND_PAGE_WRITE, &slot);
	if (ret < 0)
		return ret;

	shm_slot = proxy_device_queue_and_slot_to_buf(
	    proxy_dev, slot->parentq->info.idx, slot->slot_idx);

	struct simple_nand_page_io_req simple_req;

	nanddev_io_for_each_page(nand, NAND_PAGE_WRITE, to, ops, &iter)
	{
		simple_req.datalen = iter.req.datalen;
		simple_req.ooblen = iter.req.ooblen;
		simple_req.databuf = iter.req.databuf.in;
		simple_req.oobbuf = iter.req.oobbuf.in;

		// FIXME: Can we integrate copy_nand_pos_to_to_io_pos_params
		// with proxy_device_io_slot_pub_new_packet tightly together?
		copy_nand_pos_to_to_io_pos_params(
		    &iter.req.pos, &simple_req.pos_params);
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

		/*
		 * An ACK was received, so we now process according to user
		 * returned lengths. We don't read the lengths from the shared
		 * memory buffer, although it is possible, but we validate the
		 * lengths upon the ACK ioctl.
		 *
		 * IMPORTANT NOTE:
		 * We DO NOT want to support partial page writes - this kind of
		 * behavior is not consistent across all NAND controllers, so we
		 * can't rely on it safely.
		 * Because of this, we must reject partial writes - i.e. any RAW
		 * write request that doesn't write the whole page + OOB
		 * together.
		 */
		if (slot->header.datalen != page_data_size ||
		    slot->header.ooblen != page_oob_size) {
			pr_warn_ratelimited(
			    "ufedm: incomplete write request attempted "
			    "(tried data: %zu bytes,oob: %zu bytes, should "
			    "be data: %zu bytes,oob: %zu bytes)\n",
			    (size_t)slot->header.datalen,
			    (size_t)slot->header.ooblen, page_data_size,
			    page_oob_size);
			ret = -EOPNOTSUPP;
			goto exit;
		}

		per_page_raw_ops.mode = MTD_OPS_RAW;
		per_page_raw_ops.len = page_data_size;
		per_page_raw_ops.ooblen = page_oob_size;
		per_page_raw_ops.datbuf = shm_slot->buf;
		per_page_raw_ops.oobbuf = shm_slot->buf + page_data_size;

		/* We can also fail right here as well.
		 * Common reasons are I/O issues in hardware, etc.
		 */
		ret = backend->_write_oob(backend, to, &per_page_raw_ops);
		if (ret < 0)
			goto exit;

		/* It's tempting to do this:
		 * ```
		 * ops->retlen += slot->header.datalen;
		 * ops->oobretlen += slot->header.ooblen;
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

static int upper_nand_erase(
    struct nand_device *nand, const struct nand_pos *pos)
{
	struct upper_mtd_device *upper_dev =
	    container_of(nand, struct upper_mtd_device, base);
	struct nand_device *backend_nand = mtd_to_nanddev(upper_dev->backend);
	BUG_ON(backend_nand == NULL);

	WARN_ON_ONCE(backend_nand->ops->erase == NULL);
	if (!backend_nand->ops->erase)
		return -EOPNOTSUPP;

	return backend_nand->ops->erase(backend_nand, pos);
}

static int upper_nand_markbad(
    struct nand_device *nand, const struct nand_pos *pos)
{
	struct upper_mtd_device *upper_dev =
	    container_of(nand, struct upper_mtd_device, base);
	struct nand_device *backend_nand = mtd_to_nanddev(upper_dev->backend);
	BUG_ON(backend_nand == NULL);

	WARN_ON_ONCE(backend_nand->ops->markbad == NULL);
	if (!backend_nand->ops->markbad)
		return -EOPNOTSUPP;

	return backend_nand->ops->markbad(backend_nand, pos);
}

static bool upper_nand_isbad(
    struct nand_device *nand, const struct nand_pos *pos)
{
	struct upper_mtd_device *upper_dev =
	    container_of(nand, struct upper_mtd_device, base);
	struct nand_device *backend_nand = mtd_to_nanddev(upper_dev->backend);
	BUG_ON(backend_nand == NULL);

	WARN_ON_ONCE(backend_nand->ops->isbad == NULL);
	if (!backend_nand->ops->isbad)
		return false;

	return backend_nand->ops->isbad(backend_nand, pos);
}

static const struct nand_ops upper_nand_ops = {
    .erase = upper_nand_erase,
    .markbad = upper_nand_markbad,
    .isbad = upper_nand_isbad,
};

static void copy_nand_device_mem_organization(
    const struct nand_device *src, struct nand_device *dest)
{
	struct nand_memory_organization *src_memorg =
	    nanddev_get_memorg((struct nand_device *)src);
	struct nand_memory_organization *dest_memorg = nanddev_get_memorg(dest);
	memcpy(
	    dest_memorg, src_memorg, sizeof(struct nand_memory_organization));
}

static int create_device(struct upper_mtd_device *dev, struct mtd_info *backend, struct ufedm_proxy_device *proxy_dev)
{
	int ret;
	struct mtd_info *mtd;

	if (backend->numeraseregions != 0) {
		pr_err("ufedm: backing MTD has different erasesizes, which we "
		       "don't support currently\n");
		return -EINVAL;
	}

	/* This is probably not possible to happen on a NAND flash MTD, but
	 * it's better to check this to ensure we don't really miss something
	 * and crash the kernel.
	 */
	if (!backend->_read_oob || !backend->_write_oob) {
		pr_err("ufedm: failed to register upper MTD on an MTD which "
		       "doesn't support _read_oob or _write_oob callbacks\n");
		return -EOPNOTSUPP;
	}

	mtd = nanddev_to_mtd(&dev->base);
	dev->backend = backend;

	copy_nand_device_mem_organization(mtd_to_nanddev(backend), &dev->base);
	ret = nanddev_init(&dev->base, &upper_nand_ops, THIS_MODULE);
	if (ret != 0) {
		pr_err("ufedm: failed to init nand_device for upper MTD "
		       "with error %d (%pe)\n",
		    ret, ERR_PTR(-ret));
		return ret;
	}

	/* Basic identity */
	mtd->name = "upper-mtd";
	mtd->flags = backend->flags;
	mtd->size = backend->size;
	mtd->erasesize = backend->erasesize;
	mtd->writesize = backend->writesize;

	mtd->_erase = upper_erase;

	mtd->_write_oob = upper_write_oob;
	mtd->_read_oob = upper_read_oob;

	/* oobsize is initialized by nanddev_init, but I/O operations, in
	 * MTD_AUTO_OOB mode, might check oobavail instead.
	 * The important thing is to remember that we remain in control, and
	 * adjusting the OOB "available" range in runtime is not something we
	 * would want to do, but reporting a different size is possible.
	 * See upper_mtd read callback to learn more about such scenario.
	 */
	mtd->oobavail = mtd->oobsize;

	mtd->priv = dev;

	// Connect a proxy_dev into our upper device
	// so it can deref it later on when doing I/O.
	dev->proxy_dev = proxy_dev;

	ret = mtd_device_register(mtd, NULL, 0);
	if (ret != 0) {
		pr_err("ufedm: failed to register upper MTD\n");
		return ret;
	}

	return 0;
}

static void destroy_device(struct upper_mtd_device *dev)
{
	mtd_device_unregister(nanddev_to_mtd(&dev->base));
	dev->proxy_dev = NULL;
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
