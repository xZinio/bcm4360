// SPDX-License-Identifier: ISC
/*
 * bcm4360_open: the mac80211 side. mac80211 is the upper 802.11 layer
 * (joining, scanning, rate control with minstrel_ht, aggregation sessions,
 * crypto in software); the core does what the hardware needs.
 */
#include <linux/module.h>

#include "drv.h"

/* Broadcom's chanspec (src/shared/bcmwifi/include/bcmwifi_channels.h,
 * docs/re/spec/channel.md) */
#define CHSPEC_SB_SHIFT		8
#define CHSPEC_BW_20		0x1000
#define CHSPEC_BW_40		0x1800
#define CHSPEC_BW_80		0x2000
#define CHSPEC_BAND_2G		0x0000
#define CHSPEC_BAND_5G		0xc000

#define RATE(r, f)	{ .bitrate = (r), .hw_value = (r) / 5, .flags = (f) }

static const struct ieee80211_rate bcm4360_rates[] = {
	RATE(10, 0), RATE(20, IEEE80211_RATE_SHORT_PREAMBLE),
	RATE(55, IEEE80211_RATE_SHORT_PREAMBLE), RATE(110, IEEE80211_RATE_SHORT_PREAMBLE),
	RATE(60, 0), RATE(90, 0), RATE(120, 0), RATE(180, 0),
	RATE(240, 0), RATE(360, 0), RATE(480, 0), RATE(540, 0),
};
#define BCM4360_RATES_OFDM	4	/* the 5 GHz band starts at 6 Mb/s */

#define CHAN2G(c)	{ .band = NL80211_BAND_2GHZ, .center_freq = 2407 + 5 * (c), \
			  .hw_value = (c), .max_power = 20 }
#define CHAN5G(c)	{ .band = NL80211_BAND_5GHZ, .center_freq = 5000 + 5 * (c), \
			  .hw_value = (c), .max_power = 20 }

static const struct ieee80211_channel bcm4360_channels_2g[] = {
	CHAN2G(1), CHAN2G(2), CHAN2G(3), CHAN2G(4), CHAN2G(5), CHAN2G(6), CHAN2G(7),
	CHAN2G(8), CHAN2G(9), CHAN2G(10), CHAN2G(11), CHAN2G(12), CHAN2G(13),
	{ .band = NL80211_BAND_2GHZ, .center_freq = 2484, .hw_value = 14, .max_power = 20 },
};

static const struct ieee80211_channel bcm4360_channels_5g[] = {
	CHAN5G(36), CHAN5G(40), CHAN5G(44), CHAN5G(48), CHAN5G(52), CHAN5G(56),
	CHAN5G(60), CHAN5G(64), CHAN5G(100), CHAN5G(104), CHAN5G(108), CHAN5G(112),
	CHAN5G(116), CHAN5G(120), CHAN5G(124), CHAN5G(128), CHAN5G(132), CHAN5G(136),
	CHAN5G(140), CHAN5G(144), CHAN5G(149), CHAN5G(153), CHAN5G(157), CHAN5G(161),
	CHAN5G(165),
};

/* Broadcom's chanspec of a channel definition */
u16 bcm4360_chanspec_of(const struct cfg80211_chan_def *def)
{
	int ctl = ieee80211_frequency_to_channel(def->chan->center_freq);
	int center = ieee80211_frequency_to_channel(def->center_freq1);
	u16 band = def->chan->band == NL80211_BAND_2GHZ ? CHSPEC_BAND_2G : CHSPEC_BAND_5G;
	int lowest;

	switch (def->width) {
	case NL80211_CHAN_WIDTH_40:
		lowest = center - 2;
		return band | CHSPEC_BW_40 | ((ctl - lowest) / 4) << CHSPEC_SB_SHIFT | center;
	case NL80211_CHAN_WIDTH_80:
		lowest = center - 6;
		return band | CHSPEC_BW_80 | ((ctl - lowest) / 4) << CHSPEC_SB_SHIFT | center;
	default:
		return band | CHSPEC_BW_20 | ctl;
	}
}

