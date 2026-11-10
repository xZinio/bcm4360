/*
 * bcm4360_open: PCI device, interrupts, module (docs/re/07-open-driver.md).
 */
#include <linux/module.h>
#include <linux/interrupt.h>

#include "drv.h"

/* Broadcom's OS layer (src/shared/linux_osl.c): the open code runs on it
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

void bcm4360_plat_txdrop(void *plat, void *pkt)
{
	struct bcm4360_dev *dev = plat;

	__skb_queue_tail(&dev->txdropq, (struct sk_buff *)pkt);
}

void bcm4360_plat_tx_wake(void *plat, u8 fifo)
{
	struct bcm4360_dev *dev = plat;

	ieee80211_wake_queues(dev->ieee);
}

/* called with the lock held, as every call into the core */
void bcm4360_plat_fatal(void *plat)
{
	struct bcm4360_dev *dev = plat;

	if (!dev->dead)
		schedule_work(&dev->restart);
}

/*
 * trace=MS: log every step of bring-up and teardown, then wait MS ms (busy:
 * some steps run under the driver's spinlock), so that a log reader can save
 * the line to disk before the next step runs. For finding where the card
 * hangs the machine; 0 (default) logs nothing.
 */
static uint trace;
module_param(trace, uint, 0644);
MODULE_PARM_DESC(trace, "log each bring-up step and wait this many ms after it (0: off)");

static void bcm4360_trace(const char *step)
{
	uint ms = min(READ_ONCE(trace), 1000u);

	if (!ms)
		return;
	pr_info(BCM4360_DRV_NAME ": trace: %s\n", step);
	if (!in_hardirq())
		mdelay(ms);
}

void bcm4360_plat_trace(void *plat, const char *step)
{
	bcm4360_trace(step);
}

void bcm4360_plat_note(void *plat, const char *what, u32 val)
{
	struct bcm4360_dev *dev = plat;

	dev_warn_ratelimited(&dev->pdev->dev, "%s: 0x%08x\n", what, val);
}

void bcm4360_plat_debug(void *plat, const char *what, u32 val)
{
	struct bcm4360_dev *dev = plat;

	if (READ_ONCE(trace))
		dev_info_ratelimited(&dev->pdev->dev, "%s: 0x%08x\n", what, val);
}

const void *bcm4360_plat_clm(void *plat)
{
	return bcm4360_fw_clm(plat);
}

