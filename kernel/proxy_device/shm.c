/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2026 Liav A
 */

#include <linux/bitmap.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/shmem_fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/vmalloc.h>

#include "proxy_device/shm.h"

static int shm_map_kernel(struct ufedm_proxy_device *dev)
{
	loff_t size;
	unsigned int i;
	int ret = 0;
	struct ufedm_shm_mapping *mapping = &dev->shm_mapping;
	struct file *filp = mapping->filp;

	size = i_size_read(file_inode(filp));
	WARN_ON(size != PAGE_ALIGN(dev->shm_info.total_buf_size));

	mapping->nr_pages = DIV_ROUND_UP(size, PAGE_SIZE);

	mapping->pages =
	    kcalloc(mapping->nr_pages, sizeof(*mapping->pages), GFP_KERNEL);
	if (!mapping->pages)
		return -ENOMEM;

	for (i = 0; i < mapping->nr_pages; i++) {
		struct page *page;

		page = shmem_read_mapping_page(filp->f_mapping, i);
		if (IS_ERR(page)) {
			ret = PTR_ERR(page);
			goto err_put_pages;
		}

		mapping->pages[i] = page;
	}

	mapping->kaddr =
	    vmap(mapping->pages, mapping->nr_pages, VM_MAP, PAGE_KERNEL);
	if (!mapping->kaddr) {
		ret = -ENOMEM;
		goto err_put_pages;
	}

	return 0;

err_put_pages:
	while (i--)
		put_page(mapping->pages[i]);

	kfree(mapping->pages);

	mapping->pages = NULL;
	mapping->nr_pages = 0;

	return ret;
}

static int create_shm_mapping(struct ufedm_proxy_device *dev)
{
	struct ufedm_shm_mapping *mapping = &dev->shm_mapping;

	// FIXME: I really **REALLY** don't like this.
	// But we don't have other choice right now.
	// Remove this ifdef soup once old kernels can be forgotten...
	//
	// I did a quick check on elixir.bootlin.com and kernel 7.0.0 is the
	// first to do the actual change:
	// https://elixir.bootlin.com/linux/v7.0-rc1/source/include/linux/shmem_fs.h#L106

#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 0, 0)
	mapping->filp = shmem_kernel_file_setup("ufedm_shm",
	    PAGE_ALIGN(dev->shm_info.total_buf_size),
	    mk_vma_flags(VM_DONTDUMP | VM_LOCKED));
#else
	mapping->filp = shmem_kernel_file_setup("ufedm_shm",
	    PAGE_ALIGN(dev->shm_info.total_buf_size), VM_DONTDUMP | VM_LOCKED);
#endif

	if (IS_ERR(mapping->filp))
		return PTR_ERR(mapping->filp);

	return 0;
}

static void remove_kernel_mapping(struct ufedm_shm_mapping *mapping)
{
	unsigned int i;

	if (mapping->kaddr)
		vunmap(mapping->kaddr);

	for (i = 0; i < mapping->nr_pages; i++)
		put_page(mapping->pages[i]);

	kfree(mapping->pages);

	mapping->kaddr = NULL;
	mapping->pages = NULL;
	mapping->nr_pages = 0;
}

static void revoke_user_shm_mapping(struct ufedm_shm_mapping *mapping)
{
	mutex_lock(&mapping->lock);

	if (mapping->revoked) {
		mutex_unlock(&mapping->lock);
		return;
	}

	mapping->revoked = true;
	unmap_mapping_range(mapping->filp->f_mapping, 0, 0, 1);

	mutex_unlock(&mapping->lock);
}

static void destroy_shm_mapping(struct ufedm_shm_mapping *mapping)
{
	fput(mapping->filp);
	mapping->filp = NULL;
}

int proxy_device_init_shared_memory(struct ufedm_proxy_device *dev)
{
	int ret;

	ret = create_shm_mapping(dev);
	if (ret < 0) {
		goto exit;
	}

	ret = shm_map_kernel(dev);
	if (ret < 0) {
		goto revert_shm_mapping_creation;
	}

	return 0;

revert_shm_mapping_creation:
	destroy_shm_mapping(&dev->shm_mapping);
exit:
	return ret;
}

void proxy_device_destroy_shared_memory(struct ufedm_shm_mapping *mapping)
{
	remove_kernel_mapping(mapping);
	revoke_user_shm_mapping(mapping);
	destroy_shm_mapping(mapping);
}

struct shared_mem_slot *proxy_device_queue_and_slot_to_buf(
    struct ufedm_proxy_device *dev, size_t queue_idx, size_t slot_idx)
{
	struct ufedm_shm_mapping *mapping = &dev->shm_mapping;
	if (queue_idx >= dev->shm_info.queues_count)
		return NULL;

	struct proxy_requests_queue *q = &dev->queues[queue_idx];

	if (slot_idx * dev->shm_info.slot_size >= q->info.mem_len)
		return NULL;

	u8 *addr = (u8 *)mapping->kaddr +  q->info.mem_offset +
		   (slot_idx * dev->shm_info.slot_size);
	return (struct shared_mem_slot *)addr;
}