static void bcm4360_init_bands(struct bcm4360_dev *dev)
{
	struct ieee80211_supported_band *b;
	int nss = hweight8(dev->info.rxchain) ? : 1;
	u16 vht_map = 0xffff;
	int i;

	for (i = 0; i < nss && i < 8; i++) {
		vht_map &= ~(3 << (2 * i));
		vht_map |= IEEE80211_VHT_MCS_SUPPORT_0_9 << (2 * i);
	}

	b = &dev->band_2g;
	b->band = NL80211_BAND_2GHZ;
	b->channels = kmemdup(bcm4360_channels_2g, sizeof(bcm4360_channels_2g), GFP_KERNEL);
	b->n_channels = b->channels ? ARRAY_SIZE(bcm4360_channels_2g) : 0;
	/* cfg80211 writes flags into the bitrates at registration: per band copies */
	b->bitrates = kmemdup(bcm4360_rates, sizeof(bcm4360_rates), GFP_KERNEL);
	b->n_bitrates = b->bitrates ? ARRAY_SIZE(bcm4360_rates) : 0;

	b = &dev->band_5g;
	b->band = NL80211_BAND_5GHZ;
	b->channels = kmemdup(bcm4360_channels_5g, sizeof(bcm4360_channels_5g), GFP_KERNEL);
	b->n_channels = b->channels ? ARRAY_SIZE(bcm4360_channels_5g) : 0;
	b->bitrates = kmemdup(bcm4360_rates + BCM4360_RATES_OFDM,
			      sizeof(bcm4360_rates) - BCM4360_RATES_OFDM * sizeof(bcm4360_rates[0]),
			      GFP_KERNEL);
	b->n_bitrates = b->bitrates ? ARRAY_SIZE(bcm4360_rates) - BCM4360_RATES_OFDM : 0;

	for (b = &dev->band_2g; b; b = b == &dev->band_2g ? &dev->band_5g : NULL) {
		struct ieee80211_sta_ht_cap *ht = &b->ht_cap;

		ht->ht_supported = true;
		ht->cap = IEEE80211_HT_CAP_SUP_WIDTH_20_40 | IEEE80211_HT_CAP_SGI_20 |
			  IEEE80211_HT_CAP_SGI_40 | IEEE80211_HT_CAP_DSSSCCK40 |
			  (1 << IEEE80211_HT_CAP_RX_STBC_SHIFT);
		ht->ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K;
		ht->ampdu_density = IEEE80211_HT_MPDU_DENSITY_8;
		for (i = 0; i < nss; i++)
			ht->mcs.rx_mask[i] = 0xff;
		ht->mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED;
		if (b == &dev->band_5g) {
			struct ieee80211_sta_vht_cap *vht = &b->vht_cap;

			vht->vht_supported = true;
			vht->cap = IEEE80211_VHT_CAP_SHORT_GI_80 | IEEE80211_VHT_CAP_RXSTBC_1 |
				   IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_3895;
			vht->vht_mcs.rx_mcs_map = cpu_to_le16(vht_map);
			vht->vht_mcs.tx_mcs_map = cpu_to_le16(vht_map);
		}
	}
}

static void bcm4360_free_bands(struct bcm4360_dev *dev)
{
	kfree(dev->band_2g.channels);
	kfree(dev->band_2g.bitrates);
	kfree(dev->band_5g.channels);
	kfree(dev->band_5g.bitrates);
}

/* --------------------------------------------------------------- receive */

/* index of a legacy rate (500 kb/s units) among the band's bitrates */
static int bcm4360_legacy_idx(enum nl80211_band band, u8 rate)
{
	int first = band == NL80211_BAND_2GHZ ? 0 : BCM4360_RATES_OFDM;
	int i;

	for (i = first; i < ARRAY_SIZE(bcm4360_rates); i++)
		if (bcm4360_rates[i].hw_value == rate)
			return i - first;
	return -1;
}

