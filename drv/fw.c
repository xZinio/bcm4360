// SPDX-License-Identifier: ISC
/*
 * The firmware file: Broadcom's data the open code loads by name
 * (hw_fw_data() of open/include/bcm4360/hw.h). It is made at install time
 * from Broadcom's download and stays on the machine (b43-fwcutter model).
 */
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
