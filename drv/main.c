// SPDX-License-Identifier: ISC
/*
 * bcm4360_open: PCI device, interrupts, module (docs/re/07-open-driver.md).
 */
#include <linux/module.h>
#include <linux/interrupt.h>

#include "drv.h"

/* Broadcom's OS layer (src/shared/linux_osl.c, ISC): the open code runs on it
 * in the hybrid module, and so it does here */
void *osl_attach(void *pdev, unsigned int bustype, bool pkttag);
void osl_detach(void *osh);
#define BCM4360_PCI_BUS		1
#define BCM4360_BAR0_WINSZ	(16 * 1024)

static const struct pci_device_id bcm4360_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, 0x4360) },
	{ PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, 0x43a0) },
	{ PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, 0x43a1) },
	{ PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, 0x43a2) },
	{ }
};
MODULE_DEVICE_TABLE(pci, bcm4360_ids);

/* the core's callbacks (core.h): queued here, handed to mac80211 by
 * bcm4360_deliver() once the lock is released */
void bcm4360_plat_rx(void *plat, void *pkt, const struct bcm4360_rxstatus *rs)
{
	struct bcm4360_dev *dev = plat;
	struct sk_buff *skb = pkt;

	BUILD_BUG_ON(sizeof(*rs) > sizeof(skb->cb));
	memcpy(skb->cb, rs, sizeof(*rs));
	__skb_queue_tail(&dev->rxq, skb);
}

void bcm4360_plat_txstatus(void *plat, void *pkt, const struct bcm4360_txstatus *ts)
{
	struct bcm4360_dev *dev = plat;
	struct sk_buff *skb = pkt;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	/* the rates asked for are still in info->control.rates (mac80211.c);
	 * the status is filled in there before mac80211 gets the frame */
	BUILD_BUG_ON(sizeof(*ts) > sizeof(info->status.status_driver_data));
	memcpy(info->status.status_driver_data, ts, sizeof(*ts));
	__skb_queue_tail(&dev->txsq, skb);
}

void bcm4360_plat_tx_wake(void *plat, u8 fifo)
{
	struct bcm4360_dev *dev = plat;

	ieee80211_wake_queues(dev->ieee);
}

void bcm4360_plat_fatal(void *plat)
{
	struct bcm4360_dev *dev = plat;

	schedule_work(&dev->restart);
}

const void *bcm4360_plat_clm(void *plat)
{
	return bcm4360_fw_clm(plat);
}

static irqreturn_t bcm4360_isr(int irq, void *data)
{
	struct bcm4360_dev *dev = data;

	if (!READ_ONCE(dev->up))
		return IRQ_NONE;
	return bcm4360_core_isr(dev->core) ? IRQ_WAKE_THREAD : IRQ_NONE;
}

static irqreturn_t bcm4360_dpc(int irq, void *data)
{
	struct bcm4360_dev *dev = data;
	bool more;

	do {
		spin_lock_bh(&dev->lock);
		more = dev->up && bcm4360_core_dpc(dev->core, 64);
		spin_unlock_bh(&dev->lock);
		bcm4360_deliver(dev);
		cond_resched();
	} while (more);
	return IRQ_HANDLED;
}

static void bcm4360_restart_work(struct work_struct *work)
{
	struct bcm4360_dev *dev = container_of(work, struct bcm4360_dev, restart);

	dev_warn(&dev->pdev->dev, "hardware error: restarting\n");
	ieee80211_restart_hw(dev->ieee);
}