static bool bcm4360_rx_status(struct bcm4360_dev *dev, const struct bcm4360_rxstatus *rs,
			      struct ieee80211_rx_status *st)
{
	u8 ch = rs->chanspec & 0xff;
	enum nl80211_band band = (rs->chanspec & 0xc000) == CHSPEC_BAND_5G ?
				 NL80211_BAND_5GHZ : NL80211_BAND_2GHZ;
	int i;

	memset(st, 0, sizeof(*st));
	/* the control channel: the receive header's chanspec (frames.md) */
	if ((rs->chanspec & 0x3800) != CHSPEC_BW_20)
		ch = dev->chanspec & 0xff;
	st->band = band;
	st->freq = ieee80211_channel_to_frequency(ch, band);
	st->signal = rs->rssi;
	for (i = 0; i < 3; i++) {
		if (rs->chain_rssi[i]) {
			st->chains |= BIT(i);
			st->chain_signal[i] = rs->chain_rssi[i];
		}
	}
	st->mactime = rs->tsf;
	st->flag |= RX_FLAG_MACTIME_START;
	switch (rs->bw) {
	case 1:
		st->bw = RATE_INFO_BW_40;
		break;
	case 2:
		st->bw = RATE_INFO_BW_80;
		break;
	default:
		st->bw = RATE_INFO_BW_20;
	}
	if (rs->flags & BCM4360_RX_SGI)
		st->enc_flags |= RX_ENC_FLAG_SHORT_GI;
	if (rs->flags & BCM4360_RX_LDPC)
		st->enc_flags |= RX_ENC_FLAG_LDPC;
	if (rs->flags & BCM4360_RX_STBC)
		st->enc_flags |= 1 << RX_ENC_FLAG_STBC_SHIFT;
	if (rs->flags & BCM4360_RX_SHORTPRE)
		st->enc_flags |= RX_ENC_FLAG_SHORTPRE;
	if (rs->flags & BCM4360_RX_FCS_FAIL)
		st->flag |= RX_FLAG_FAILED_FCS_CRC;
	if (rs->flags & BCM4360_RX_DECRYPTED)
		st->flag |= RX_FLAG_DECRYPTED;
	switch (rs->enc) {
	case 1:
		st->encoding = RX_ENC_HT;
		st->rate_idx = rs->rate;
		break;
	case 2:
		st->encoding = RX_ENC_VHT;
		st->rate_idx = rs->rate;
		st->nss = rs->nss ? : 1;
		break;
	default:
		i = bcm4360_legacy_idx(band, rs->rate);
		if (i < 0)
			return false;
		st->encoding = RX_ENC_LEGACY;
		st->rate_idx = i;
	}
	return true;
}

/* -------------------------------------------------------------- transmit */

static void bcm4360_txrate(const struct ieee80211_tx_rate *r, enum nl80211_band band,
			   struct bcm4360_txrate *out)
{
	memset(out, 0, sizeof(*out));
	out->tries = r->count;
	if (r->flags & IEEE80211_TX_RC_VHT_MCS) {
		out->enc = 2;
		out->rate = ieee80211_rate_get_vht_mcs(r);
		out->nss = ieee80211_rate_get_vht_nss(r);
	} else if (r->flags & IEEE80211_TX_RC_MCS) {
		out->enc = 1;
		out->rate = r->idx;
	} else {
		int first = band == NL80211_BAND_2GHZ ? 0 : BCM4360_RATES_OFDM;

		out->rate = bcm4360_rates[first + r->idx].hw_value;
		if (r->flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE)
			out->flags |= BCM4360_TX_SHORTPRE;
	}
	if (r->flags & IEEE80211_TX_RC_80_MHZ_WIDTH)
		out->bw = 2;
	else if (r->flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
		out->bw = 1;
	if (r->flags & IEEE80211_TX_RC_SHORT_GI)
		out->flags |= BCM4360_TX_SGI;
}

/* the transmit FIFO of an access category (frames.md, "FIFOs") */
static const u8 bcm4360_ac_fifo[IEEE80211_NUM_ACS] = {
	[IEEE80211_AC_VO] = 3, [IEEE80211_AC_VI] = 2,
	[IEEE80211_AC_BE] = 1, [IEEE80211_AC_BK] = 0,
};

/* 0: drop every frame instead of transmitting it (scripts/open-test.sh,
 * stage rx: the first tests on a card receive only) */
static bool allow_tx = true;
module_param(allow_tx, bool, 0644);
MODULE_PARM_DESC(allow_tx, "transmit frames (1, default) or drop them all (0: receive-only test)");

/* per station: the index the core knows its A-MPDU sessions by */
struct bcm4360_sta {
	u8 peer;
};

static int bcm4360_sta_add(struct ieee80211_hw *ieee, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta)
{
	struct bcm4360_dev *dev = ieee->priv;
	struct bcm4360_sta *ps = (struct bcm4360_sta *)sta->drv_priv;
	int peer;

	spin_lock_bh(&dev->lock);
	peer = ffz(dev->peer_map);
	if (peer < BCM4360_AMPDU_NPEERS)
		dev->peer_map |= BIT(peer);
	spin_unlock_bh(&dev->lock);
	if (peer >= BCM4360_AMPDU_NPEERS)
		return -ENOSPC;
	ps->peer = peer;
	return 0;
}

static int bcm4360_sta_remove(struct ieee80211_hw *ieee, struct ieee80211_vif *vif,
			      struct ieee80211_sta *sta)
{
	struct bcm4360_dev *dev = ieee->priv;
	struct bcm4360_sta *ps = (struct bcm4360_sta *)sta->drv_priv;
	int tid;