static irqreturn_t bcm4360_isr(int irq, void *data)
{
	struct bcm4360_dev *dev = data;

	static bool first = true;

	if (!READ_ONCE(dev->up))
		return IRQ_NONE;
	if (first) {
		first = false;
		bcm4360_trace("isr: first interrupt while up");
	}
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

/*
 * A fatal error of the core (bcm4360_plat_fatal). ieee80211_restart_hw()
 * wants the hardware stopped and the driver's state reset: mac80211 then
 * calls .start and adds the interface, stations and keys again.
 */
static void bcm4360_restart_work(struct work_struct *work)
{
	struct bcm4360_dev *dev = container_of(work, struct bcm4360_dev, restart);

	spin_lock_bh(&dev->lock);
	if (dev->dead) {		/* bcm4360_remove() is under way */
		spin_unlock_bh(&dev->lock);
		return;
	}
	dev_warn(&dev->pdev->dev, "hardware error: restarting\n");
	if (dev->up) {
		WRITE_ONCE(dev->up, false);
		bcm4360_core_intrsoff(dev->core);
		spin_unlock_bh(&dev->lock);
		synchronize_irq(dev->pdev->irq);
		spin_lock_bh(&dev->lock);
		bcm4360_core_down(dev->core);
	}
	dev->vif = NULL;
	dev->peer_map = 0;
	memset(dev->hwkey, 0, sizeof(dev->hwkey));
	dev->wpa_seen = false;
	dev->group_seen = false;
	spin_unlock_bh(&dev->lock);
	bcm4360_deliver(dev);
	ieee80211_restart_hw(dev->ieee);
}

/* the retry timeout of PCI configuration register 0x40 must be 0, as
 * Broadcom's glue sets it (src/wl/sys/wl_linux.c); pci_restore_state() does
 * not cover it, so the resume sets it again (as brcmsmac does) */
static void bcm4360_pci_retry_timeout(struct pci_dev *pdev)
{
	u32 val;

	pci_read_config_dword(pdev, 0x40, &val);
	if (val & 0xff00)
		pci_write_config_dword(pdev, 0x40, val & ~0xff00);
}

static int bcm4360_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct bcm4360_dev *dev;
	struct ieee80211_hw *ieee;
	void __iomem *bar0;
	int err;

	bcm4360_trace("probe: enable device");
	err = pcim_enable_device(pdev);
	if (err)
		return err;
	pci_set_master(pdev);
	bcm4360_pci_retry_timeout(pdev);
	/*
	 * 32-bit DMA only. The DMA layer passes 32-bit bus addresses (osl_dma_map
	 * returns unsigned int; the descriptors' high word is a constant), as
	 * Broadcom's code does: its glue never raises the mask from the PCI
	 * default of 32 bits. With a 64-bit mask, buffers above 4 GB lost their
	 * high bits and the card wrote received frames into other memory
	 * (2026-11-06: machine frozen; 11-07: DMA errors, nothing received).
	 */
	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (err)
		return err;
	bcm4360_trace("probe: map BAR0");
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
	skb_queue_head_init(&dev->txdropq);
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

	bcm4360_trace("probe: load firmware data");
	err = bcm4360_fw_load(dev);
	if (err)
		goto fail_osh;

	bcm4360_trace("probe: core attach");
	dev->core = bcm4360_core_attach(&dev->hw, dev->osh, pdev->vendor, pdev->device, dev,
					&err);
	if (!dev->core) {
		dev_err(&pdev->dev, "attach failed (%d)\n", err);
		err = -ENODEV;
		goto fail_fw;
	}
	bcm4360_core_get_info(dev->core, &dev->info);

	bcm4360_trace("probe: request irq");
	err = request_threaded_irq(pdev->irq, bcm4360_isr, bcm4360_dpc, IRQF_SHARED,
				   BCM4360_DRV_NAME, dev);
	if (err)
		goto fail_core;

	bcm4360_trace("probe: register with mac80211");
	err = bcm4360_mac80211_register(dev);
	if (err)
		goto fail_irq;
	dev_info(&pdev->dev, "BCM%x rev %u, %pM: open driver, no Broadcom code linked\n",
		 dev->info.chip_id, dev->info.chip_rev, dev->info.etheraddr);
	bcm4360_trace("probe: done");
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

	bcm4360_trace("remove");
	/*
	 * No restart from here on, before mac80211 lets go of the device: a
	 * restart that ran after ieee80211_unregister_hw() would queue mac80211's
	 * restart work on memory that is freed below. Set in the lock, as every
	 * call of bcm4360_plat_fatal() holds it: one that saw dead clear has
	 * queued the work already, and cancel_work_sync() takes it out or waits
	 * for it.
	 */
	spin_lock_bh(&dev->lock);
	dev->dead = true;
	spin_unlock_bh(&dev->lock);
	cancel_work_sync(&dev->restart);
	bcm4360_mac80211_unregister(dev);
	free_irq(pdev->irq, dev);
	bcm4360_core_detach(dev->core);
	/* .stop handed everything to mac80211; nothing is expected here */
	skb_queue_purge(&dev->rxq);
	skb_queue_purge(&dev->txsq);
	skb_queue_purge(&dev->txdropq);
	bcm4360_fw_release(dev);
	osl_detach(dev->osh);
	ieee80211_free_hw(dev->ieee);
}

/*
 * System sleep. mac80211 has stopped the device already (no WoWLAN: its
 * suspend calls .stop -> bcm4360_core_down) and starts it again after the
 * resume (.start -> bcm4360_core_up), so these only do what Broadcom's
 * wl_suspend/wl_resume add around wl_down/wl_up: the core powers the
 * hardware up again at the next up. The PCI core saves and restores the
 * configuration space and the power state.
 */
static int bcm4360_suspend(struct device *d)
{
	struct bcm4360_dev *dev = dev_get_drvdata(d);

	spin_lock_bh(&dev->lock);
	if (dev->up) {
		/* mac80211 did not stop it (not expected): down here */
		WRITE_ONCE(dev->up, false);
		bcm4360_core_intrsoff(dev->core);
		spin_unlock_bh(&dev->lock);
		synchronize_irq(dev->pdev->irq);
		spin_lock_bh(&dev->lock);
		bcm4360_core_down(dev->core);
	}
	bcm4360_core_suspend(dev->core);
	spin_unlock_bh(&dev->lock);
	bcm4360_deliver(dev);
	return 0;
}

static int bcm4360_resume(struct device *d)
{
	bcm4360_pci_retry_timeout(to_pci_dev(d));
	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(bcm4360_pm_ops, bcm4360_suspend, bcm4360_resume);

static struct pci_driver bcm4360_driver = {
	.name = BCM4360_DRV_NAME,
	.id_table = bcm4360_ids,
	.probe = bcm4360_probe,
	.remove = bcm4360_remove,
	.driver.pm = pm_sleep_ptr(&bcm4360_pm_ops),
};
module_pci_driver(bcm4360_driver);

MODULE_DESCRIPTION("Broadcom BCM4360 802.11ac, open driver (mac80211)");
MODULE_AUTHOR("bcm4360 project");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(BCM4360_DRV_VERSION);
MODULE_FIRMWARE(BCM4360_FW_NAME);