static int bcm4360_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct bcm4360_dev *dev;
	struct ieee80211_hw *ieee;
	void __iomem *bar0;
	u32 val;
	int err;

	err = pcim_enable_device(pdev);
	if (err)
		return err;
	pci_set_master(pdev);
	/* the retry timeout of PCI configuration register 0x40 must be 0, as
	 * Broadcom's glue sets it (src/wl/sys/wl_linux.c) */
	pci_read_config_dword(pdev, 0x40, &val);
	if (val & 0xff00)
		pci_write_config_dword(pdev, 0x40, val & ~0xff00);
	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (err)
		err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (err)
		return err;
	bar0 = pcim_iomap(pdev, 0, BCM4360_BAR0_WINSZ);
	if (!bar0)
		return -ENOMEM;

	ieee = ieee80211_alloc_hw(sizeof(*dev), bcm4360_mac80211_ops_get());
	if (!ieee)
		return -ENOMEM;
	dev = ieee->priv;
	dev->ieee = ieee;
	dev->pdev = pdev;
	spin_lock_init(&dev->lock);
	skb_queue_head_init(&dev->rxq);
	skb_queue_head_init(&dev->txsq);
	INIT_WORK(&dev->restart, bcm4360_restart_work);
	SET_IEEE80211_DEV(ieee, &pdev->dev);
	pci_set_drvdata(pdev, dev);

	dev->osh = osl_attach(pdev, BCM4360_PCI_BUS, false);
	if (!dev->osh) {
		err = -ENOMEM;
		goto fail_hw;
	}
	dev->hw.bar0 = (void __force *)bar0;
	dev->hw.d11 = dev->hw.bar0;
	dev->hw.wrap = (u8 *)dev->hw.bar0 + 0x1000;
	dev->hw.pcie = (u8 *)dev->hw.bar0 + 0x2000;
	dev->hw.cc = (u8 *)dev->hw.bar0 + 0x3000;
	dev->hw.pdev = dev->osh;	/* hw_pcicfg_*() take the OS layer's handle */

	err = bcm4360_fw_load(dev);
	if (err)
		goto fail_osh;

	dev->core = bcm4360_core_attach(&dev->hw, dev->osh, pdev->vendor, pdev->device, dev,
					&err);
	if (!dev->core) {
		dev_err(&pdev->dev, "attach failed (%d)\n", err);
		err = -ENODEV;
		goto fail_fw;
	}
	bcm4360_core_get_info(dev->core, &dev->info);

	err = request_threaded_irq(pdev->irq, bcm4360_isr, bcm4360_dpc, IRQF_SHARED,
				   BCM4360_DRV_NAME, dev);
	if (err)
		goto fail_core;

	err = bcm4360_mac80211_register(dev);
	if (err)
		goto fail_irq;
	dev_info(&pdev->dev, "BCM%x rev %u, %pM: open driver, no Broadcom code linked\n",
		 dev->info.chip_id, dev->info.chip_rev, dev->info.etheraddr);
	return 0;

fail_irq:
	free_irq(pdev->irq, dev);
fail_core:
	bcm4360_core_detach(dev->core);
fail_fw:
	bcm4360_fw_release(dev);
fail_osh:
	osl_detach(dev->osh);
fail_hw:
	ieee80211_free_hw(ieee);
	return err;
}

static void bcm4360_remove(struct pci_dev *pdev)
{
	struct bcm4360_dev *dev = pci_get_drvdata(pdev);

	bcm4360_mac80211_unregister(dev);
	free_irq(pdev->irq, dev);
	cancel_work_sync(&dev->restart);
	bcm4360_core_detach(dev->core);
	bcm4360_fw_release(dev);
	osl_detach(dev->osh);
	ieee80211_free_hw(dev->ieee);
}

static struct pci_driver bcm4360_driver = {
	.name = BCM4360_DRV_NAME,
	.id_table = bcm4360_ids,
	.probe = bcm4360_probe,
	.remove = bcm4360_remove,
};
module_pci_driver(bcm4360_driver);

MODULE_DESCRIPTION("Broadcom BCM4360 802.11ac, open driver (mac80211)");
MODULE_AUTHOR("bcm4360 project");
/* everything here is ISC; Broadcom's data comes as a firmware file */
MODULE_LICENSE("Dual BSD/GPL");
MODULE_FIRMWARE(BCM4360_FW_NAME);