	spin_lock_bh(&dev->lock);
	for (tid = 0; tid < IEEE80211_NUM_TIDS; tid++)
		bcm4360_core_ampdu_tx_stop(dev->core, ps->peer, tid);
	dev->peer_map &= ~BIT(ps->peer);
	spin_unlock_bh(&dev->lock);
	return 0;
}

static void bcm4360_tx(struct ieee80211_hw *ieee, struct ieee80211_tx_control *control,
		       struct sk_buff *skb)
{
	struct bcm4360_dev *dev = ieee->priv;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct bcm4360_txparams p;
	enum nl80211_band band = info->band;
	int i, err;

	if (!READ_ONCE(allow_tx)) {
		ieee80211_free_txskb(ieee, skb);
		return;
	}
	memset(&p, 0, sizeof(p));
	p.fifo = bcm4360_ac_fifo[skb_get_queue_mapping(skb)];
	for (i = 0; i < IEEE80211_TX_MAX_RATES && info->control.rates[i].idx >= 0 &&
		    info->control.rates[i].count; i++)
		bcm4360_txrate(&info->control.rates[i], band, &p.rates[p.nrates++]);
	if (!p.nrates) {
		p.rates[0].rate = band == NL80211_BAND_2GHZ ? 2 : 12;	/* 1 or 6 Mb/s */
		p.rates[0].tries = 1;
		p.nrates = 1;
	}
	p.no_ack = !!(info->flags & IEEE80211_TX_CTL_NO_ACK);
	p.rts = !!(info->control.rates[0].flags & IEEE80211_TX_RC_USE_RTS_CTS);
	p.cts = !!(info->control.rates[0].flags & IEEE80211_TX_RC_USE_CTS_PROTECT);
	p.seq_by_hw = !!(info->flags & IEEE80211_TX_CTL_ASSIGN_SEQ);
	if ((info->flags & IEEE80211_TX_CTL_AMPDU) && control->sta) {
		/* a frame of a Block Ack session: mac80211 gave it its sequence number
		 * (docs/re/spec/ampdu.md 1.4) */
		p.ampdu = 1;
		p.tid = ieee80211_get_tid((struct ieee80211_hdr *)skb->data);
		p.peer = ((struct bcm4360_sta *)control->sta->drv_priv)->peer;
		p.seq_by_hw = 0;
	}

