/*
 * bcm4360_open: the open mac80211 driver for the BCM4360
 * (docs/re/07-open-driver.md). This is the Linux side: PCI, interrupts,
 * firmware file, mac80211. The hardware work is done by the core
 * (open/include/bcm4360/core.h) and the open layers below it.
 */
#ifndef BCM4360_DRV_H
#define BCM4360_DRV_H

#include <linux/pci.h>
#include <linux/firmware.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <net/mac80211.h>

#include <bcm4360/core.h>

#define BCM4360_DRV_NAME	"bcm4360_open"
/* Broadcom's data (microcode, initialisation lists, PHY and radio tables,
 * regulatory database), cut out of Broadcom's download at install time
 * (scripts/bcm4360-fwcut.py, docs/re/07-open-driver.md); never distributed */
#define BCM4360_FW_NAME		"bcm4360/bcm4360-data.bin"

/* the firmware file: a header, a directory of named entries, the data */
#define BCM4360_FW_MAGIC	"B4360FW1"
struct bcm4360_fw_header {
	char magic[8];
	__le32 count;		/* entries */
	__le32 version;		/* Broadcom release: 0x061edf0f = 6.30.223.271 */
};

struct bcm4360_fw_entry {
	char name[48];		/* NUL-terminated */
	__le32 offset;		/* from the start of the file */
	__le32 size;		/* bytes */
};

struct bcm4360_dev {
	/* first: the open code's hardware handle; hw_fw_data() and the core's
	 * callbacks find the device from it */
	struct bcm4360_hw hw;
	struct pci_dev *pdev;
	void *osh;		/* Broadcom's OS layer (src/shared/linux_osl.c) */
	struct ieee80211_hw *ieee;
	struct bcm4360_core *core;
	struct bcm4360_core_info info;

	const struct firmware *fw;
	const struct bcm4360_fw_entry *fw_dir;
	u32 fw_count;
	/* bcm4360_fw_clm(): the regulatory database, copied and relocated */
	u8 *clm;
	u32 clm_header;		/* offset of clm_header in it */

	/* serialises every call into the core but bcm4360_core_isr() */
	spinlock_t lock;
	/* frames and transmit status the core reported while the lock was
	 * held, handed to mac80211 after it is released */
	struct sk_buff_head rxq;
	struct sk_buff_head txsq;

	struct ieee80211_vif *vif;	/* the one interface (station) */
	u32 peer_map;		/* A-MPDU peer indices in use (mac80211.c) */
	/* hardware keys and power save (mac80211.c); both under dev->lock */
	struct ieee80211_key_conf *hwkey[BCM4360_NKEYS];	/* keys in the core, by entry */
	bool wpa_seen;		/* a key was set since the association */
	bool group_seen;	/* a group key was set since the association */
	bool up;
	u16 chanspec;
	struct work_struct restart;
	/* rfkill (mac80211.c): the core reports the hardware switch from a timer, in the lock;
	 * the work calls cfg80211, which may sleep */
	struct work_struct rfkill_work;
	bool rfkill_hw;
	bool rfkill_dead;	/* set before unregistering: the work does nothing more */
	u8 bssid[ETH_ALEN];	/* the BSS being joined or joined, for the receive-signal tracking */

	struct ieee80211_supported_band band_2g;
	struct ieee80211_supported_band band_5g;
	/* received frames by what the microcode said about them (card debugging, logged at
	 * every stop): marked decrypted, marked decryption failed (dropped), dropped for
	 * an unknown rate, part of an A-MPDU, passed to mac80211 */
	u32 rx_n_decrypted, rx_n_decrypt_err, rx_n_badrate, rx_n_ampdu, rx_n_passed;
	u32 rx_n_nokey;		/* decryption flags for an entry without a key: cleared */
};

static inline struct bcm4360_dev *bcm4360_dev_of(struct bcm4360_hw *hw)
{
	return container_of(hw, struct bcm4360_dev, hw);
}

/* fw.c */
int bcm4360_fw_load(struct bcm4360_dev *dev);
void bcm4360_fw_release(struct bcm4360_dev *dev);
/* the regulatory database for clm_init() (its clm_header); NULL if missing */
const void *bcm4360_fw_clm(struct bcm4360_dev *dev);

/* mac80211.c */
const struct ieee80211_ops *bcm4360_mac80211_ops_get(void);
int bcm4360_mac80211_register(struct bcm4360_dev *dev);
void bcm4360_mac80211_unregister(struct bcm4360_dev *dev);
void bcm4360_deliver(struct bcm4360_dev *dev);
u16 bcm4360_chanspec_of(const struct cfg80211_chan_def *def);

#endif
