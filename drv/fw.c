/*
 * The firmware file: Broadcom's data the open code loads by name
 * (hw_fw_data() of open/include/bcm4360/hw.h). It is made at install time
 * from Broadcom's download and stays on the machine (b43-fwcutter model).
 */
#include <linux/mm.h>
#include <linux/unaligned.h>

#include "drv.h"

int bcm4360_fw_load(struct bcm4360_dev *dev)
{
	const struct bcm4360_fw_header *h;
	const struct firmware *fw;
	u32 i, count;
	int err;

	err = request_firmware(&fw, BCM4360_FW_NAME, &dev->pdev->dev);
	if (err) {
		dev_err(&dev->pdev->dev,
			"%s missing: run scripts/install.sh (it cuts the data out of Broadcom's driver)\n",
			BCM4360_FW_NAME);
		return err;
	}
	h = (const void *)fw->data;
	if (fw->size < sizeof(*h) || memcmp(h->magic, BCM4360_FW_MAGIC, 8))
		goto bad;
	count = le32_to_cpu(h->count);
	if (count > 4096 || sizeof(*h) + (size_t)count * sizeof(struct bcm4360_fw_entry) > fw->size)
		goto bad;
	dev->fw_dir = (const void *)(fw->data + sizeof(*h));
	for (i = 0; i < count; i++) {
		const struct bcm4360_fw_entry *e = &dev->fw_dir[i];
		u64 end = (u64)le32_to_cpu(e->offset) + le32_to_cpu(e->size);

		if (end > fw->size || strnlen(e->name, sizeof(e->name)) == sizeof(e->name))
			goto bad;
	}
	dev->fw = fw;
	dev->fw_count = count;
	return 0;
bad:
	dev_err(&dev->pdev->dev, "%s is damaged: run scripts/install.sh again\n",
		BCM4360_FW_NAME);
	release_firmware(fw);
	return -EINVAL;
}

void bcm4360_fw_release(struct bcm4360_dev *dev)
{
	kvfree(dev->clm);
	dev->clm = NULL;
	release_firmware(dev->fw);
	dev->fw = NULL;
	dev->fw_dir = NULL;
	dev->fw_count = 0;
}

/* hw.h */
const void *hw_fw_data(struct bcm4360_hw *hw, const char *name, u32 *size)
{
	struct bcm4360_dev *dev = bcm4360_dev_of(hw);
	u32 i;

	for (i = 0; i < dev->fw_count; i++) {
		const struct bcm4360_fw_entry *e = &dev->fw_dir[i];

		if (!strcmp(e->name, name)) {
			if (size)
				*size = le32_to_cpu(e->size);
			return dev->fw->data + le32_to_cpu(e->offset);
		}
	}
	return NULL;
}

/*
 * The regulatory database (docs/re/spec/clm.md) as clm_init() takes it. The
 * file holds it as "clm_db", Broadcom's tables from clm_header on, each 64-bit
 * pointer stored as an offset from the start of the entry; "clm_db.reloc"
 * lists the offsets of these pointers, "clm_db.root" the offsets of
 * clm_header and clm_data (le32 each; scripts/bcm4360-fwcut.py). It is copied
 * once, the pointers are made addresses of the copy, and the address of
 * clm_header in it is returned. The copy lives until bcm4360_fw_release().
 */
const void *bcm4360_fw_clm(struct bcm4360_dev *dev)
{
	const __le32 *reloc, *root;
	u32 size, rsize, nroot, i, hdr;
	const u8 *db;
	u8 *buf;

	if (dev->clm)
		return dev->clm + dev->clm_header;
	db = hw_fw_data(&dev->hw, "clm_db", &size);
	reloc = hw_fw_data(&dev->hw, "clm_db.reloc", &rsize);
	root = hw_fw_data(&dev->hw, "clm_db.root", &nroot);
	if (!db || !reloc || !root || rsize % 4 || nroot < 4)
		goto bad;
	hdr = le32_to_cpu(root[0]);
	if (size < 8 || hdr > size - 8)
		goto bad;
	buf = kvmalloc(size, GFP_KERNEL);
	if (!buf)
		return NULL;
	memcpy(buf, db, size);
	for (i = 0; i < rsize / 4; i++) {
		u32 off = le32_to_cpu(reloc[i]);
		u64 v;

		if (off > size - 8)
			goto bad_free;
		v = get_unaligned_le64(buf + off);
		if (v >= size)
			goto bad_free;
		put_unaligned_le64((u64)(uintptr_t)(buf + v), buf + off);
	}
	dev->clm = buf;
	dev->clm_header = hdr;
	return buf + hdr;
bad_free:
	kvfree(buf);
bad:
	dev_err(&dev->pdev->dev, "%s: no usable regulatory database\n", BCM4360_FW_NAME);
	return NULL;
}