	spin_lock_bh(&dev->lock);
	err = dev->up ? bcm4360_core_tx(dev->core, skb, &p) : -1;
	if (err > 0)
		ieee80211_stop_queues(ieee);	/* taken, and the FIFO is full now */
	spin_unlock_bh(&dev->lock);
	if (err < 0)
		ieee80211_free_txskb(ieee, skb);
}

/* hand the frames and transmit status queued by the core's callbacks to
 * mac80211; called without the lock */
void bcm4360_deliver(struct bcm4360_dev *dev)
{
	struct sk_buff_head rxq, txsq;
	struct sk_buff *skb;

	__skb_queue_head_init(&rxq);
	__skb_queue_head_init(&txsq);
	spin_lock_bh(&dev->lock);
	skb_queue_splice_init(&dev->rxq, &rxq);
	skb_queue_splice_init(&dev->txsq, &txsq);
	spin_unlock_bh(&dev->lock);

	while ((skb = __skb_dequeue(&rxq))) {
		struct bcm4360_rxstatus rs;

		memcpy(&rs, skb->cb, sizeof(rs));
		if (!bcm4360_rx_status(dev, &rs, IEEE80211_SKB_RXCB(skb))) {
			dev_kfree_skb(skb);
			continue;
		}
		ieee80211_rx_ni(dev->ieee, skb);
	}
	while ((skb = __skb_dequeue(&txsq))) {
		struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
		struct bcm4360_txstatus ts;
		u8 asked[IEEE80211_TX_MAX_RATES];
		int i, left;

		memcpy(&ts, info->status.status_driver_data, sizeof(ts));
		left = ts.tries;
		/* clear_status zeroes the try counts asked for: keep them */
		for (i = 0; i < IEEE80211_TX_MAX_RATES; i++)
			asked[i] = info->status.rates[i].count;
		ieee80211_tx_info_clear_status(info);
		for (i = 0; i < IEEE80211_TX_MAX_RATES; i++) {
			struct ieee80211_tx_rate *r = &info->status.rates[i];

			r->count = asked[i];
			if (ts.ampdu) {
				/* ampdu.md 2.6: the aggregate's tries are on its first MPDU
				 * (one per rate used); the others only say acked or not */
				if (ts.ampdu_len && r->idx >= 0 && i <= ts.rate_idx &&
				    (i == 0 || i == ts.rate_idx))
					r->count = 1;
				else {
					r->idx = -1;
					r->count = 0;
				}
				continue;
			}
			if (i > ts.rate_idx || r->idx < 0 || left <= 0) {
				r->idx = -1;
				r->count = 0;
				continue;
			}
			if (i < ts.rate_idx)
				left -= r->count;
			else
				r->count = left;
		}
		if (ts.ampdu) {
			info->flags |= IEEE80211_TX_STAT_AMPDU;
			info->status.ampdu_len = ts.ampdu_len;
			info->status.ampdu_ack_len = ts.ampdu_ack_len;
		}
		if (ts.acked && !(info->flags & IEEE80211_TX_CTL_NO_ACK))
			info->flags |= IEEE80211_TX_STAT_ACK;
		ieee80211_tx_status_ni(dev->ieee, skb);
	}
}

/* ------------------------------------------------------------ operations */

static int bcm4360_start(struct ieee80211_hw *ieee)
{
	struct bcm4360_dev *dev = ieee->priv;
	struct cfg80211_chan_def *def = &ieee->conf.chandef;
	int err;

	spin_lock_bh(&dev->lock);
	dev->chanspec = def->chan ? bcm4360_chanspec_of(def) : CHSPEC_BAND_2G | CHSPEC_BW_20 | 1;
	err = bcm4360_core_up(dev->core, dev->chanspec);
	if (!err) {
		WRITE_ONCE(dev->up, true);
		bcm4360_core_intrson(dev->core);
	}
	spin_unlock_bh(&dev->lock);
	return err ? -EIO : 0;
}

static void bcm4360_stop(struct ieee80211_hw *ieee, bool suspend)
{
	struct bcm4360_dev *dev = ieee->priv;

	spin_lock_bh(&dev->lock);
	WRITE_ONCE(dev->up, false);
	bcm4360_core_intrsoff(dev->core);
	spin_unlock_bh(&dev->lock);
	synchronize_irq(dev->pdev->irq);
	spin_lock_bh(&dev->lock);
	bcm4360_core_down(dev->core);
	spin_unlock_bh(&dev->lock);
	bcm4360_deliver(dev);
}

static int bcm4360_add_interface(struct ieee80211_hw *ieee, struct ieee80211_vif *vif)
{
	struct bcm4360_dev *dev = ieee->priv;

	if (dev->vif)
		return -EBUSY;
	if (vif->type != NL80211_IFTYPE_STATION)
		return -EOPNOTSUPP;
	dev->vif = vif;
	spin_lock_bh(&dev->lock);
	bcm4360_core_set_macaddr(dev->core, vif->addr);
	spin_unlock_bh(&dev->lock);
	return 0;
}

static void bcm4360_remove_interface(struct ieee80211_hw *ieee, struct ieee80211_vif *vif)
{
	struct bcm4360_dev *dev = ieee->priv;

	dev->vif = NULL;
}

static int bcm4360_config(struct ieee80211_hw *ieee, int radio_idx, u32 changed)
{
	struct bcm4360_dev *dev = ieee->priv;
	struct ieee80211_conf *conf = &ieee->conf;
	int err = 0;

	spin_lock_bh(&dev->lock);
	if ((changed & IEEE80211_CONF_CHANGE_CHANNEL) && conf->chandef.chan) {
		u16 cs = bcm4360_chanspec_of(&conf->chandef);

		if (cs != dev->chanspec) {
			err = bcm4360_core_set_chanspec(dev->core, cs);
			if (!err)
				dev->chanspec = cs;
		}
	}
	if (changed & IEEE80211_CONF_CHANGE_POWER)
		bcm4360_core_set_txpower(dev->core, conf->power_level > 0 ? conf->power_level * 4 : 0);
	spin_unlock_bh(&dev->lock);
	return err ? -EIO : 0;
}

static void bcm4360_bss_info_changed(struct ieee80211_hw *ieee, struct ieee80211_vif *vif,
				     struct ieee80211_bss_conf *info, u64 changed)
{
	struct bcm4360_dev *dev = ieee->priv;
	struct bcm4360_bss bss;

	if (!(changed & (BSS_CHANGED_BSSID | BSS_CHANGED_ASSOC | BSS_CHANGED_ERP_SLOT |
			 BSS_CHANGED_ERP_PREAMBLE | BSS_CHANGED_ERP_CTS_PROT |
			 BSS_CHANGED_BEACON_INT | BSS_CHANGED_BASIC_RATES |
			 BSS_CHANGED_BEACON_INFO)))
		return;
	memset(&bss, 0, sizeof(bss));
	if (info->bssid)
		memcpy(bss.bssid, info->bssid, ETH_ALEN);
	bss.associated = vif->cfg.assoc;
	bss.aid = vif->cfg.aid;
	bss.beacon_int = info->beacon_int;
	bss.dtim_period = info->dtim_period;
	bss.short_slot = info->use_short_slot;
	bss.short_preamble = info->use_short_preamble;
	bss.cts_prot = info->use_cts_prot;
	bss.tsf = info->sync_tsf;
	bss.basic_rates = info->basic_rates;
	spin_lock_bh(&dev->lock);
	bcm4360_core_set_bss(dev->core, &bss);
	spin_unlock_bh(&dev->lock);
}

#define BCM4360_FIF	(FIF_ALLMULTI | FIF_FCSFAIL | FIF_CONTROL | FIF_OTHER_BSS | \
			 FIF_BCN_PRBRESP_PROMISC | FIF_PROBE_REQ)

static void bcm4360_configure_filter(struct ieee80211_hw *ieee, unsigned int changed,
				     unsigned int *total, u64 multicast)
{
	struct bcm4360_dev *dev = ieee->priv;
	u32 f = 0;

	*total &= BCM4360_FIF;
	if (*total & FIF_OTHER_BSS)
		f |= BCM4360_FILTER_PROMISC;
	if (*total & FIF_BCN_PRBRESP_PROMISC)
		f |= BCM4360_FILTER_BEACONS | BCM4360_FILTER_PROBE_RESP;
	if (*total & FIF_CONTROL)
		f |= BCM4360_FILTER_CONTROL;
	if (*total & FIF_FCSFAIL)
		f |= BCM4360_FILTER_FCSFAIL;
	spin_lock_bh(&dev->lock);
	bcm4360_core_set_filter(dev->core, f);
	spin_unlock_bh(&dev->lock);
}

static void bcm4360_sw_scan_start(struct ieee80211_hw *ieee, struct ieee80211_vif *vif,
				  const u8 *mac_addr)
{
	struct bcm4360_dev *dev = ieee->priv;

	spin_lock_bh(&dev->lock);
	bcm4360_core_set_scanning(dev->core, true);
	spin_unlock_bh(&dev->lock);
}

static void bcm4360_sw_scan_complete(struct ieee80211_hw *ieee, struct ieee80211_vif *vif)
{
	struct bcm4360_dev *dev = ieee->priv;

	spin_lock_bh(&dev->lock);
	bcm4360_core_set_scanning(dev->core, false);
	spin_unlock_bh(&dev->lock);
}

static int bcm4360_conf_tx(struct ieee80211_hw *ieee, struct ieee80211_vif *vif,
			   unsigned int link_id, u16 ac,
			   const struct ieee80211_tx_queue_params *q)
{
	struct bcm4360_dev *dev = ieee->priv;

	spin_lock_bh(&dev->lock);
	bcm4360_core_set_edca(dev->core, bcm4360_ac_fifo[ac], q->cw_min, q->cw_max, q->aifs,
			      q->txop);
	spin_unlock_bh(&dev->lock);
	return 0;
}

static u64 bcm4360_get_tsf(struct ieee80211_hw *ieee, struct ieee80211_vif *vif)
{
	struct bcm4360_dev *dev = ieee->priv;
	u64 tsf;

	spin_lock_bh(&dev->lock);
	tsf = bcm4360_core_get_tsf(dev->core);
	spin_unlock_bh(&dev->lock);
	return tsf;
}

/* ampdu.md 1.3: the most MPDUs in an A-MPDU to this peer, by its spatial streams */
static u8 bcm4360_ampdu_max_mpdus(struct ieee80211_sta *sta)
{
	if (sta->deflink.vht_cap.vht_supported) {
		u16 map = le16_to_cpu(sta->deflink.vht_cap.vht_mcs.rx_mcs_map);

		if (((map >> 4) & 3) != IEEE80211_VHT_MCS_NOT_SUPPORTED)
			return 64;
		if (((map >> 2) & 3) == IEEE80211_VHT_MCS_NOT_SUPPORTED)
			return 32;
		return 48;
	}
	if (sta->deflink.ht_cap.mcs.rx_mask[2])
		return 64;
	if (!sta->deflink.ht_cap.mcs.rx_mask[1])
		return 32;
	return 48;
}

/* ampdu.md 3.1: Block Ack sessions. Neither side touches the hardware: the session
 * decides what the core puts into the headers of the MPDUs. Receive: the microcode
 * acknowledges and mac80211 reorders (ampdu.md 3.3), nothing to do. */
static int bcm4360_ampdu_action(struct ieee80211_hw *ieee, struct ieee80211_vif *vif,
				struct ieee80211_ampdu_params *params)
{
	struct bcm4360_dev *dev = ieee->priv;
	struct ieee80211_sta *sta = params->sta;
	struct bcm4360_sta *ps = (struct bcm4360_sta *)sta->drv_priv;
	u8 tid = params->tid;

	switch (params->action) {
	case IEEE80211_AMPDU_RX_START:
	case IEEE80211_AMPDU_RX_STOP:
		return 0;
	case IEEE80211_AMPDU_TX_START:
		spin_lock_bh(&dev->lock);
		bcm4360_core_ampdu_tx_start(dev->core, ps->peer, tid, params->ssn,
					    bcm4360_ampdu_max_mpdus(sta),
					    sta->deflink.ht_cap.ampdu_density);
		spin_unlock_bh(&dev->lock);
		ieee80211_start_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		return 0;
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		spin_lock_bh(&dev->lock);
		bcm4360_core_ampdu_tx_operational(dev->core, ps->peer, tid,
						  min_t(u16, params->buf_size, 64));
		spin_unlock_bh(&dev->lock);
		return 0;
	case IEEE80211_AMPDU_TX_STOP_CONT:
	case IEEE80211_AMPDU_TX_STOP_FLUSH:
	case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
		spin_lock_bh(&dev->lock);
		bcm4360_core_ampdu_tx_stop(dev->core, ps->peer, tid);
		spin_unlock_bh(&dev->lock);
		/* MPDUs already posted complete with their statuses (ampdu.md 3.2) */
		if (params->action == IEEE80211_AMPDU_TX_STOP_CONT)
			ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		return 0;
	}
	return -EOPNOTSUPP;
}

static const struct ieee80211_ops bcm4360_ops = {
	.tx = bcm4360_tx,
	.wake_tx_queue = ieee80211_handle_wake_tx_queue,
	.start = bcm4360_start,
	.stop = bcm4360_stop,
	.add_interface = bcm4360_add_interface,
	.remove_interface = bcm4360_remove_interface,
	.config = bcm4360_config,
	.bss_info_changed = bcm4360_bss_info_changed,
	.configure_filter = bcm4360_configure_filter,
	.sw_scan_start = bcm4360_sw_scan_start,
	.sw_scan_complete = bcm4360_sw_scan_complete,
	.conf_tx = bcm4360_conf_tx,
	.get_tsf = bcm4360_get_tsf,
	.sta_add = bcm4360_sta_add,
	.sta_remove = bcm4360_sta_remove,
	.ampdu_action = bcm4360_ampdu_action,
	.add_chanctx = ieee80211_emulate_add_chanctx,
	.remove_chanctx = ieee80211_emulate_remove_chanctx,
	.change_chanctx = ieee80211_emulate_change_chanctx,
	.switch_vif_chanctx = ieee80211_emulate_switch_vif_chanctx,
};

const struct ieee80211_ops *bcm4360_mac80211_ops_get(void)
{
	return &bcm4360_ops;
}

int bcm4360_mac80211_register(struct bcm4360_dev *dev)
{
	struct ieee80211_hw *ieee = dev->ieee;
	struct wiphy *wiphy = ieee->wiphy;
	int err;

	bcm4360_init_bands(dev);
	if (dev->info.bands & 1)
		wiphy->bands[NL80211_BAND_2GHZ] = &dev->band_2g;
	if (dev->info.bands & 2)
		wiphy->bands[NL80211_BAND_5GHZ] = &dev->band_5g;
	wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION);
	wiphy->available_antennas_tx = dev->info.txchain;
	wiphy->available_antennas_rx = dev->info.rxchain;
	ieee80211_hw_set(ieee, SIGNAL_DBM);
	ieee80211_hw_set(ieee, RX_INCLUDES_FCS);	/* frames.md 1.8: the FCS stays on the frame */
	ieee80211_hw_set(ieee, REPORTS_TX_ACK_STATUS);
	ieee80211_hw_set(ieee, MFP_CAPABLE);	/* crypto in software */
	ieee80211_hw_set(ieee, AMPDU_AGGREGATION);	/* ampdu.md: the microcode builds the A-MPDUs */
	ieee->sta_data_size = sizeof(struct bcm4360_sta);
	ieee->queues = IEEE80211_NUM_ACS;
	ieee->max_rates = 4;
	ieee->max_rate_tries = 11;
	ieee->extra_tx_headroom = bcm4360_core_tx_headroom(dev->core);
	SET_IEEE80211_PERM_ADDR(ieee, dev->info.etheraddr);
	err = ieee80211_register_hw(ieee);
	if (err)
		bcm4360_free_bands(dev);
	return err;
}

void bcm4360_mac80211_unregister(struct bcm4360_dev *dev)
{
	ieee80211_unregister_hw(dev->ieee);
	bcm4360_free_bands(dev);
}
