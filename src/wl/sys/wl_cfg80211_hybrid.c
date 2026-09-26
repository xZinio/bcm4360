/*
 * Linux-specific portion of Broadcom 802.11abg Networking Device Driver
 * cfg80211 interface
 *
 * Copyright (C) 2015, Broadcom Corporation. All Rights Reserved.
 * 
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $Id: wl_cfg80211.c,v 1.1.6.4 2011-02-11 00:22:09 $
 */

#if defined(USE_CFG80211)

#define LINUX_PORT
#include <typedefs.h>
#include <linuxver.h>
#include <osl.h>

#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/netdevice.h>
#include <linux/ieee80211.h>
#include <net/cfg80211.h>
#include <linux/nl80211.h>
#include <net/rtnetlink.h>
#include <bcmutils.h>
#include <bcmendian.h>
#include <wlioctl.h>
#include <proto/802.11.h>
#include <wl_cfg80211_hybrid.h>
#include <wl_linux.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
#include <linux/sched/signal.h>
#endif

#define EVENT_TYPE(e) dtoh32((e)->event_type)
#define EVENT_FLAGS(e) dtoh16((e)->flags)
#define EVENT_STATUS(e) dtoh32((e)->status)

#ifdef BCMDBG
u32 wl_dbg_level = WL_DBG_ERR | WL_DBG_INFO;
#else
u32 wl_dbg_level = WL_DBG_ERR;
#endif

static s32 wl_cfg80211_change_iface(struct wiphy *wiphy, struct net_device *ndev,
           enum nl80211_iftype type,
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0)
           u32 *flags,
#endif
	   struct vif_params *params);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
static s32
wl_cfg80211_scan(struct wiphy *wiphy,
                 struct cfg80211_scan_request *request);
#else
static s32 wl_cfg80211_scan(struct wiphy *wiphy, struct net_device *ndev,
           struct cfg80211_scan_request *request);
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 17, 0)
static s32 wl_cfg80211_set_wiphy_params(struct wiphy *wiphy, s32 radio_idx, u32 changed);
#else
static s32 wl_cfg80211_set_wiphy_params(struct wiphy *wiphy, u32 changed);
#endif
static s32 wl_cfg80211_join_ibss(struct wiphy *wiphy, struct net_device *dev,
           struct cfg80211_ibss_params *params);
static s32 wl_cfg80211_leave_ibss(struct wiphy *wiphy, struct net_device *dev);

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 16, 0)
static s32 wl_cfg80211_get_station(struct wiphy *wiphy,
           struct net_device *dev, u8 *mac, struct station_info *sinfo);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(7, 1, 0)
static s32 wl_cfg80211_get_station(struct wiphy *wiphy,
           struct net_device *dev, const u8 *mac, struct station_info *sinfo);
#else
static s32 wl_cfg80211_get_station(struct wiphy *wiphy,
           struct wireless_dev *wdev, const u8 *mac, struct station_info *sinfo);
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(7, 1, 0)
static s32 wl_cfg80211_dump_station(struct wiphy *wiphy,
           struct net_device *dev, int idx, u8 *mac, struct station_info *sinfo);
#else
static s32 wl_cfg80211_dump_station(struct wiphy *wiphy,
           struct wireless_dev *wdev, int idx, u8 *mac, struct station_info *sinfo);
#endif

static s32 wl_cfg80211_set_power_mgmt(struct wiphy *wiphy,
           struct net_device *dev, bool enabled, s32 timeout);
static int wl_cfg80211_connect(struct wiphy *wiphy, struct net_device *dev,
           struct cfg80211_connect_params *sme);
static s32 wl_cfg80211_disconnect(struct wiphy *wiphy, struct net_device *dev, u16 reason_code);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 17, 0)
static s32 wl_cfg80211_set_tx_power(struct wiphy *wiphy, struct wireless_dev *wdev,
           s32 radio_idx, enum nl80211_tx_power_setting type, s32 dbm);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
static s32
wl_cfg80211_set_tx_power(struct wiphy *wiphy, struct wireless_dev *wdev,
                         enum nl80211_tx_power_setting type, s32 dbm);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
static s32 wl_cfg80211_set_tx_power(struct wiphy *wiphy,
           enum nl80211_tx_power_setting type, s32 dbm);
#else
static s32 wl_cfg80211_set_tx_power(struct wiphy *wiphy,
           enum tx_power_setting type, s32 dbm);
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 17, 0)
static s32 wl_cfg80211_get_tx_power(struct wiphy *wiphy, struct wireless_dev *wdev, s32 radio_idx,
           u32 /*link_id*/, s32 *dbm);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 14, 0)
static s32 wl_cfg80211_get_tx_power(struct wiphy *wiphy, struct wireless_dev *wdev, u32 /*link_id*/, s32 *dbm);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
static s32 wl_cfg80211_get_tx_power(struct wiphy *wiphy, struct wireless_dev *wdev, s32 *dbm);
#else
static s32 wl_cfg80211_get_tx_power(struct wiphy *wiphy, s32 *dbm);
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
static s32 wl_cfg80211_config_default_key(struct wiphy *wiphy,
           struct net_device *dev, int link_id, u8 key_idx, bool unicast,
           bool multicast);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38)
static s32 wl_cfg80211_config_default_key(struct wiphy *wiphy,
           struct net_device *dev, u8 key_idx, bool unicast, bool multicast);
#else
static s32 wl_cfg80211_config_default_key(struct wiphy *wiphy,
           struct net_device *dev, u8 key_idx);
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 1, 0)
static s32 wl_cfg80211_add_key(struct wiphy *wiphy, struct wireless_dev *wdev,
           int link_id, u8 key_idx, bool pairwise, const u8 *mac_addr,
           struct key_params *params);
static s32 wl_cfg80211_del_key(struct wiphy *wiphy, struct wireless_dev *wdev,
           int link_id, u8 key_idx, bool pairwise, const u8 *mac_addr);
static s32 wl_cfg80211_get_key(struct wiphy *wiphy, struct wireless_dev *wdev,
           int link_id, u8 key_idx, bool pairwise, const u8 *mac_addr,
           void *cookie,
           void (*callback) (void *cookie, struct key_params *params));
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
static s32 wl_cfg80211_add_key(struct wiphy *wiphy, struct net_device *dev,
           int link_id, u8 key_idx, bool pairwise, const u8 *mac_addr,
           struct key_params *params);
static s32 wl_cfg80211_del_key(struct wiphy *wiphy, struct net_device *dev,
           int link_id, u8 key_idx, bool pairwise, const u8 *mac_addr);
static s32 wl_cfg80211_get_key(struct wiphy *wiphy, struct net_device *dev,
           int link_id, u8 key_idx, bool pairwise, const u8 *mac_addr,
           void *cookie,
           void (*callback) (void *cookie, struct key_params *params));
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)
static s32 wl_cfg80211_add_key(struct wiphy *wiphy, struct net_device *dev,
           u8 key_idx, bool pairwise, const u8 *mac_addr, struct key_params *params);
static s32 wl_cfg80211_del_key(struct wiphy *wiphy, struct net_device *dev,
           u8 key_idx, bool pairwise, const u8 *mac_addr);
static s32 wl_cfg80211_get_key(struct wiphy *wiphy, struct net_device *dev,
           u8 key_idx, bool pairwise, const u8 *mac_addr,
           void *cookie, void (*callback) (void *cookie, struct key_params *params));
#else
static s32 wl_cfg80211_add_key(struct wiphy *wiphy, struct net_device *dev,
           u8 key_idx, const u8 *mac_addr, struct key_params *params);
static s32 wl_cfg80211_del_key(struct wiphy *wiphy, struct net_device *dev,
           u8 key_idx, const u8 *mac_addr);
static s32 wl_cfg80211_get_key(struct wiphy *wiphy, struct net_device *dev,
           u8 key_idx, const u8 *mac_addr,
           void *cookie, void (*callback) (void *cookie, struct key_params *params));
#endif 

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 33)
static s32 wl_cfg80211_set_pmksa(struct wiphy *wiphy, struct net_device *dev,
           struct cfg80211_pmksa *pmksa);
static s32 wl_cfg80211_del_pmksa(struct wiphy *wiphy, struct net_device *dev,
           struct cfg80211_pmksa *pmksa);
static s32 wl_cfg80211_flush_pmksa(struct wiphy *wiphy, struct net_device *dev);
#endif

static s32 wl_create_event_handler(struct wl_cfg80211_priv *wl);
static void wl_destroy_event_handler(struct wl_cfg80211_priv *wl);
static s32 wl_event_handler(void *data);
static void wl_init_eq(struct wl_cfg80211_priv *wl);
static void wl_flush_eq(struct wl_cfg80211_priv *wl);
static void wl_lock_eq(struct wl_cfg80211_priv *wl);
static void wl_unlock_eq(struct wl_cfg80211_priv *wl);
static void wl_init_eq_lock(struct wl_cfg80211_priv *wl);
static void wl_init_eloop_handler(struct wl_cfg80211_event_loop *el);
static struct wl_cfg80211_event_q *wl_deq_event(struct wl_cfg80211_priv *wl);
static s32 wl_enq_event(struct wl_cfg80211_priv *wl, u32 type,
	const wl_event_msg_t *msg, void *data);
static void wl_put_event(struct wl_cfg80211_event_q *e);
static void wl_wakeup_event(struct wl_cfg80211_priv *wl);

static s32 wl_notify_connect_status(struct wl_cfg80211_priv *wl, struct net_device *ndev,
           const wl_event_msg_t *e, void *data);
static s32 wl_notify_roaming_status(struct wl_cfg80211_priv *wl, struct net_device *ndev,
           const wl_event_msg_t *e, void *data);
static s32 wl_notify_scan_status(struct wl_cfg80211_priv *wl, struct net_device *ndev,
           const wl_event_msg_t *e, void *data);
static s32 wl_bss_connect_done(struct wl_cfg80211_priv *wl, struct net_device *ndev,
           const wl_event_msg_t *e, void *data, bool completed);
static s32 wl_bss_roaming_done(struct wl_cfg80211_priv *wl, struct net_device *ndev,
           const wl_event_msg_t *e, void *data);
static s32 wl_notify_mic_status(struct wl_cfg80211_priv *wl, struct net_device *ndev,
           const wl_event_msg_t *e, void *data);

static s32 wl_dev_bufvar_get(struct net_device *dev, s8 *name, s8 *buf, s32 buf_len);
static __used s32 wl_dev_bufvar_set(struct net_device *dev, s8 *name, s8 *buf, s32 len);
static s32 wl_dev_intvar_set(struct net_device *dev, s8 *name, s32 val);
static s32 wl_dev_intvar_get(struct net_device *dev, s8 *name, s32 *retval);
static s32 wl_dev_ioctl(struct net_device *dev, u32 cmd, void *arg, u32 len);
static s32 wl_dev_ioctl_bcmerr(struct net_device *dev, u32 cmd, void *arg, u32 len,
           int *bcmerr);

static s32 wl_set_frag(struct net_device *dev, u32 frag_threshold);
static s32 wl_set_rts(struct net_device *dev, u32 frag_threshold);
static s32 wl_set_retry(struct net_device *dev, u32 retry, bool l);

static void wl_init_prof(struct wl_cfg80211_profile *prof);

static s32 wl_set_wpa_version(struct net_device *dev, struct cfg80211_connect_params *sme);
static s32 wl_set_auth_type(struct net_device *dev, struct cfg80211_connect_params *sme);
static s32 wl_set_set_cipher(struct net_device *dev, struct cfg80211_connect_params *sme);
static s32 wl_set_key_mgmt(struct net_device *dev, struct cfg80211_connect_params *sme);
static s32 wl_set_set_sharedkey(struct net_device *dev, struct cfg80211_connect_params *sme);
static s32 wl_get_assoc_ies(struct wl_cfg80211_priv *wl);
static void wl_ch_to_chanspec(struct ieee80211_channel *chan,
            struct wl_join_params *join_params, size_t *join_params_size);

static __used s32 wl_add_ie(struct wl_cfg80211_priv *wl, u8 t, u8 l, u8 *v);

static s32 wl_mode_to_nl80211_iftype(s32 mode);

static s32 wl_alloc_wdev(struct device *dev, struct wireless_dev **rwdev);
static void wl_free_wdev(struct wl_cfg80211_priv *wl);

static s32 wl_inform_bss(struct wl_cfg80211_priv *wl, struct wl_scan_results *bss_list,
           u32 buflen);
static s32 wl_inform_single_bss(struct wl_cfg80211_priv *wl, struct wl_bss_info *bi,
           struct cfg80211_bss **bss_out);
static s32 wl_update_bss_info(struct wl_cfg80211_priv *wl, struct cfg80211_bss **bss_out);

static void key_endian_to_device(struct wl_wsec_key *key);
static void key_endian_to_host(struct wl_wsec_key *key);

static s32 wl_init_priv_mem(struct wl_cfg80211_priv *wl);
static void wl_deinit_priv_mem(struct wl_cfg80211_priv *wl);

static bool wl_is_ibssmode(struct wl_cfg80211_priv *wl);

static void wl_link_up(struct wl_cfg80211_priv *wl);
static void wl_link_down(struct wl_cfg80211_priv *wl);
static s32 wl_set_mode(struct net_device *ndev, s32 iftype);

static void wl_init_conf(struct wl_cfg80211_conf *conf);

static s32 wl_update_wiphybands(struct wl_cfg80211_priv *wl);

static __used s32 wl_update_pmklist(struct net_device *dev,
                  struct wl_cfg80211_pmk_list *pmk_list, s32 err);

#if defined(WL_DBGMSG_ENABLE)
#define WL_DBG_ESTR_MAX	32
static s8 wl_dbg_estr[][WL_DBG_ESTR_MAX] = {
	"SET_SSID", "JOIN", "START", "AUTH", "AUTH_IND",
	"DEAUTH", "DEAUTH_IND", "ASSOC", "ASSOC_IND", "REASSOC",
	"REASSOC_IND", "DISASSOC", "DISASSOC_IND", "QUIET_START", "QUIET_END",
	"BEACON_RX", "LINK", "MIC_ERROR", "NDIS_LINK", "ROAM",
	"TXFAIL", "PMKID_CACHE", "RETROGRADE_TSF", "PRUNE", "AUTOAUTH",
	"EAPOL_MSG", "SCAN_COMPLETE", "ADDTS_IND", "DELTS_IND", "BCNSENT_IND",
	"BCNRX_MSG", "BCNLOST_MSG", "ROAM_PREP", "PFN_NET_FOUND",
	"PFN_NET_LOST",
	"RESET_COMPLETE", "JOIN_START", "ROAM_START", "ASSOC_START",
	"IBSS_ASSOC",
	"RADIO", "PSM_WATCHDOG",
	"PROBREQ_MSG",
	"SCAN_CONFIRM_IND", "PSK_SUP", "COUNTRY_CODE_CHANGED",
	"EXCEEDED_MEDIUM_TIME", "ICV_ERROR",
	"UNICAST_DECODE_ERROR", "MULTICAST_DECODE_ERROR", "TRACE",
	"IF",
	"RSSI", "PFN_SCAN_COMPLETE", "ACTION_FRAME", "ACTION_FRAME_COMPLETE",
};
#endif				

#define CHAN2G(_channel, _freq, _flags) {			\
	.band			= NL80211_BAND_2GHZ,		\
	.center_freq		= (_freq),			\
	.hw_value		= (_channel),			\
	.flags			= (_flags),			\
	.max_antenna_gain	= 0,				\
	.max_power		= 30,				\
}

#define CHAN5G(_channel, _flags) {				\
	.band			= NL80211_BAND_5GHZ,		\
	.center_freq		= 5000 + (5 * (_channel)),	\
	.hw_value		= (_channel),			\
	.flags			= (_flags),			\
	.max_antenna_gain	= 0,				\
	.max_power		= 30,				\
}

#define RATE_TO_BASE100KBPS(rate)   (((rate) * 10) / 2)
#define RATETAB_ENT(_rateid, _flags) \
	{                                                               \
		.bitrate        = RATE_TO_BASE100KBPS(_rateid),     \
		.hw_value       = (_rateid),                            \
		.flags          = (_flags),                             \
	}

static struct ieee80211_rate __wl_rates[] = {
	RATETAB_ENT(DOT11_RATE_1M, 0),
	RATETAB_ENT(DOT11_RATE_2M, IEEE80211_RATE_SHORT_PREAMBLE),
	RATETAB_ENT(DOT11_RATE_5M5, IEEE80211_RATE_SHORT_PREAMBLE),
	RATETAB_ENT(DOT11_RATE_11M, IEEE80211_RATE_SHORT_PREAMBLE),
	RATETAB_ENT(DOT11_RATE_6M, 0),
	RATETAB_ENT(DOT11_RATE_9M, 0),
	RATETAB_ENT(DOT11_RATE_12M, 0),
	RATETAB_ENT(DOT11_RATE_18M, 0),
	RATETAB_ENT(DOT11_RATE_24M, 0),
	RATETAB_ENT(DOT11_RATE_36M, 0),
	RATETAB_ENT(DOT11_RATE_48M, 0),
	RATETAB_ENT(DOT11_RATE_54M, 0),
};

#define wl_a_rates		(__wl_rates + 4)
#define wl_a_rates_size	8
#define wl_g_rates		(__wl_rates + 0)
#define wl_g_rates_size	12

static struct ieee80211_channel __wl_2ghz_channels[] = {
	CHAN2G(1, 2412, 0),
	CHAN2G(2, 2417, 0),
	CHAN2G(3, 2422, 0),
	CHAN2G(4, 2427, 0),
	CHAN2G(5, 2432, 0),
	CHAN2G(6, 2437, 0),
	CHAN2G(7, 2442, 0),
	CHAN2G(8, 2447, 0),
	CHAN2G(9, 2452, 0),
	CHAN2G(10, 2457, 0),
	CHAN2G(11, 2462, 0),
	CHAN2G(12, 2467, 0),
	CHAN2G(13, 2472, 0),
	CHAN2G(14, 2484, 0),
};

static struct ieee80211_channel __wl_5ghz_a_channels[] = {
	CHAN5G(34, 0), CHAN5G(36, 0),
	CHAN5G(38, 0), CHAN5G(40, 0),
	CHAN5G(42, 0), CHAN5G(44, 0),
	CHAN5G(46, 0), CHAN5G(48, 0),
	CHAN5G(52, 0), CHAN5G(56, 0),
	CHAN5G(60, 0), CHAN5G(64, 0),
	CHAN5G(100, 0), CHAN5G(104, 0),
	CHAN5G(108, 0), CHAN5G(112, 0),
	CHAN5G(116, 0), CHAN5G(120, 0),
	CHAN5G(124, 0), CHAN5G(128, 0),
	CHAN5G(132, 0), CHAN5G(136, 0),
	CHAN5G(140, 0), CHAN5G(149, 0),
	CHAN5G(153, 0), CHAN5G(157, 0),
	CHAN5G(161, 0), CHAN5G(165, 0),
	CHAN5G(184, 0), CHAN5G(188, 0),
	CHAN5G(192, 0), CHAN5G(196, 0),
	CHAN5G(200, 0), CHAN5G(204, 0),
	CHAN5G(208, 0), CHAN5G(212, 0),
	CHAN5G(216, 0),
};

static struct ieee80211_channel __wl_5ghz_n_channels[] = {
	CHAN5G(32, 0), CHAN5G(34, 0),
	CHAN5G(36, 0), CHAN5G(38, 0),
	CHAN5G(40, 0), CHAN5G(42, 0),
	CHAN5G(44, 0), CHAN5G(46, 0),
	CHAN5G(48, 0), CHAN5G(50, 0),
	CHAN5G(52, 0), CHAN5G(54, 0),
	CHAN5G(56, 0), CHAN5G(58, 0),
	CHAN5G(60, 0), CHAN5G(62, 0),
	CHAN5G(64, 0), CHAN5G(66, 0),
	CHAN5G(68, 0), CHAN5G(70, 0),
	CHAN5G(72, 0), CHAN5G(74, 0),
	CHAN5G(76, 0), CHAN5G(78, 0),
	CHAN5G(80, 0), CHAN5G(82, 0),
	CHAN5G(84, 0), CHAN5G(86, 0),
	CHAN5G(88, 0), CHAN5G(90, 0),
	CHAN5G(92, 0), CHAN5G(94, 0),
	CHAN5G(96, 0), CHAN5G(98, 0),
	CHAN5G(100, 0), CHAN5G(102, 0),
	CHAN5G(104, 0), CHAN5G(106, 0),
	CHAN5G(108, 0), CHAN5G(110, 0),
	CHAN5G(112, 0), CHAN5G(114, 0),
	CHAN5G(116, 0), CHAN5G(118, 0),
	CHAN5G(120, 0), CHAN5G(122, 0),
	CHAN5G(124, 0), CHAN5G(126, 0),
	CHAN5G(128, 0), CHAN5G(130, 0),
	CHAN5G(132, 0), CHAN5G(134, 0),
	CHAN5G(136, 0), CHAN5G(138, 0),
	CHAN5G(140, 0), CHAN5G(142, 0),
	CHAN5G(144, 0), CHAN5G(145, 0),
	CHAN5G(146, 0), CHAN5G(147, 0),
	CHAN5G(148, 0), CHAN5G(149, 0),
	CHAN5G(150, 0), CHAN5G(151, 0),
	CHAN5G(152, 0), CHAN5G(153, 0),
	CHAN5G(154, 0), CHAN5G(155, 0),
	CHAN5G(156, 0), CHAN5G(157, 0),
	CHAN5G(158, 0), CHAN5G(159, 0),
	CHAN5G(160, 0), CHAN5G(161, 0),
	CHAN5G(162, 0), CHAN5G(163, 0),
	CHAN5G(164, 0), CHAN5G(165, 0),
	CHAN5G(166, 0), CHAN5G(168, 0),
	CHAN5G(170, 0), CHAN5G(172, 0),
	CHAN5G(174, 0), CHAN5G(176, 0),
	CHAN5G(178, 0), CHAN5G(180, 0),
	CHAN5G(182, 0), CHAN5G(184, 0),
	CHAN5G(186, 0), CHAN5G(188, 0),
	CHAN5G(190, 0), CHAN5G(192, 0),
	CHAN5G(194, 0), CHAN5G(196, 0),
	CHAN5G(198, 0), CHAN5G(200, 0),
	CHAN5G(202, 0), CHAN5G(204, 0),
	CHAN5G(206, 0), CHAN5G(208, 0),
	CHAN5G(210, 0), CHAN5G(212, 0),
	CHAN5G(214, 0), CHAN5G(216, 0),
	CHAN5G(218, 0), CHAN5G(220, 0),
	CHAN5G(222, 0), CHAN5G(224, 0),
	CHAN5G(226, 0), CHAN5G(228, 0),
};

static struct ieee80211_supported_band __wl_band_2ghz = {
	.band = NL80211_BAND_2GHZ,
	.channels = __wl_2ghz_channels,
	.n_channels = ARRAY_SIZE(__wl_2ghz_channels),
	.bitrates = wl_g_rates,
	.n_bitrates = wl_g_rates_size,
};

static struct ieee80211_supported_band __wl_band_5ghz_a = {
	.band = NL80211_BAND_5GHZ,
	.channels = __wl_5ghz_a_channels,
	.n_channels = ARRAY_SIZE(__wl_5ghz_a_channels),
	.bitrates = wl_a_rates,
	.n_bitrates = wl_a_rates_size,
};

static struct ieee80211_supported_band __wl_band_5ghz_n = {
	.band = NL80211_BAND_5GHZ,
	.channels = __wl_5ghz_n_channels,
	.n_channels = ARRAY_SIZE(__wl_5ghz_n_channels),
	.bitrates = wl_a_rates,
	.n_bitrates = wl_a_rates_size,
};

static const u32 __wl_cipher_suites[] = {
	WLAN_CIPHER_SUITE_WEP40,
	WLAN_CIPHER_SUITE_WEP104,
	WLAN_CIPHER_SUITE_TKIP,
	WLAN_CIPHER_SUITE_CCMP,
	WLAN_CIPHER_SUITE_AES_CMAC,
};

static void key_endian_to_device(struct wl_wsec_key *key)
{
	key->index = htod32(key->index);
	key->len = htod32(key->len);
	key->algo = htod32(key->algo);
	key->flags = htod32(key->flags);
	key->rxiv.hi = htod32(key->rxiv.hi);
	key->rxiv.lo = htod16(key->rxiv.lo);
	key->iv_initialized = htod32(key->iv_initialized);
}

static void key_endian_to_host(struct wl_wsec_key *key)
{
	key->index = dtoh32(key->index);
	key->len = dtoh32(key->len);
	key->algo = dtoh32(key->algo);
	key->flags = dtoh32(key->flags);
	key->rxiv.hi = dtoh32(key->rxiv.hi);
	key->rxiv.lo = dtoh16(key->rxiv.lo);
	key->iv_initialized = dtoh32(key->iv_initialized);
}

/*
 * Every cfg80211 op reaches the firmware through here. nl80211 has already
 * applied its permission checks for the operation (read-only queries such as
 * station or tx-power information are open to unprivileged users, anything
 * that changes state needs CAP_NET_ADMIN), so unlike the private
 * SIOCDEVPRIVATE ioctl this path must not demand CAP_NET_ADMIN again.
 * @bcmerr, when non-NULL, receives the raw Broadcom status (BCME_*).
 */
static s32
wl_dev_ioctl_bcmerr(struct net_device *dev, u32 cmd, void *arg, u32 len, int *bcmerr)
{
	return wlc_ioctl_kernel(dev, cmd, arg, len, bcmerr);
}

static s32
wl_dev_ioctl(struct net_device *dev, u32 cmd, void *arg, u32 len)
{
	return wl_dev_ioctl_bcmerr(dev, cmd, arg, len, NULL);
}

/* Firmware refusals that only mean "not now", e.g. while joining a network. */
static bool wl_bcmerr_is_transient(int bcmerr)
{
	return bcmerr == BCME_BUSY || bcmerr == BCME_NOTREADY ||
	       bcmerr == BCME_SCANREJECT || bcmerr == BCME_NOTUP;
}

static s32
wl_cfg80211_change_iface(struct wiphy *wiphy, struct net_device *ndev,
                         enum nl80211_iftype type,
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0)
                         u32 *flags,
#endif
                         struct vif_params *params)
{
	struct wl_cfg80211_priv *wl = wiphy_to_wl(wiphy);
	struct wireless_dev *wdev;
	s32 infra = 0;
	s32 ap = 0;
	s32 err = 0;

	switch (type) {
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_WDS:
		WL_ERR(("type (%d) : currently we do not support this type\n",
			type));
		return -EOPNOTSUPP;
	case NL80211_IFTYPE_ADHOC:
		wl->conf->mode = WL_MODE_IBSS;
		break;
	case NL80211_IFTYPE_STATION:
		wl->conf->mode = WL_MODE_BSS;
		infra = 1;
		break;
	default:
		return -EINVAL;
	}
	infra = htod32(infra);
	ap = htod32(ap);
	wdev = ndev->ieee80211_ptr;
	wdev->iftype = type;
	WL_DBG(("%s : ap (%d), infra (%d)\n", ndev->name, ap, infra));
	err = wl_dev_ioctl(ndev, WLC_SET_INFRA, &infra, sizeof(infra));
	if (err) {
		WL_ERR(("WLC_SET_INFRA error (%d)\n", err));
		return err;
	}
	err = wl_dev_ioctl(ndev, WLC_SET_AP, &ap, sizeof(ap));
	if (err) {
		WL_ERR(("WLC_SET_AP error (%d)\n", err));
		return err;
	}

	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
static s32
wl_cfg80211_scan(struct wiphy *wiphy,
                 struct cfg80211_scan_request *request)
#else
static s32
wl_cfg80211_scan(struct wiphy *wiphy,
                 struct net_device *ndev,
                 struct cfg80211_scan_request *request)
#endif

{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
	struct net_device *ndev = request->wdev->netdev;
#endif
	struct wl_cfg80211_priv *wl = ndev_to_wl(ndev);
	struct cfg80211_ssid *ssids;
	struct wl_cfg80211_scan_req *sr = wl_to_sr(wl);
	s32 passive_scan;
	int bcmerr = 0;
	s32 err = 0;

	if (request) {		
		ssids = request->ssids;
	}
	else {		

		ssids = NULL;
	}
	wl->scan_request = request;

	memset(&sr->ssid, 0, sizeof(sr->ssid));

	if (ssids) {
		WL_DBG(("ssid \"%s\", ssid_len (%d)\n", ssids->ssid, ssids->ssid_len));
		sr->ssid.SSID_len = min_t(u8, sizeof(sr->ssid.SSID), ssids->ssid_len);
	}

	if (sr->ssid.SSID_len) {
		memcpy(sr->ssid.SSID, ssids->ssid, sr->ssid.SSID_len);
		sr->ssid.SSID_len = htod32(sr->ssid.SSID_len);
		WL_DBG(("Specific scan ssid=\"%s\" len=%d\n", sr->ssid.SSID, sr->ssid.SSID_len));
	} else {
		WL_DBG(("Broadcast scan\n"));
	}
	WL_DBG(("sr->ssid.SSID_len (%d)\n", sr->ssid.SSID_len));
	passive_scan = wl->active_scan ? 0 : 1;
	err = wl_dev_ioctl(ndev, WLC_SET_PASSIVE_SCAN, &passive_scan, sizeof(passive_scan));
	if (err) {
		WL_ERR(("WLC_SET_PASSIVE_SCAN error (%d)\n", err));
		goto scan_out;
	}
	err = wl_dev_ioctl_bcmerr(ndev, WLC_SCAN, &sr->ssid, sizeof(sr->ssid), &bcmerr);
	if (err) {
		/*
		 * The firmware turns scans down while it is joining or roaming.
		 * Its status codes all collapse to -EINVAL on the way out, which
		 * cfg80211 and NetworkManager treat as a hard failure; -EBUSY
		 * tells them to simply try again later.
		 */
		if (err == -EBUSY || wl_bcmerr_is_transient(bcmerr)) {
			WL_INF(("firmware busy (bcmerror %d): scan for \"%s\" deferred\n",
				bcmerr, sr->ssid.SSID));
			err = -EBUSY;
		} else {
			WL_ERR(("WLC_SCAN error (%d, bcmerror %d)\n", err, bcmerr));
		}
		goto scan_out;
	}

	return 0;

scan_out:
	wl->scan_request = NULL;
	return err;
}

static s32 wl_dev_intvar_set(struct net_device *dev, s8 *name, s32 val)
{
	s8 buf[WLC_IOCTL_SMLEN];
	u32 len;
	s32 err = 0;

	val = htod32(val);
	len = bcm_mkiovar(name, (char *)(&val), sizeof(val), buf, sizeof(buf));
	BUG_ON(!len);

	err = wl_dev_ioctl(dev, WLC_SET_VAR, buf, len);
	if (err) {
		WL_ERR(("error (%d)\n", err));
	}

	return err;
}

static s32
wl_dev_intvar_get(struct net_device *dev, s8 *name, s32 *retval)
{
	union {
		s8 buf[WLC_IOCTL_SMLEN];
		s32 val;
	} var;
	u32 len;
	u32 data_null;
	s32 err = 0;

	len = bcm_mkiovar(name, (char *)(&data_null), 0, (char *)(&var), sizeof(var.buf));
	BUG_ON(!len);
	err = wl_dev_ioctl(dev, WLC_GET_VAR, &var, len);
	if (err) {
		WL_ERR(("error (%d)\n", err));
	}
	*retval = dtoh32(var.val);

	return err;
}

static s32 wl_set_rts(struct net_device *dev, u32 rts_threshold)
{
	s32 err = 0;

	err = wl_dev_intvar_set(dev, "rtsthresh", rts_threshold);
	if (err) {
		WL_ERR(("Error (%d)\n", err));
		return err;
	}
	return err;
}

static s32 wl_set_frag(struct net_device *dev, u32 frag_threshold)
{
	s32 err = 0;

	err = wl_dev_intvar_set(dev, "fragthresh", frag_threshold);
	if (err) {
		WL_ERR(("Error (%d)\n", err));
		return err;
	}
	return err;
}

static s32 wl_set_retry(struct net_device *dev, u32 retry, bool l)
{
	s32 err = 0;
	u32 cmd = (l ? WLC_SET_LRL : WLC_SET_SRL);

	retry = htod32(retry);
	err = wl_dev_ioctl(dev, cmd, &retry, sizeof(retry));
	if (err) {
		WL_ERR(("cmd (%d) , error (%d)\n", cmd, err));
		return err;
	}
	return err;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 17, 0)
static s32 wl_cfg80211_set_wiphy_params(struct wiphy *wiphy, s32 radio_idx, u32 changed)
#else
static s32 wl_cfg80211_set_wiphy_params(struct wiphy *wiphy, u32 changed)
#endif
{
	struct wl_cfg80211_priv *wl = wiphy_to_wl(wiphy);
	struct net_device *ndev = wl_to_ndev(wl);
	s32 err = 0;

	if (changed & WIPHY_PARAM_RTS_THRESHOLD &&
	    (wl->conf->rts_threshold != wiphy->rts_threshold)) {
		wl->conf->rts_threshold = wiphy->rts_threshold;
		err = wl_set_rts(ndev, wl->conf->rts_threshold);
		if (!err)
			return err;
	}
	if (changed & WIPHY_PARAM_FRAG_THRESHOLD &&
	    (wl->conf->frag_threshold != wiphy->frag_threshold)) {
		wl->conf->frag_threshold = wiphy->frag_threshold;
		err = wl_set_frag(ndev, wl->conf->frag_threshold);
		if (!err)
			return err;
	}
	if (changed & WIPHY_PARAM_RETRY_LONG && (wl->conf->retry_long != wiphy->retry_long)) {
		wl->conf->retry_long = wiphy->retry_long;
		err = wl_set_retry(ndev, wl->conf->retry_long, true);
		if (!err)
			return err;
	}
	if (changed & WIPHY_PARAM_RETRY_SHORT && (wl->conf->retry_short != wiphy->retry_short)) {
		wl->conf->retry_short = wiphy->retry_short;
		err = wl_set_retry(ndev, wl->conf->retry_short, false);
		if (!err) {
			return err;
		}
	}

	return err;
}

static s32
wl_cfg80211_join_ibss(struct wiphy *wiphy, struct net_device *dev,
                      struct cfg80211_ibss_params *params)
{
	struct wl_join_params join_params;
	size_t join_params_size;
	s32 val;
	s32 err = 0;

	WL_DBG(("\n"));

	if (params->bssid) {
		WL_ERR(("Invalid bssid\n"));
		return -EOPNOTSUPP;
	}

	if ((err = wl_dev_intvar_set(dev, "auth", 0))) {
		return err;
	}
	if ((err = wl_dev_intvar_set(dev, "wpa_auth", WPA_AUTH_NONE))) {
		return err;
	}
	if ((err = wl_dev_intvar_get(dev, "wsec", &val))) {
		return err;
	}
	val &= ~(WEP_ENABLED | TKIP_ENABLED | AES_ENABLED);
	if ((err = wl_dev_intvar_set(dev, "wsec", val))) {
		return err;
	}

	memset(&join_params, 0, sizeof(join_params));
	join_params_size = sizeof(join_params.ssid);

	memcpy((void *)join_params.ssid.SSID, (void *)params->ssid, params->ssid_len);
	join_params.ssid.SSID_len = htod32(params->ssid_len);
	if (params->bssid)
		memcpy(&join_params.params.bssid, params->bssid, ETHER_ADDR_LEN);
	else
		memset(&join_params.params.bssid, 0, ETHER_ADDR_LEN);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
	wl_ch_to_chanspec(params->chandef.chan, &join_params, &join_params_size);
#else
	wl_ch_to_chanspec(params->channel, &join_params, &join_params_size);
#endif
	err = wl_dev_ioctl(dev, WLC_SET_SSID, &join_params, join_params_size);
	if (err) {
		WL_ERR(("Error (%d)\n", err));
		return err;
	}
	return err;
}

static s32 wl_cfg80211_leave_ibss(struct wiphy *wiphy, struct net_device *dev)
{
	struct wl_cfg80211_priv *wl = wiphy_to_wl(wiphy);
	s32 err = 0;

	WL_DBG(("\n"));

	wl_link_down(wl);

	return err;
}

static s32
wl_set_wpa_version(struct net_device *dev, struct cfg80211_connect_params *sme)
{
	struct wl_cfg80211_priv *wl = ndev_to_wl(dev);
	s32 val = 0;
	s32 err = 0;

	if (sme->crypto.wpa_versions & NL80211_WPA_VERSION_1)
		val = WPA_AUTH_PSK | WPA_AUTH_UNSPECIFIED;
	else if (sme->crypto.wpa_versions & NL80211_WPA_VERSION_2)
		val = WPA2_AUTH_PSK | WPA2_AUTH_UNSPECIFIED;
	else
		val = WPA_AUTH_DISABLED;
	WL_DBG(("setting wpa_auth to 0x%0x\n", val));
	err = wl_dev_intvar_set(dev, "wpa_auth", val);
	if (err) {
		WL_ERR(("set wpa_auth failed (%d)\n", err));
		return err;
	}
	wl->profile->sec.wpa_versions = sme->crypto.wpa_versions;
	return err;
}

static s32
wl_set_auth_type(struct net_device *dev, struct cfg80211_connect_params *sme)
{
	struct wl_cfg80211_priv *wl = ndev_to_wl(dev);
	s32 val = 0;
	s32 err = 0;

	switch (sme->auth_type) {
	case NL80211_AUTHTYPE_OPEN_SYSTEM:
		val = 0;
		WL_DBG(("open system\n"));
		break;
	case NL80211_AUTHTYPE_SHARED_KEY:
		val = 1;
		WL_DBG(("shared key\n"));
		break;
	case NL80211_AUTHTYPE_AUTOMATIC:
		val = 2;
		WL_DBG(("automatic\n"));
		break;
	case NL80211_AUTHTYPE_NETWORK_EAP:
		WL_DBG(("network eap\n"));
	default:
		val = 2;
		WL_ERR(("invalid auth type (%d)\n", sme->auth_type));
		break;
	}

	err = wl_dev_intvar_set(dev, "auth", val);
	if (err) {
		WL_ERR(("set auth failed (%d)\n", err));
		return err;
	}

	wl->profile->sec.auth_type = sme->auth_type;
	return err;
}

static s32
wl_set_set_cipher(struct net_device *dev, struct cfg80211_connect_params *sme)
{
	struct wl_cfg80211_priv *wl = ndev_to_wl(dev);
	s32 pval = 0;
	s32 gval = 0;
	s32 val = 0;
	s32 err = 0;

	if (sme->crypto.n_ciphers_pairwise) {
		switch (sme->crypto.ciphers_pairwise[0]) {
		case WLAN_CIPHER_SUITE_WEP40:
		case WLAN_CIPHER_SUITE_WEP104:
			pval = WEP_ENABLED;
			break;
		case WLAN_CIPHER_SUITE_TKIP:
			pval = TKIP_ENABLED;
			break;
		case WLAN_CIPHER_SUITE_CCMP:
			pval = AES_ENABLED;
			break;
		case WLAN_CIPHER_SUITE_AES_CMAC:
			pval = AES_ENABLED;
			break;
		default:
			WL_ERR(("invalid cipher pairwise (%d)\n", sme->crypto.ciphers_pairwise[0]));
			return -EINVAL;
		}
	}
	if (sme->crypto.cipher_group) {
		switch (sme->crypto.cipher_group) {
		case WLAN_CIPHER_SUITE_WEP40:
		case WLAN_CIPHER_SUITE_WEP104:
			gval = WEP_ENABLED;
			break;
		case WLAN_CIPHER_SUITE_TKIP:
			gval = TKIP_ENABLED;
			break;
		case WLAN_CIPHER_SUITE_CCMP:
			gval = AES_ENABLED;
			break;
		case WLAN_CIPHER_SUITE_AES_CMAC:
			gval = AES_ENABLED;
			break;
		default:
			WL_ERR(("invalid cipher group (%d)\n", sme->crypto.cipher_group));
			return -EINVAL;
		}
	}

	if ((err = wl_dev_intvar_get(dev, "wsec", &val))) {
		return err;
	}
	val &= ~(WEP_ENABLED | TKIP_ENABLED | AES_ENABLED);
	val |= pval | gval;
	WL_DBG(("set wsec to %d\n", val));
	err = wl_dev_intvar_set(dev, "wsec", val);
	if (err) {
		WL_ERR(("error (%d)\n", err));
		return err;
	}

	wl->profile->sec.cipher_pairwise = sme->crypto.ciphers_pairwise[0];
	wl->profile->sec.cipher_group = sme->crypto.cipher_group;

	return err;
}

static s32
wl_set_key_mgmt(struct net_device *dev, struct cfg80211_connect_params *sme)
{
	struct wl_cfg80211_priv *wl = ndev_to_wl(dev);
	u32 akm = sme->crypto.akm_suites[0];
	s32 val = 0;
	s32 err = 0;

	if (sme->crypto.n_akm_suites) {
		err = wl_dev_intvar_get(dev, "wpa_auth", &val);
		if (err) {
			WL_ERR(("could not get wpa_auth (%d)\n", err));
			return err;
		}
		if (val & (WPA_AUTH_PSK | WPA_AUTH_UNSPECIFIED)) {
			switch (akm) {
			case WLAN_AKM_SUITE_8021X:
				val = WPA_AUTH_UNSPECIFIED;
				break;
			case WLAN_AKM_SUITE_PSK:
				val = WPA_AUTH_PSK;
				break;
			default:
				WL_ERR(("unsupported AKM suite 0x%08x\n", akm));
				return -EINVAL;
			}
		} else if (val & (WPA2_AUTH_PSK | WPA2_AUTH_UNSPECIFIED)) {
			switch (akm) {
			/*
			 * Transition-mode networks (WPA2/WPA3-Enterprise, e.g.
			 * eduroam) offer the SHA-256 AKMs next to the classic
			 * ones, and wpa_supplicant picks SHA-256 whenever its
			 * profile allows it, which NetworkManager always does.
			 * This core only does the classic AKMs, and rejecting the
			 * request meant such networks never connected under
			 * NetworkManager. Associate with the classic AKM instead:
			 * the firmware writes the RSN element of the association
			 * request itself (sme->ie is not used), and for drivers
			 * doing their own association wpa_supplicant takes the
			 * AKM from that element, disconnecting if its profile
			 * does not allow it.
			 */
			case WLAN_AKM_SUITE_8021X_SHA256:
				printk(KERN_INFO KBUILD_MODNAME ": %s: AP offers 802.1X-SHA256, "
					"which this core lacks; associating with 802.1X\n", dev->name);
				akm = WLAN_AKM_SUITE_8021X;
				fallthrough;
			case WLAN_AKM_SUITE_8021X:
				val = WPA2_AUTH_UNSPECIFIED;
				break;
			case WLAN_AKM_SUITE_PSK_SHA256:
				printk(KERN_INFO KBUILD_MODNAME ": %s: AP offers PSK-SHA256, "
					"which this core lacks; associating with PSK\n", dev->name);
				akm = WLAN_AKM_SUITE_PSK;
				fallthrough;
			case WLAN_AKM_SUITE_PSK:
				val = WPA2_AUTH_PSK;
				break;
			default:
				WL_ERR(("unsupported AKM suite 0x%08x\n", akm));
				return -EINVAL;
			}
		}

		WL_DBG(("setting wpa_auth to %d\n", val));
		err = wl_dev_intvar_set(dev, "wpa_auth", val);
		if (err) {
			WL_ERR(("could not set wpa_auth (%d)\n", err));
			return err;
		}
	}

	wl->profile->sec.wpa_auth = akm;

	return err;
}

static s32
wl_set_set_sharedkey(struct net_device *dev, struct cfg80211_connect_params *sme)
{
	struct wl_cfg80211_priv *wl = ndev_to_wl(dev);
	struct wl_cfg80211_security *sec;
	struct wl_wsec_key key;
	s32 err = 0;

	WL_DBG(("key len (%d)\n", sme->key_len));
	if (sme->key_len) {
		sec = &wl->profile->sec;
		WL_DBG(("wpa_versions 0x%x cipher_pairwise 0x%x\n",
		         sec->wpa_versions, sec->cipher_pairwise));
		if (!(sec->wpa_versions & (NL80211_WPA_VERSION_1 | NL80211_WPA_VERSION_2)) &&
		     (sec->cipher_pairwise &
		     (WLAN_CIPHER_SUITE_WEP40 | WLAN_CIPHER_SUITE_WEP104))) {
			memset(&key, 0, sizeof(key));
			key.len = (u32) sme->key_len;
			key.index = (u32) sme->key_idx;
			if (key.len > sizeof(key.data)) {
				WL_ERR(("Too long key length (%u)\n", key.len));
				return -EINVAL;
			}
			memcpy(key.data, sme->key, key.len);
			key.flags = WL_PRIMARY_KEY;
			switch (sec->cipher_pairwise) {
			case WLAN_CIPHER_SUITE_WEP40:
				key.algo = CRYPTO_ALGO_WEP1;
				break;
			case WLAN_CIPHER_SUITE_WEP104:
				key.algo = CRYPTO_ALGO_WEP128;
				break;
			default:
				WL_ERR(("Invalid algorithm (%d)\n",
				        sme->crypto.ciphers_pairwise[0]));
				return -EINVAL;
			}

			WL_DBG(("key length (%d) key index (%d) algo (%d)\n", key.len,
				key.index, key.algo));
			WL_DBG(("key \"%s\"\n", key.data));
			key_endian_to_device(&key);
			err = wl_dev_ioctl(dev, WLC_SET_KEY, &key, sizeof(key));
			if (err) {
				WL_ERR(("WLC_SET_KEY error (%d)\n", err));
				return err;
			}
		}
	}
	return err;
}

static s32
wl_cfg80211_connect(struct wiphy *wiphy, struct net_device *dev,
                    struct cfg80211_connect_params *sme)
{
	struct wl_cfg80211_priv *wl = wiphy_to_wl(wiphy);
	struct wl_join_params join_params;
	size_t join_params_size;
	char valc;
	s32 err = 0;

	if (!sme->ssid) {
		WL_ERR(("Invalid ssid\n"));
		return -EOPNOTSUPP;
	}

	WL_DBG(("ie (%p), ie_len (%zd)\n", sme->ie, sme->ie_len));

	err = wl_set_auth_type(dev, sme);
	if (err)
		return err;

	err = wl_set_wpa_version(dev, sme);
	if (err)
		return err;

	err = wl_set_set_cipher(dev, sme);
	if (err)
		return err;

	err = wl_set_key_mgmt(dev, sme);
	if (err)
		return err;

	err = wl_set_set_sharedkey(dev, sme);
	if (err)
		return err;

	valc = 1;
	wl_dev_bufvar_set(dev, "wsec_restrict", &valc, 1);

	if (sme->bssid) {
		memcpy(wl->profile->bssid, sme->bssid, ETHER_ADDR_LEN);
	}
	else {
		memset(wl->profile->bssid, 0, ETHER_ADDR_LEN);
	}

	memset(&join_params, 0, sizeof(join_params));
	join_params_size = sizeof(join_params.ssid);

	join_params.ssid.SSID_len = min(sizeof(join_params.ssid.SSID), sme->ssid_len);
	memcpy(&join_params.ssid.SSID, sme->ssid, join_params.ssid.SSID_len);
	join_params.ssid.SSID_len = htod32(join_params.ssid.SSID_len);
	memcpy(&join_params.params.bssid, &ether_bcast, ETHER_ADDR_LEN);

	memcpy(wl->profile->ssid.SSID, &join_params.ssid.SSID, join_params.ssid.SSID_len);
	wl->profile->ssid.SSID_len = join_params.ssid.SSID_len;

	wl_ch_to_chanspec(sme->channel, &join_params, &join_params_size);
	WL_DBG(("join_param_size %u\n", (unsigned int)join_params_size));

	if (join_params.ssid.SSID_len < IEEE80211_MAX_SSID_LEN) {
		WL_DBG(("ssid \"%s\", len (%d)\n", join_params.ssid.SSID,
		        join_params.ssid.SSID_len));
	}
	err = wl_dev_ioctl(dev, WLC_SET_SSID, &join_params, join_params_size);
	if (err) {
		WL_ERR(("error (%d)\n", err));
		return err;
	}

	set_bit(WL_STATUS_CONNECTING, &wl->status);

	return err;
}

static s32
wl_cfg80211_disconnect(struct wiphy *wiphy, struct net_device *dev, u16 reason_code)
{
	struct wl_cfg80211_priv *wl = wiphy_to_wl(wiphy);
	scb_val_t scbval;
	s32 err = 0;

	WL_DBG(("Reason %d\n", reason_code));

	if (wl->profile->active) {
		scbval.val = reason_code;
		memcpy(&scbval.ea, &wl->bssid, ETHER_ADDR_LEN);
		scbval.val = htod32(scbval.val);
		err = wl_dev_ioctl(dev, WLC_DISASSOC, &scbval, sizeof(scb_val_t));
		if (err) {
			WL_ERR(("error (%d)\n", err));
			return err;
		}
	}

	return err;
}

/*
 * cfg80211 hands us mBm. The firmware's "qtxpower" is in quarter-dBm, with
 * WL_TXPWR_OVERRIDE marking a user override and 127 without the flag meaning
 * "no user limit" (same convention as the in-kernel brcmfmac driver). The
 * original code pushed the mBm value through a milliwatt conversion.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 17, 0)
static s32 wl_cfg80211_set_tx_power(struct wiphy *wiphy, struct wireless_dev *wdev,
           s32 radio_idx, enum nl80211_tx_power_setting type, s32 mbm)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
static s32
wl_cfg80211_set_tx_power(struct wiphy *wiphy, struct wireless_dev *wdev,
                         enum nl80211_tx_power_setting type, s32 mbm)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
static s32
wl_cfg80211_set_tx_power(struct wiphy *wiphy, enum nl80211_tx_power_setting type, s32 mbm)
#else
#define NL80211_TX_POWER_AUTOMATIC TX_POWER_AUTOMATIC
#define NL80211_TX_POWER_LIMITED TX_POWER_LIMITED
#define NL80211_TX_POWER_FIXED TX_POWER_FIXED
static s32
wl_cfg80211_set_tx_power(struct wiphy *wiphy, enum tx_power_setting type, s32 mbm)
#endif
{

	struct wl_cfg80211_priv *wl = wiphy_to_wl(wiphy);
	struct net_device *ndev = wl_to_ndev(wl);
	u32 qdbm = 127;
	s32 err = 0;
	s32 disable = 0;

	switch (type) {
	case NL80211_TX_POWER_AUTOMATIC:
		break;
	case NL80211_TX_POWER_LIMITED:
	case NL80211_TX_POWER_FIXED:
		if (mbm < 0) {
			WL_ERR(("tx power must not be negative (%d mBm)\n", mbm));
			return -EINVAL;
		}
		qdbm = min(MBM_TO_DBM(mbm) * 4, 127) | WL_TXPWR_OVERRIDE;
		break;
	}

	disable = WL_RADIO_SW_DISABLE << 16;
	disable = htod32(disable);
	err = wl_dev_ioctl(ndev, WLC_SET_RADIO, &disable, sizeof(disable));
	if (err) {
		WL_ERR(("WLC_SET_RADIO error (%d)\n", err));
		return err;
	}

	err = wl_dev_intvar_set(ndev, "qtxpower", (s32)qdbm);
	if (err) {
		WL_ERR(("qtxpower error (%d)\n", err));
		return err;
	}
	wl->conf->tx_power = type == NL80211_TX_POWER_AUTOMATIC ? -1 : MBM_TO_DBM(mbm);

	return err;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 17, 0)
static s32 wl_cfg80211_get_tx_power(struct wiphy *wiphy, struct wireless_dev *wdev, s32 radio_idx,
           u32 /*link_id*/, s32 *dbm)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 14, 0)
static s32 wl_cfg80211_get_tx_power(struct wiphy *wiphy, struct wireless_dev *wdev, u32 /*link_id*/, s32 *dbm)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
static s32 wl_cfg80211_get_tx_power(struct wiphy *wiphy, struct wireless_dev *wdev, s32 *dbm)
#else
static s32 wl_cfg80211_get_tx_power(struct wiphy *wiphy, s32 *dbm)
#endif
{
	struct wl_cfg80211_priv *wl = wiphy_to_wl(wiphy);
	struct net_device *ndev = wl_to_ndev(wl);
	s32 qdbm;
	s32 err = 0;

	err = wl_dev_intvar_get(ndev, "qtxpower", &qdbm);
	if (err) {
		WL_ERR(("error (%d)\n", err));
		return err;
	}
	/* quarter-dBm from the firmware, dBm for cfg80211 (was reported as mW) */
	*dbm = (s32)((qdbm & ~WL_TXPWR_OVERRIDE) / 4);

	return err;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
static s32
wl_cfg80211_config_default_key(struct wiphy *wiphy,
	struct net_device *dev, int link_id, u8 key_idx, bool unicast,
	bool multicast)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38)
static s32
wl_cfg80211_config_default_key(struct wiphy *wiphy,
	struct net_device *dev, u8 key_idx, bool unicast, bool multicast)
#else
static s32
wl_cfg80211_config_default_key(struct wiphy *wiphy,
	struct net_device *dev, u8 key_idx)
#endif
{
	u32 index;
	s32 err = 0;

	WL_DBG(("key index (%d)\n", key_idx));

	index = (u32) key_idx;
	index = htod32(index);
	err = wl_dev_ioctl(dev, WLC_SET_KEY_PRIMARY, &index, sizeof(index));
	if (err) {
		WL_DBG(("error (%d)\n", err));
	}

	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 1, 0)
static s32
wl_cfg80211_add_key(struct wiphy *wiphy, struct wireless_dev *wdev,
                    int link_id, u8 key_idx, bool pairwise, const u8 *mac_addr,
                    struct key_params *params)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
static s32
wl_cfg80211_add_key(struct wiphy *wiphy, struct net_device *dev,
                    int link_id, u8 key_idx, bool pairwise, const u8 *mac_addr,
                    struct key_params *params)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)
static s32
wl_cfg80211_add_key(struct wiphy *wiphy, struct net_device *dev,
                    u8 key_idx, bool pairwise, const u8 *mac_addr, struct key_params *params)
#else
static s32
wl_cfg80211_add_key(struct wiphy *wiphy, struct net_device *dev,
                    u8 key_idx, const u8 *mac_addr, struct key_params *params)
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 1, 0)
	struct net_device *dev = wdev->netdev;
#endif
	struct wl_cfg80211_priv *wl = ndev_to_wl(dev);
	struct wl_wsec_key key;
	s32 secval, secnew = 0;
	s32 err = 0;

	WL_DBG(("key index %u    len %u\n", (unsigned)key_idx, params->key_len));

	memset(&key, 0, sizeof(key));

	key.index = (u32) key_idx;

	switch (params->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		key.algo = CRYPTO_ALGO_WEP1;
		secnew = WEP_ENABLED;
		WL_DBG(("WLAN_CIPHER_SUITE_WEP40\n"));
		break;
	case WLAN_CIPHER_SUITE_WEP104:
		key.algo = CRYPTO_ALGO_WEP128;
		secnew = WEP_ENABLED;
		WL_DBG(("WLAN_CIPHER_SUITE_WEP104\n"));
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		key.algo = CRYPTO_ALGO_TKIP;
		secnew = TKIP_ENABLED;
		WL_DBG(("WLAN_CIPHER_SUITE_TKIP\n"));
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
		key.algo = CRYPTO_ALGO_AES_CCM;
		secnew = AES_ENABLED;
		WL_DBG(("WLAN_CIPHER_SUITE_AES_CMAC\n"));
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		key.algo = CRYPTO_ALGO_AES_CCM;
		secnew = AES_ENABLED;
		WL_DBG(("WLAN_CIPHER_SUITE_CCMP\n"));
		break;
	default:
		WL_ERR(("Invalid cipher (0x%x)\n", params->cipher));
		return -EINVAL;
	}

	if (mac_addr) {
		if (!ETHER_ISMULTI(mac_addr)) {
			memcpy((char *)&key.ea, (void *)mac_addr, ETHER_ADDR_LEN);
		}
	}

	key.len = (u32) params->key_len;
	if (key.len > sizeof(key.data)) {
		WL_ERR(("Too long key length (%u)\n", key.len));
		return -EINVAL;
	}
	memcpy(key.data, params->key, key.len);

	if (params->cipher == WLAN_CIPHER_SUITE_TKIP) {
		u8 keybuf[8];
		memcpy(keybuf, &key.data[24], sizeof(keybuf));
		memcpy(&key.data[24], &key.data[16], sizeof(keybuf));
		memcpy(&key.data[16], keybuf, sizeof(keybuf));
	}

	if (params->seq_len) { 
		u8 *ivptr;
		if (params->seq_len != 6) {
			WL_ERR(("seq_len %d is unexpected, check implementation.\n",
				params->seq_len));
		}
		ivptr = (u8 *) params->seq;
		key.rxiv.hi = (ivptr[5] << 24) | (ivptr[4] << 16) | (ivptr[3] << 8) | ivptr[2];
		key.rxiv.lo = (ivptr[1] << 8) | ivptr[0];
		key.iv_initialized = true;
	}

	key_endian_to_device(&key);
	if (wl->passive) {
		schedule();
	}
	err = wl_dev_ioctl(dev, WLC_SET_KEY, &key, sizeof(key));
	if (err) {
		WL_ERR(("WLC_SET_KEY error (%d)\n", err));
		return err;
	}

	if ((err = wl_dev_intvar_get(dev, "wsec", &secval))) {
		return err;
	}
	if (secnew == WEP_ENABLED) {
		secval &= ~(TKIP_ENABLED | AES_ENABLED);
	}
	else {
		secval &= ~(WEP_ENABLED);
	}
	secval |= secnew;
	WL_DBG(("set wsec to %d\n", secval));
	err = wl_dev_intvar_set(dev, "wsec", secval);
	if (err) {
		WL_ERR(("error (%d)\n", err));
		return err;
	}

	if (mac_addr) {
		wl->profile->sec.cipher_pairwise = params->cipher;
	}
	else {
		wl->profile->sec.cipher_group = params->cipher;
	}

	return err;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 1, 0)
static s32
wl_cfg80211_del_key(struct wiphy *wiphy, struct wireless_dev *wdev,
                    int link_id, u8 key_idx, bool pairwise, const u8 *mac_addr)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
static s32
wl_cfg80211_del_key(struct wiphy *wiphy, struct net_device *dev,
                    int link_id, u8 key_idx, bool pairwise, const u8 *mac_addr)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)
static s32
wl_cfg80211_del_key(struct wiphy *wiphy, struct net_device *dev,
                    u8 key_idx, bool pairwise, const u8 *mac_addr)
#else
static s32
wl_cfg80211_del_key(struct wiphy *wiphy, struct net_device *dev,
                    u8 key_idx, const u8 *mac_addr)
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 1, 0)
	struct net_device *dev = wdev->netdev;
#endif
	struct wl_wsec_key key;
	s32 err = 0;

	WL_DBG(("key index (%d)\n", key_idx));

	memset(&key, 0, sizeof(key));

	key.index = (u32) key_idx;
	key.len = 0;
	if (mac_addr) {
		if (!ETHER_ISMULTI(mac_addr)) {
			memcpy((char *)&key.ea, (void *)mac_addr, ETHER_ADDR_LEN);
		}
	}
	key.algo = CRYPTO_ALGO_OFF;

	key_endian_to_device(&key);
	err = wl_dev_ioctl(dev, WLC_SET_KEY, &key, sizeof(key));
	if (err) {
		if (err == -EINVAL) {
			if (key.index >= DOT11_MAX_DEFAULT_KEYS) {

				WL_DBG(("invalid key index (%d)\n", key_idx));
			}
		} else {
			WL_ERR(("WLC_SET_KEY error (%d)\n", err));
		}
		return err;
	}

	return err;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 1, 0)
static s32
wl_cfg80211_get_key(struct wiphy *wiphy, struct wireless_dev *wdev,
                    int link_id, u8 key_idx, bool pairwise, const u8 *mac_addr,
                    void *cookie,
                    void (*callback) (void *cookie, struct key_params * params))
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
static s32
wl_cfg80211_get_key(struct wiphy *wiphy, struct net_device *dev,
                    int link_id, u8 key_idx, bool pairwise, const u8 *mac_addr,
                    void *cookie,
                    void (*callback) (void *cookie, struct key_params * params))
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)
static s32
wl_cfg80211_get_key(struct wiphy *wiphy, struct net_device *dev,
                    u8 key_idx, bool pairwise, const u8 *mac_addr, void *cookie,
                    void (*callback) (void *cookie, struct key_params * params))
#else
static s32
wl_cfg80211_get_key(struct wiphy *wiphy, struct net_device *dev,
                    u8 key_idx, const u8 *mac_addr, void *cookie,
                    void (*callback) (void *cookie, struct key_params * params))
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 1, 0)
	struct net_device *dev = wdev->netdev;
#endif
	struct key_params params;
	struct wl_wsec_key key;
	struct wl_cfg80211_priv *wl = wiphy_to_wl(wiphy);
	struct wl_cfg80211_security *sec;
	s32 wsec;
	s32 err = 0;

	WL_DBG(("key index (%d)\n", key_idx));

	memset(&params, 0, sizeof(params));

	memset(&key, 0, sizeof(key));
	key.index = key_idx;
	key_endian_to_device(&key);

	if ((err = wl_dev_ioctl(dev, WLC_GET_KEY, &key, sizeof(key)))) {
		return err;
	}
	key_endian_to_host(&key);

	params.key_len = (u8) min_t(u8, DOT11_MAX_KEY_SIZE, key.len);
	memcpy((char *)params.key, key.data, params.key_len);

	if ((err = wl_dev_ioctl(dev, WLC_GET_WSEC, &wsec, sizeof(wsec)))) {
		return err;
	}
	wsec = dtoh32(wsec);
	switch (wsec) {
	case WEP_ENABLED:
		sec = &wl->profile->sec;
		if (sec->cipher_pairwise & WLAN_CIPHER_SUITE_WEP40) {
			params.cipher = WLAN_CIPHER_SUITE_WEP40;
			WL_DBG(("WLAN_CIPHER_SUITE_WEP40\n"));
		} else if (sec->cipher_pairwise & WLAN_CIPHER_SUITE_WEP104) {
			params.cipher = WLAN_CIPHER_SUITE_WEP104;
			WL_DBG(("WLAN_CIPHER_SUITE_WEP104\n"));
		}
		break;
	case TKIP_ENABLED:
		params.cipher = WLAN_CIPHER_SUITE_TKIP;
		WL_DBG(("WLAN_CIPHER_SUITE_TKIP\n"));
		break;
	case AES_ENABLED:
		params.cipher = WLAN_CIPHER_SUITE_AES_CMAC;
		WL_DBG(("WLAN_CIPHER_SUITE_AES_CMAC\n"));
		break;
	default:
		WL_ERR(("Invalid algo (0x%x)\n", wsec));
		return -EINVAL;
	}

	callback(cookie, &params);
	return err;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 16, 0)
static s32
wl_cfg80211_get_station(struct wiphy *wiphy, struct net_device *dev,
                        u8 *mac, struct station_info *sinfo)
#elif LINUX_VERSION_CODE < KERNEL_VERSION(7, 1, 0)
static s32
wl_cfg80211_get_station(struct wiphy *wiphy, struct net_device *dev,
                        const u8 *mac, struct station_info *sinfo)
#else
static s32
wl_cfg80211_get_station(struct wiphy *wiphy, struct wireless_dev *wdev,
                        const u8 *mac, struct station_info *sinfo)
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 1, 0)
	struct net_device *dev = wdev->netdev;
#endif
	struct wl_cfg80211_priv *wl = wiphy_to_wl(wiphy);
	scb_val_t scb_val;
	int rssi;
	s32 rate;
	s32 err = 0;

	/* Our only peer is the AP; don't report its figures under another address. */
	if (memcmp(mac, wl->profile->bssid, ETHER_ADDR_LEN)) {
		WL_DBG(("no station %pM (connected to %pM)\n", mac, wl->profile->bssid));
		return -ENOENT;
	}

	err = wl_dev_ioctl(dev, WLC_GET_RATE, &rate, sizeof(rate));
	if (err) {
		WL_DBG(("Could not get rate (%d)\n", err));
	} else {
		rate = dtoh32(rate);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
		sinfo->filled |= BIT(NL80211_STA_INFO_TX_BITRATE);
#else
		sinfo->filled |= STATION_INFO_TX_BITRATE;
#endif
		sinfo->txrate.legacy = rate * 5;
		WL_DBG(("Rate %d Mbps\n", (rate / 2)));
	}

	if (test_bit(WL_STATUS_CONNECTED, &wl->status)) {
		memset(&scb_val, 0, sizeof(scb_val));
		err = wl_dev_ioctl(dev, WLC_GET_RSSI, &scb_val, sizeof(scb_val_t));
		if (err) {
			WL_DBG(("Could not get rssi (%d)\n", err));
			return err;
		}
		rssi = dtoh32(scb_val.val);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
		sinfo->filled |= BIT(NL80211_STA_INFO_SIGNAL);
#else
		sinfo->filled |= STATION_INFO_SIGNAL;
#endif
		sinfo->signal = rssi;
		WL_DBG(("RSSI %d dBm\n", rssi));
	}

	return err;
}

/*
 * A station's only peer is the AP it is associated with. Without this op,
 * `iw dev wlan0 station dump` and tools polling signal strength through it
 * found no station at all.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(7, 1, 0)
static s32
wl_cfg80211_dump_station(struct wiphy *wiphy, struct net_device *dev,
                         int idx, u8 *mac, struct station_info *sinfo)
#else
static s32
wl_cfg80211_dump_station(struct wiphy *wiphy, struct wireless_dev *wdev,
                         int idx, u8 *mac, struct station_info *sinfo)
#endif
{
	struct wl_cfg80211_priv *wl = wiphy_to_wl(wiphy);

	if (idx != 0 || !test_bit(WL_STATUS_CONNECTED, &wl->status))
		return -ENOENT;
	memcpy(mac, wl->profile->bssid, ETHER_ADDR_LEN);
#if LINUX_VERSION_CODE < KERNEL_VERSION(7, 1, 0)
	return wl_cfg80211_get_station(wiphy, dev, mac, sinfo);
#else
	return wl_cfg80211_get_station(wiphy, wdev, mac, sinfo);
#endif
}

static s32
wl_cfg80211_set_power_mgmt(struct wiphy *wiphy, struct net_device *dev,
                           bool enabled, s32 timeout)
{
	s32 pm;
	s32 err = 0;

	pm = enabled ? PM_FAST : PM_OFF;
	pm = htod32(pm);
	WL_DBG(("power save %s\n", (pm ? "enabled" : "disabled")));
	err = wl_dev_ioctl(dev, WLC_SET_PM, &pm, sizeof(pm));
	if (err) {
		if (err == -ENODEV)
			WL_DBG(("net_device is not ready yet\n"));
		else
			WL_ERR(("error (%d)\n", err));
		return err;
	}
	return err;
}

static __used s32
wl_update_pmklist(struct net_device *dev, struct wl_cfg80211_pmk_list *pmk_list, s32 err)
{
	int i, j;

	WL_DBG(("No of elements %d\n", pmk_list->pmkids.npmkid));
	for (i = 0; i < pmk_list->pmkids.npmkid; i++) {
		WL_DBG(("PMKID[%d]: %pM =\n", i,
			&pmk_list->pmkids.pmkid[i].BSSID));
		for (j = 0; j < WPA2_PMKID_LEN; j++) {
			WL_DBG(("%02x\n", pmk_list->pmkids.pmkid[i].PMKID[j]));
		}
	}
	if (!err) {
		err = wl_dev_bufvar_set(dev, "pmkid_info", (char *)pmk_list, sizeof(*pmk_list));
	}

	return err;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 33)

static s32
wl_cfg80211_set_pmksa(struct wiphy *wiphy, struct net_device *dev,
                      struct cfg80211_pmksa *pmksa)
{
	struct wl_cfg80211_priv *wl = wiphy_to_wl(wiphy);
	s32 err = 0;
	int i;

	for (i = 0; i < wl->pmk_list->pmkids.npmkid; i++)
		if (!memcmp(pmksa->bssid, &wl->pmk_list->pmkids.pmkid[i].BSSID, ETHER_ADDR_LEN))
			break;
	if (i < WL_NUM_PMKIDS_MAX) {
		memcpy(&wl->pmk_list->pmkids.pmkid[i].BSSID, pmksa->bssid, ETHER_ADDR_LEN);
		memcpy(&wl->pmk_list->pmkids.pmkid[i].PMKID, pmksa->pmkid, WPA2_PMKID_LEN);
		if (i == wl->pmk_list->pmkids.npmkid)
			wl->pmk_list->pmkids.npmkid++;
	} else {
		err = -EINVAL;
	}
	WL_DBG(("set_pmksa,IW_PMKSA_ADD - PMKID: %pM =\n",
		&wl->pmk_list->pmkids.pmkid[wl->pmk_list->pmkids.npmkid].BSSID));
	for (i = 0; i < WPA2_PMKID_LEN; i++) {
		WL_DBG(("%02x\n",
			wl->pmk_list->pmkids.pmkid[wl->pmk_list->pmkids.npmkid].PMKID[i]));
	}

	err = wl_update_pmklist(dev, wl->pmk_list, err);

	return err;
}

static s32
wl_cfg80211_del_pmksa(struct wiphy *wiphy, struct net_device *dev,
                      struct cfg80211_pmksa *pmksa)
{
	struct wl_cfg80211_priv *wl = wiphy_to_wl(wiphy);
	struct _pmkid_list pmkid;
	s32 err = 0;
	int i;

	memcpy(&pmkid.pmkid[0].BSSID, pmksa->bssid, ETHER_ADDR_LEN);
	memcpy(&pmkid.pmkid[0].PMKID, pmksa->pmkid, WPA2_PMKID_LEN);

	WL_DBG(("del_pmksa,IW_PMKSA_REMOVE - PMKID: %pM =\n",
		&pmkid.pmkid[0].BSSID));
	for (i = 0; i < WPA2_PMKID_LEN; i++) {
		WL_DBG(("%02x\n", pmkid.pmkid[0].PMKID[i]));
	}

	for (i = 0; i < wl->pmk_list->pmkids.npmkid; i++)
		if (!memcmp(pmksa->bssid, &wl->pmk_list->pmkids.pmkid[i].BSSID, ETHER_ADDR_LEN))
			break;

	if ((wl->pmk_list->pmkids.npmkid > 0) && (i < wl->pmk_list->pmkids.npmkid)) {
		memset(&wl->pmk_list->pmkids.pmkid[i], 0, sizeof(pmkid_t));
		for (; i < (wl->pmk_list->pmkids.npmkid - 1); i++) {
			memcpy(&wl->pmk_list->pmkids.pmkid[i].BSSID,
			       &wl->pmk_list->pmkids.pmkid[i + 1].BSSID, ETHER_ADDR_LEN);
			memcpy(&wl->pmk_list->pmkids.pmkid[i].PMKID,
			       &wl->pmk_list->pmkids.pmkid[i + 1].PMKID, WPA2_PMKID_LEN);
		}
		wl->pmk_list->pmkids.npmkid--;
	} else {
		err = -EINVAL;
	}

	err = wl_update_pmklist(dev, wl->pmk_list, err);

	return err;

}

static s32
wl_cfg80211_flush_pmksa(struct wiphy *wiphy, struct net_device *dev)
{
	struct wl_cfg80211_priv *wl = wiphy_to_wl(wiphy);
	s32 err = 0;

	memset(wl->pmk_list, 0, sizeof(*wl->pmk_list));
	err = wl_update_pmklist(dev, wl->pmk_list, err);
	return err;

}

#endif  

#ifdef CONFIG_PM
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)

static int
wl_wowl_ind_wake_reason(struct wl_cfg80211_priv *wl, struct cfg80211_wowlan_wakeup *wakeup)
{
	wl_wowl_wakeind_t wowl_ind;
	s32 err;

	err = wl_dev_bufvar_get(wl_to_ndev(wl), "wowl_wakeind",
		(s8 *)&wowl_ind, sizeof(wowl_ind));
	if (err != 0) {
		WL_ERR(("Unable to get wake reason, err = %d\n", err));
		return -1;
	}

	if (wowl_ind.ucode_wakeind == 0) {
		WL_DBG(("System woke, but not by us\n"));
		return 0;
	}
	WL_DBG(("wake reason is 0x%x\n", wowl_ind.ucode_wakeind));

	if (wowl_ind.ucode_wakeind & WL_WOWL_MAGIC) {
		WL_ERR(("WOWLAN Woke for: Magic Pkt\n"));
		wakeup->magic_pkt = true;
	}
	if (wowl_ind.ucode_wakeind & WL_WOWL_DIS) {
		WL_ERR(("WOWLAN Woke for: Disconnect\n"));
		wakeup->disconnect = true;
	}
	if (wowl_ind.ucode_wakeind & WL_WOWL_BCN) {
		WL_ERR(("WOWLAN Woke for: Beacon Loss\n"));
		wakeup->disconnect = true;
	}
	if (wowl_ind.ucode_wakeind & WL_WOWL_GTK_FAILURE) {
		WL_ERR(("WOWLAN Woke for: GTK failure\n"));
		wakeup->gtk_rekey_failure = true;
	}
	if (wowl_ind.ucode_wakeind & WL_WOWL_EAPID) {
		WL_ERR(("WOWLAN Woke for: EAP identify request\n"));
		wakeup->eap_identity_req = true;
	}
	if (wowl_ind.ucode_wakeind & WL_WOWL_M1) {
		WL_ERR(("WOWLAN Woke for: 4-way handshake request\n"));
		wakeup->four_way_handshake = true;
	}
	return 1;
}
#endif 

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0)
static int
wl_cfg80211_rekey(struct wiphy *wiphy, struct net_device *ndev,
        struct cfg80211_gtk_rekey_data *data)
{
	struct wl_cfg80211_priv *wl = wiphy_to_wl(wiphy);
	wlc_rekey_info_t rekey;
	s32 err;

	if (!wl->offloads) {
		return 0;
	}

	memset(&rekey, 0, sizeof(rekey));
	memcpy(&rekey.kek, data->kek, WLC_KEK_LEN);
	memcpy(&rekey.kck, data->kck, WLC_KCK_LEN);
	memcpy(&rekey.replay_counter, data->replay_ctr, WLC_REPLAY_CTR_LEN);
	WL_INF(("Send down replay counter %x%x%x%x%x%x%x%x\n",
		rekey.replay_counter[0], rekey.replay_counter[1], rekey.replay_counter[2],
		rekey.replay_counter[3], rekey.replay_counter[4], rekey.replay_counter[5],
		rekey.replay_counter[6], rekey.replay_counter[7]));

	err = wl_dev_bufvar_set(wl_to_ndev(wl), "wowl_replay", (s8 *)&rekey, sizeof(rekey));
	if (err) {
		WL_ERR(("Error calling wowl_set_key\n"));
		return err;
	}
	return err;
}
#endif 

static int wl_cfg80211_suspend(struct wiphy *wiphy, struct cfg80211_wowlan *wowlan)
{
	struct wl_cfg80211_priv *wl = wiphy_to_wl(wiphy);
	uint wowl = 0;
	s32 err;

	if (!wowlan) {
		WL_DBG(("No wowlan requested\n"));
		return 0;
	}
	if (!test_bit(WL_STATUS_CONNECTED, &wl->status)) {
		WL_INF(("No wowl when not associated.\n"));
		return 0;
	}

	err = wl_dev_intvar_get(wl_to_ndev(wl), "wowl", &wowl);
	if (err) {
		WL_ERR(("Error fetching WOWL %d\n", err));
	}
	if (wowlan->disconnect) {
		WL_INF(("Requesting wake on Disconnect\n"));
		wowl |= WL_WOWL_DIS | WL_WOWL_BCN;
	}
	if (wowlan->magic_pkt) {
		WL_INF(("Requesting wake on Magic Pkt\n"));
		wowl |= WL_WOWL_MAGIC;
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0)
	if (wowlan->gtk_rekey_failure) {
		WL_INF(("Requesting wake GTK rekey failure Pkt\n"));
		wowl |= WL_WOWL_GTK_FAILURE;
	}
	if (wowlan->four_way_handshake) {
		WL_INF(("Requesting wake on 4way handshake request\n"));
		wowl |= WL_WOWL_M1;
	}
#endif 

	wowl |= WL_WOWL_KEYROT;

	err = wl_dev_intvar_set(wl_to_ndev(wl), "wowl", wowl);
	if (err) {
		WL_ERR(("Error enabling WOWL %d\n", err));
	}

	return err;
}
#else 
static int wl_cfg80211_suspend(struct wiphy *wiphy)
{
	return 0;
}
#endif 

static int wl_cfg80211_resume(struct wiphy *wiphy)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0)
	struct wl_cfg80211_priv *wl = wiphy_to_wl(wiphy);
	wlc_rekey_info_t *rekey = (wlc_rekey_info_t *)wl->extra_buf;
	s32 err;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
	struct cfg80211_wowlan_wakeup wakeup;
	int result;

	memset(&wakeup, 0, sizeof(wakeup));
	wakeup.pattern_idx = -1;

	result = wl_wowl_ind_wake_reason(wl, &wakeup);
	switch (result) {
		case -1: 
			break;
		case 0:	
			cfg80211_report_wowlan_wakeup(wl_to_wdev(wl), NULL, GFP_KERNEL);
			break;
		case 1: 
			cfg80211_report_wowlan_wakeup(wl_to_wdev(wl), &wakeup, GFP_KERNEL);
			break;
	}
#endif 

	err = wl_dev_bufvar_get(wl_to_ndev(wl), "wowl_replay", (s8 *)rekey,
		sizeof(wlc_rekey_info_t));
	if (!err) {
		WL_INF(("Send up replay counter %x%x%x%x%x%x%x%x\n",
			rekey->replay_counter[0], rekey->replay_counter[1],
			rekey->replay_counter[2], rekey->replay_counter[3],
			rekey->replay_counter[4], rekey->replay_counter[5],
			rekey->replay_counter[6], rekey->replay_counter[7]));
		cfg80211_gtk_rekey_notify(wl_to_ndev(wl), (u8 *)&wl->bssid.octet,
			rekey->replay_counter, GFP_KERNEL);
	}
#endif 
	return 0;
}
#endif 

static struct cfg80211_ops wl_cfg80211_ops = {
	.change_virtual_intf = wl_cfg80211_change_iface,
	.scan = wl_cfg80211_scan,
	.set_wiphy_params = wl_cfg80211_set_wiphy_params,
	.join_ibss = wl_cfg80211_join_ibss,
	.leave_ibss = wl_cfg80211_leave_ibss,
	.get_station = wl_cfg80211_get_station,
	.dump_station = wl_cfg80211_dump_station,
	.set_tx_power = wl_cfg80211_set_tx_power,
	.get_tx_power = wl_cfg80211_get_tx_power,
	.add_key = wl_cfg80211_add_key,
	.del_key = wl_cfg80211_del_key,
	.get_key = wl_cfg80211_get_key,
	.set_default_key = wl_cfg80211_config_default_key,
	.set_power_mgmt = wl_cfg80211_set_power_mgmt,
	.connect = wl_cfg80211_connect,
	.disconnect = wl_cfg80211_disconnect,
#ifdef CONFIG_PM
	.suspend = wl_cfg80211_suspend,
	.resume = wl_cfg80211_resume,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0)
	.set_rekey_data = wl_cfg80211_rekey,
#endif 
#endif 
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 33)
	.set_pmksa = wl_cfg80211_set_pmksa,
	.del_pmksa = wl_cfg80211_del_pmksa,
	.flush_pmksa = wl_cfg80211_flush_pmksa
#endif
};

#ifdef CONFIG_PM
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
static const struct wiphy_wowlan_support wl_wowlan_support = {
#else
static struct wiphy_wowlan_support wl_wowlan_support = {
#endif 
	.flags = WIPHY_WOWLAN_MAGIC_PKT
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0)
	| WIPHY_WOWLAN_SUPPORTS_GTK_REKEY | WIPHY_WOWLAN_GTK_REKEY_FAILURE |
	WIPHY_WOWLAN_EAP_IDENTITY_REQ
#endif
	| WIPHY_WOWLAN_DISCONNECT,
};
#endif 
#endif 

static s32 wl_mode_to_nl80211_iftype(s32 mode)
{
	s32 err = 0;

	switch (mode) {
	case WL_MODE_BSS:
		return NL80211_IFTYPE_STATION;
	case WL_MODE_IBSS:
		return NL80211_IFTYPE_ADHOC;
	default:
		return NL80211_IFTYPE_UNSPECIFIED;
	}

	return err;
}

static s32 wl_alloc_wdev(struct device *dev, struct wireless_dev **rwdev)
{
	struct wireless_dev *wdev;
	s32 err = 0;

	wdev = kzalloc(sizeof(*wdev), GFP_KERNEL);
	if (!wdev) {
		WL_ERR(("Could not allocate wireless device\n"));
		err = -ENOMEM;
		goto early_out;
	}
	wdev->wiphy = wiphy_new(&wl_cfg80211_ops, sizeof(struct wl_cfg80211_priv));
	if (!wdev->wiphy) {
		WL_ERR(("Couldn not allocate wiphy device\n"));
		err = -ENOMEM;
		goto wiphy_new_out;
	}
	set_wiphy_dev(wdev->wiphy, dev);
	wdev->wiphy->max_scan_ssids = WL_NUM_SCAN_MAX;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 33)
	wdev->wiphy->max_num_pmkids = WL_NUM_PMKIDS_MAX;
#endif
	wdev->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) | BIT(NL80211_IFTYPE_ADHOC);
	wdev->wiphy->bands[NL80211_BAND_2GHZ] = &__wl_band_2ghz;
	wdev->wiphy->bands[NL80211_BAND_5GHZ] = &__wl_band_5ghz_a; 
	wdev->wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;
	wdev->wiphy->cipher_suites = __wl_cipher_suites;
	wdev->wiphy->n_cipher_suites = ARRAY_SIZE(__wl_cipher_suites);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 33)

	wdev->wiphy->flags &= ~WIPHY_FLAG_PS_ON_BY_DEFAULT;
#endif

#ifdef CONFIG_PM
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
	wdev->wiphy->wowlan = &wl_wowlan_support;
#else
	wdev->wiphy->wowlan = wl_wowlan_support;
#endif 
#endif 
#endif 

	err = wiphy_register(wdev->wiphy);
	if (err < 0) {
		WL_ERR(("Couldn not register wiphy device (%d)\n", err));
		goto wiphy_register_out;
	}

	*rwdev = wdev;
	return err;

wiphy_register_out:
	wiphy_free(wdev->wiphy);

wiphy_new_out:
	kfree(wdev);

early_out:
	*rwdev = wdev;
	return err;
}

static void wl_free_wdev(struct wl_cfg80211_priv *wl)
{
	struct wireless_dev *wdev = wl_to_wdev(wl);

	if (!wdev) {
		WL_ERR(("wdev is invalid\n"));
		return;
	}
	wiphy_unregister(wdev->wiphy);
	wiphy_free(wdev->wiphy);
	kfree(wdev);
	wl_to_wdev(wl) = NULL;
}

/* Every field wl_inform_single_bss() reads lies before SNR. */
#define WL_BSS_INFO_MIN_LEN	offsetof(struct wl_bss_info, SNR)

static s32 wl_inform_bss(struct wl_cfg80211_priv *wl, struct wl_scan_results *bss_list,
                         u32 buflen)
{
	struct wl_bss_info *bi = NULL;
	s32 err = 0;
	int i;

	if (bss_list->version != WL_BSS_INFO_VERSION) {
		WL_ERR(("Version %d != WL_BSS_INFO_VERSION\n", bss_list->version));
		return -EOPNOTSUPP;
	}
	WL_DBG(("scanned AP count (%d)\n", bss_list->count));
	bi = next_bss(bss_list, bi);
	for_each_bss(bss_list, bi, i) {
		size_t off = (u8 *)bi - (u8 *)bss_list;
		u32 len;

		if (off + offsetof(struct wl_bss_info, BSSID) > buflen)
			break;
		len = dtoh32(bi->length);
		if (len < WL_BSS_INFO_MIN_LEN || len > buflen - off) {
			WL_ERR(("scan entry %d has bad length %u, dropping the rest\n", i, len));
			break;
		}
		/*
		 * One odd entry must not hide every network after it: the old
		 * loop stopped at the first failure, so e.g. an AP with a large
		 * IE set could make the user's own network vanish from the list.
		 */
		err = wl_inform_single_bss(wl, bi, NULL);
		if (err == -ENOMEM)
			return err;
	}
	return 0;
}

/*
 * Report one firmware BSS entry to cfg80211. The IEs are passed straight from
 * the firmware record; the old code first built a fake beacon frame through a
 * 1 KiB staging buffer (dropping APs with more IEs than that) and wrote it via
 * a zero-length array, which FORTIFY_SOURCE reports as a field-spanning write.
 *
 * If @bss_out is non-NULL it receives a referenced entry that the caller must
 * pass on to cfg80211 or drop with cfg80211_put_bss().
 */
static s32 wl_inform_single_bss(struct wl_cfg80211_priv *wl, struct wl_bss_info *bi,
                                struct cfg80211_bss **bss_out)
{
	struct wiphy *wiphy = wl_to_wiphy(wl);
	struct ieee80211_channel *channel;
	struct cfg80211_bss *cbss;
	u32 len = dtoh32(bi->length);
	u32 ie_off = dtoh16(bi->ie_offset);
	u32 ie_len = dtoh32(bi->ie_length);
	u32 chan, freq;
	s32 signal;

	if (len < WL_BSS_INFO_MIN_LEN || ie_off > len || ie_len > len - ie_off) {
		WL_DBG(("malformed entry for %pM (len %u, ie %u+%u)\n",
			&bi->BSSID, len, ie_off, ie_len));
		return -EINVAL;
	}

	chan = bi->ctl_ch ? bi->ctl_ch : CHSPEC_CHANNEL(dtohchanspec(bi->chanspec));
	freq = ieee80211_channel_to_frequency(chan,
		chan <= CH_MAX_2G_CHANNEL ? NL80211_BAND_2GHZ : NL80211_BAND_5GHZ);
	channel = freq ? ieee80211_get_channel(wiphy, freq) : NULL;
	if (unlikely(!channel)) {
		WL_DBG(("no usable channel for %pM (chan %u)\n", &bi->BSSID, chan));
		return -EINVAL;
	}

	signal = (s16)dtoh16(bi->RSSI) * 100;

	WL_DBG(("SSID \"%.*s\", rssi %d, channel %u, capability 0x%04x, bssid %pM\n",
		bi->SSID_len, bi->SSID, signal / 100, chan, dtoh16(bi->capability),
		&bi->BSSID));

	cbss = cfg80211_inform_bss(wiphy, channel,
			wl->active_scan ? CFG80211_BSS_FTYPE_PRESP : CFG80211_BSS_FTYPE_BEACON,
			(const u8 *)bi->BSSID.octet, 0,
			dtoh16(bi->capability), dtoh16(bi->beacon_period),
			(const u8 *)bi + ie_off, ie_len, signal, GFP_KERNEL);
	if (unlikely(!cbss)) {
		WL_ERR(("cfg80211_inform_bss failed for %pM\n", &bi->BSSID));
		return -ENOMEM;
	}

	if (bss_out)
		*bss_out = cbss;
	else
		cfg80211_put_bss(wiphy, cbss);

	return 0;
}

static s32
wl_notify_connect_status(struct wl_cfg80211_priv *wl, struct net_device *ndev,
                         const wl_event_msg_t *e, void *data)
{
	s32 err = 0;
	u32 event = EVENT_TYPE(e);
	u16 flags = EVENT_FLAGS(e);
	u32 status = EVENT_STATUS(e);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
	struct ieee80211_channel *channel = NULL;
	struct wiphy *wiphy;
	u32 chanspec, chan;
	u32 freq, band;
#endif 

	WL_DBG(("\n"));

	if (!wl_is_ibssmode(wl)) {
		if (event == WLC_E_LINK && (flags & WLC_EVENT_MSG_LINK)) {
			wl_link_up(wl);
			wl_bss_connect_done(wl, ndev, e, data, true);
			wl->profile->active = true;
		}
		else if ((event == WLC_E_LINK && ~(flags & WLC_EVENT_MSG_LINK)) ||
			event == WLC_E_DEAUTH_IND || event == WLC_E_DISASSOC_IND) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,2,0)
			cfg80211_disconnected(ndev, 0, NULL, 0, GFP_KERNEL);
#else
			cfg80211_disconnected(ndev, 0, NULL, 0, false, GFP_KERNEL);
#endif
			clear_bit(WL_STATUS_CONNECTED, &wl->status);
			wl_link_down(wl);
			wl_init_prof(wl->profile);
		}
		else if (event == WLC_E_SET_SSID && status == WLC_E_STATUS_NO_NETWORKS) {
			wl_bss_connect_done(wl, ndev, e, data, false);
		}
		else {
			WL_DBG(("no action (BSS mode)\n"));
		}
	}
	else {
		if (event == WLC_E_JOIN) {
			WL_DBG(("joined in IBSS network\n"));
		}
		if (event == WLC_E_START) {
			WL_DBG(("started IBSS network\n"));
		}
		if (event == WLC_E_JOIN || event == WLC_E_START) {
			wl_link_up(wl);
			wl_get_assoc_ies(wl);
			memcpy(&wl->bssid, &e->addr, ETHER_ADDR_LEN);
			wl_update_bss_info(wl, NULL);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
			wiphy = wl_to_wiphy(wl);
			err = wl_dev_intvar_get(ndev, "chanspec", &chanspec);
			if (err) {
				WL_ERR(("Could not get chanspec, err %d\n", err));
				return err;
			}
			chan = wf_chspec_ctlchan(chanspec);
			band = (chan <= CH_MAX_2G_CHANNEL) ? NL80211_BAND_2GHZ : NL80211_BAND_5GHZ;
			freq = ieee80211_channel_to_frequency(chan, band);
			channel = ieee80211_get_channel(wiphy, freq);
			cfg80211_ibss_joined(ndev, (u8 *)&wl->bssid, channel, GFP_KERNEL);
#else
			cfg80211_ibss_joined(ndev, (u8 *)&wl->bssid, GFP_KERNEL);
#endif
			set_bit(WL_STATUS_CONNECTED, &wl->status);
			wl->profile->active = true;
		}
		else if ((event == WLC_E_LINK && ~(flags & WLC_EVENT_MSG_LINK)) ||
			event == WLC_E_DEAUTH_IND || event == WLC_E_DISASSOC_IND) {
			clear_bit(WL_STATUS_CONNECTED, &wl->status);
			wl_link_down(wl);
			wl_init_prof(wl->profile);
		}
		else if (event == WLC_E_SET_SSID && status == WLC_E_STATUS_NO_NETWORKS) {
			WL_DBG(("no action - join fail (IBSS mode)\n"));
		}
		else {
			WL_DBG(("no action (IBSS mode)\n"));
		}
	}

	return err;
}

static s32
wl_notify_roaming_status(struct wl_cfg80211_priv *wl, struct net_device *ndev,
                         const wl_event_msg_t *e, void *data)
{
	s32 err = 0;
	u32 status = EVENT_STATUS(e);

	WL_DBG(("\n"));

	if (status == WLC_E_STATUS_SUCCESS) {
		err = wl_bss_roaming_done(wl, ndev, e, data);
		wl->profile->active = true;
	}

	return err;
}

static __used s32
wl_dev_bufvar_set(struct net_device *dev, s8 *name, s8 *buf, s32 len)
{
	struct wl_cfg80211_priv *wl = ndev_to_wl(dev);
	u32 buflen;

	buflen = bcm_mkiovar(name, buf, len, wl->ioctl_buf, WL_IOCTL_LEN_MAX);
	BUG_ON(!buflen);

	return wl_dev_ioctl(dev, WLC_SET_VAR, wl->ioctl_buf, buflen);
}

static s32
wl_dev_bufvar_get(struct net_device *dev, s8 *name, s8 *buf, s32 buf_len)
{
	struct wl_cfg80211_priv *wl = ndev_to_wl(dev);
	u32 len;
	s32 err = 0;

	len = bcm_mkiovar(name, NULL, 0, wl->ioctl_buf, WL_IOCTL_LEN_MAX);
	BUG_ON(!len);
	err = wl_dev_ioctl(dev, WLC_GET_VAR, (void *)wl->ioctl_buf, WL_IOCTL_LEN_MAX);
	if (err) {
		WL_INF(("error (%d)\n", err));
		return err;
	}
	memcpy(buf, wl->ioctl_buf, buf_len);

	return err;
}

static s32 wl_get_assoc_ies(struct wl_cfg80211_priv *wl)
{
	struct net_device *ndev = wl_to_ndev(wl);
	struct wl_cfg80211_assoc_ielen *assoc_info;
	struct wl_cfg80211_connect_info *conn_info = wl_to_conn(wl);
	u32 req_len;
	u32 resp_len;
	s32 err = 0;

	err = wl_dev_bufvar_get(ndev, "assoc_info", wl->extra_buf, WL_ASSOC_INFO_MAX);
	if (err) {
		WL_ERR(("could not get assoc info (%d)\n", err));
		return err;
	}
	assoc_info = (struct wl_cfg80211_assoc_ielen *)wl->extra_buf;
	/*
	 * The lengths come from the firmware; never copy more than was fetched.
	 * (They used to be taken as-is while only 512 bytes were fetched.)
	 */
	req_len = min_t(u32, dtoh32(assoc_info->req_len), WL_EXTRA_BUF_MAX);
	resp_len = min_t(u32, dtoh32(assoc_info->resp_len), WL_EXTRA_BUF_MAX);

	/* On a roam the previous association's IEs are still held: free them. */
	kfree(conn_info->req_ie);
	conn_info->req_ie = NULL;
	conn_info->req_ie_len = 0;
	kfree(conn_info->resp_ie);
	conn_info->resp_ie = NULL;
	conn_info->resp_ie_len = 0;

	if (req_len) {
		err = wl_dev_bufvar_get(ndev, "assoc_req_ies", wl->extra_buf, WL_EXTRA_BUF_MAX);
		if (err) {
			WL_ERR(("could not get assoc req (%d)\n", err));
			return err;
		}
		conn_info->req_ie = kmemdup(wl->extra_buf, req_len, GFP_KERNEL);
		conn_info->req_ie_len = conn_info->req_ie ? req_len : 0;
	}
	if (resp_len) {
		err = wl_dev_bufvar_get(ndev, "assoc_resp_ies", wl->extra_buf, WL_EXTRA_BUF_MAX);
		if (err) {
			WL_ERR(("could not get assoc resp (%d)\n", err));
			return err;
		}
		conn_info->resp_ie = kmemdup(wl->extra_buf, resp_len, GFP_KERNEL);
		conn_info->resp_ie_len = conn_info->resp_ie ? resp_len : 0;
	}
	WL_DBG(("req len (%d) resp len (%d)\n", conn_info->req_ie_len,
		conn_info->resp_ie_len));

	return err;
}

static void wl_ch_to_chanspec(struct ieee80211_channel *chan, struct wl_join_params *join_params,
	size_t *join_params_size)
{
	chanspec_t chanspec = 0;

	if (chan) {
		join_params->params.chanspec_num = 1;
		join_params->params.chanspec_list[0] =
		    ieee80211_frequency_to_channel(chan->center_freq);

		if (chan->band == NL80211_BAND_2GHZ) {
			chanspec |= WL_CHANSPEC_BAND_2G;
		}
		else if (chan->band == NL80211_BAND_5GHZ) {
			chanspec |= WL_CHANSPEC_BAND_5G;
		}
		else {
			WL_ERR(("Unknown band\n"));
			BUG();
		}

		chanspec |= WL_CHANSPEC_BW_20;

		*join_params_size += WL_ASSOC_PARAMS_FIXED_SIZE +
			join_params->params.chanspec_num * sizeof(chanspec_t);

		join_params->params.chanspec_list[0] &= WL_CHANSPEC_CHAN_MASK;
		join_params->params.chanspec_list[0] |= chanspec;
		join_params->params.chanspec_list[0] =
		    htodchanspec(join_params->params.chanspec_list[0]);

		join_params->params.chanspec_num = htod32(join_params->params.chanspec_num);

		WL_DBG(("join_params->params.chanspec_list[0]= %#X, channel %d, chanspec %#X\n",
		        join_params->params.chanspec_list[0],
		        join_params->params.chanspec_list[0], chanspec));
	}
}

/*
 * Make sure cfg80211 has a fresh entry for the BSS we are associated with
 * (wl->bssid) and, if @bss_out is non-NULL, return a reference to it that the
 * caller must pass on to cfg80211 or release with cfg80211_put_bss().
 *
 * The lookups used to pass WLAN_CAPABILITY_ESS where cfg80211_get_bss() has
 * taken enum ieee80211_bss_type / enum ieee80211_privacy since Linux 4.1, so
 * they searched for a 60 GHz PBSS with privacy off and never matched a normal
 * access point. The connect/roam report then left the lookup to cfg80211,
 * whose scan entries expire after 30 s; when that failed cfg80211 hit its
 * "bss_not_found" WARN_ON and discarded the connection or roam result.
 */
static s32 wl_update_bss_info(struct wl_cfg80211_priv *wl, struct cfg80211_bss **bss_out)
{
	struct wiphy *wiphy = wl_to_wiphy(wl);
	struct wlc_ssid *ssid = &wl->profile->ssid;
	enum ieee80211_bss_type bss_type = wl_is_ibssmode(wl) ?
		IEEE80211_BSS_TYPE_IBSS : IEEE80211_BSS_TYPE_ESS;
	struct cfg80211_bss *bss = NULL;
	struct wl_bss_info *bi;
	s32 err;

	rtnl_lock();
	/* Always refresh from the firmware's view of the current BSS. */
	*(u32 *) wl->extra_buf = htod32(WL_EXTRA_BUF_MAX);
	err = wl_dev_ioctl(wl_to_ndev(wl), WLC_GET_BSS_INFO, wl->extra_buf,
	                   WL_EXTRA_BUF_MAX);
	if (err) {
		WL_ERR(("Could not get bss info %d\n", err));
	} else {
		bi = (struct wl_bss_info *)(wl->extra_buf + 4);
		if (dtoh32(bi->length) > WL_EXTRA_BUF_MAX - 4)
			WL_ERR(("bss info too long (%u)\n", dtoh32(bi->length)));
		else if (memcmp(&bi->BSSID, &wl->bssid, ETHER_ADDR_LEN))
			WL_DBG(("firmware BSS %pM is not %pM\n", &bi->BSSID, &wl->bssid));
		else
			err = wl_inform_single_bss(wl, bi, &bss);
	}

	if (!bss)
		bss = cfg80211_get_bss(wiphy, NULL, (const u8 *)&wl->bssid,
		                       ssid->SSID, ssid->SSID_len, bss_type,
		                       IEEE80211_PRIVACY_ANY);
	/* hidden networks beacon an empty SSID: match on the BSSID alone */
	if (!bss)
		bss = cfg80211_get_bss(wiphy, NULL, (const u8 *)&wl->bssid,
		                       NULL, 0, bss_type, IEEE80211_PRIVACY_ANY);
	rtnl_unlock();

	if (!bss) {
		WL_ERR(("no BSS entry for %pM\n", &wl->bssid));
		return err ? err : -ENOENT;
	}

	wl->conf->channel = *bss->channel;
	if (bss_out)
		*bss_out = bss;
	else
		cfg80211_put_bss(wiphy, bss);

	return 0;
}

static s32
wl_bss_roaming_done(struct wl_cfg80211_priv *wl, struct net_device *ndev,
                    const wl_event_msg_t *e, void *data)
{
	struct wl_cfg80211_connect_info *conn_info = wl_to_conn(wl);
	struct cfg80211_bss *bss = NULL;
	s32 err = 0;

	err = wl_get_assoc_ies(wl);
	if (err)
		return err;

	memcpy(wl->profile->bssid, &e->addr, ETHER_ADDR_LEN);
	memcpy(&wl->bssid, &e->addr, ETHER_ADDR_LEN);

	/*
	 * The firmware has already moved to the new AP, so the roam is always
	 * reported. Handing over the BSS entry keeps cfg80211 from dropping it
	 * when its own lookup fails; without an entry it still tries the BSSID.
	 */
	wl_update_bss_info(wl, &bss);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)
	{
		/* Built only now: the IEs were refetched just above. */
		struct cfg80211_roam_info roam_info = {
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 0, 0)
			.bssid = wl->profile->bssid,
			.bss = bss,
#else
			.links[0].bssid = wl->profile->bssid,
			.links[0].bss = bss,
#endif
			.req_ie = conn_info->req_ie,
			.req_ie_len = conn_info->req_ie_len,
			.resp_ie = conn_info->resp_ie,
			.resp_ie_len = conn_info->resp_ie_len,
		};

		cfg80211_roamed(ndev, &roam_info, GFP_KERNEL);
	}
#else
	if (bss)
		cfg80211_put_bss(wl_to_wiphy(wl), bss);
	cfg80211_roamed(ndev,
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39)
			&wl->conf->channel,
#endif
			(u8 *)&wl->bssid,
			conn_info->req_ie, conn_info->req_ie_len,
			conn_info->resp_ie, conn_info->resp_ie_len,
			GFP_KERNEL);
#endif
	WL_DBG(("Report roaming result\n"));

	set_bit(WL_STATUS_CONNECTED, &wl->status);

	return err;
}

static s32
wl_bss_connect_done(struct wl_cfg80211_priv *wl, struct net_device *ndev,
                    const wl_event_msg_t *e, void *data, bool completed)
{
	struct wl_cfg80211_connect_info *conn_info = wl_to_conn(wl);
	s32 err = 0;

	if (wl->scan_request) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
		struct cfg80211_scan_info info = {
			.aborted = true,
		};
		WL_DBG(("%s: Aborting scan\n", __FUNCTION__));
		cfg80211_scan_done(wl->scan_request, &info);
#else
		WL_DBG(("%s: Aborting scan\n", __FUNCTION__));
		cfg80211_scan_done(wl->scan_request, true);
#endif
		wl->scan_request = NULL;
	}

	if (test_and_clear_bit(WL_STATUS_CONNECTING, &wl->status)) {
		struct cfg80211_bss *bss = NULL;

		if (completed) {
			wl_get_assoc_ies(wl);
			memcpy(&wl->bssid, &e->addr, ETHER_ADDR_LEN);
			memcpy(wl->profile->bssid, &e->addr, ETHER_ADDR_LEN);
			wl_update_bss_info(wl, &bss);
			set_bit(WL_STATUS_CONNECTED, &wl->status);
		}

		WL_DBG(("Reporting BSS network join result \"%s\"\n",
			wl->profile->ssid.SSID));
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
		/* Hand over the entry (and our reference) instead of a bare BSSID. */
		cfg80211_connect_bss(ndev, (u8 *)&wl->bssid, bss, conn_info->req_ie,
		    conn_info->req_ie_len, conn_info->resp_ie, conn_info->resp_ie_len,
		    completed ? WLAN_STATUS_SUCCESS : WLAN_STATUS_AUTH_TIMEOUT, GFP_KERNEL,
		    NL80211_TIMEOUT_UNSPECIFIED);
#else
		if (bss)
			cfg80211_put_bss(wl_to_wiphy(wl), bss);
		cfg80211_connect_result(ndev, (u8 *)&wl->bssid,	conn_info->req_ie,
		    conn_info->req_ie_len, conn_info->resp_ie, conn_info->resp_ie_len,
		    completed ? WLAN_STATUS_SUCCESS : WLAN_STATUS_AUTH_TIMEOUT,	GFP_KERNEL);
#endif
		WL_DBG(("Connection %s\n", completed ? "Succeeded" : "FAILed"));
	}

	return err;
}

static s32
wl_notify_mic_status(struct wl_cfg80211_priv *wl, struct net_device *ndev,
                     const wl_event_msg_t *e, void *data)
{
	u16 flags = EVENT_FLAGS(e);
	enum nl80211_key_type key_type;

	WL_DBG(("\n"));

	rtnl_lock();
	if (flags & WLC_EVENT_MSG_GROUP)
		key_type = NL80211_KEYTYPE_GROUP;
	else
		key_type = NL80211_KEYTYPE_PAIRWISE;

	cfg80211_michael_mic_failure(ndev, (u8 *)&e->addr, key_type, -1, NULL, GFP_KERNEL);
	rtnl_unlock();

	return 0;
}

static s32
wl_notify_scan_status(struct wl_cfg80211_priv *wl, struct net_device *ndev,
                      const wl_event_msg_t *e, void *data)
{
	struct channel_info channel_inform;
	struct wl_scan_results *bss_list;
	u32 buflen;
	int bcmerr;
	s32 err = 0;

	WL_DBG(("\n"));

	rtnl_lock();
	err = wl_dev_ioctl(ndev, WLC_GET_CHANNEL, &channel_inform, sizeof(channel_inform));
	if (err) {
		WL_ERR(("scan busy (%d)\n", err));
		goto scan_done_out;
	}
	channel_inform.scan_channel = dtoh32(channel_inform.scan_channel);
	if (channel_inform.scan_channel) {

		WL_DBG(("channel_inform.scan_channel (%d)\n",	channel_inform.scan_channel));
	}

	for (buflen = WL_SCAN_BUF_BASE; ; ) {
		bss_list = kvzalloc(buflen, GFP_KERNEL);
		if (!bss_list) {
			WL_ERR(("%s Out of memory for scan results\n", ndev->name));
			err = -ENOMEM;
			goto scan_done_out;
		}
		bss_list->buflen = htod32(buflen);
		err = wl_dev_ioctl_bcmerr(ndev, WLC_SCAN_RESULTS, bss_list, buflen, &bcmerr);
		if (!err)
			break;
		kvfree(bss_list);
		/* grow on "buffer too short", but not without bound */
		if (err != -E2BIG || buflen >= WL_SCAN_BUF_MAX) {
			/*
			 * A scan cut short, e.g. by NetworkManager disconnecting
			 * before suspend, has no results (BCME_NOTFOUND).
			 */
			if (wl_bcmerr_is_transient(bcmerr) || bcmerr == BCME_NOTFOUND)
				WL_INF(("%s scan aborted, results unavailable (bcmerror %d)\n",
					ndev->name, bcmerr));
			else
				WL_ERR(("%s Scan_results error (%d, bcmerror %d)\n",
					ndev->name, err, bcmerr));
			goto scan_done_out;
		}
		buflen *= 2;
	}

	bss_list->buflen = dtoh32(bss_list->buflen);
	bss_list->version = dtoh32(bss_list->version);
	bss_list->count = dtoh32(bss_list->count);

	err = wl_inform_bss(wl, bss_list, buflen);
	kvfree(bss_list);

scan_done_out:
	if (wl->scan_request) {
		/* Results that never reached cfg80211 make this an aborted scan. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
		struct cfg80211_scan_info info = {
			.aborted = err != 0,
		};
		cfg80211_scan_done(wl->scan_request, &info);
#else
		cfg80211_scan_done(wl->scan_request, err != 0);
#endif
		wl->scan_request = NULL;
	}
	rtnl_unlock();
	return err;
}

static void wl_init_conf(struct wl_cfg80211_conf *conf)
{
	conf->mode = (u32)-1;
	conf->frag_threshold = (u32)-1;
	conf->rts_threshold = (u32)-1;
	conf->retry_short = (u32)-1;
	conf->retry_long = (u32)-1;
	conf->tx_power = -1;
}

static void wl_init_prof(struct wl_cfg80211_profile *prof)
{
	memset(prof, 0, sizeof(*prof));
}

static void wl_init_eloop_handler(struct wl_cfg80211_event_loop *el)
{
	memset(el, 0, sizeof(*el));
	el->handler[WLC_E_SCAN_COMPLETE] = wl_notify_scan_status;
	el->handler[WLC_E_JOIN] = wl_notify_connect_status;
	el->handler[WLC_E_START] = wl_notify_connect_status;
	el->handler[WLC_E_LINK] = wl_notify_connect_status;
	el->handler[WLC_E_NDIS_LINK] = wl_notify_connect_status;
	el->handler[WLC_E_SET_SSID] = wl_notify_connect_status;
	el->handler[WLC_E_DISASSOC_IND] = wl_notify_connect_status;
	el->handler[WLC_E_DEAUTH_IND] = wl_notify_connect_status;
	el->handler[WLC_E_ROAM] = wl_notify_roaming_status;
	el->handler[WLC_E_MIC_ERROR] = wl_notify_mic_status;
}

static s32 wl_init_priv_mem(struct wl_cfg80211_priv *wl)
{
	wl->conf = (void *)kzalloc(sizeof(*wl->conf), GFP_KERNEL);
	if (!wl->conf) {
		WL_ERR(("wl_cfg80211_conf alloc failed\n"));
		goto init_priv_mem_out;
	}
	wl->profile = (void *)kzalloc(sizeof(*wl->profile), GFP_KERNEL);
	if (!wl->profile) {
		WL_ERR(("wl_cfg80211_profile alloc failed\n"));
		goto init_priv_mem_out;
	}
	wl->scan_req_int = (void *)kzalloc(sizeof(*wl->scan_req_int), GFP_KERNEL);
	if (!wl->scan_req_int) {
		WL_ERR(("Scan req alloc failed\n"));
		goto init_priv_mem_out;
	}
	wl->ioctl_buf = (void *)kzalloc(WL_IOCTL_LEN_MAX, GFP_KERNEL);
	if (!wl->ioctl_buf) {
		WL_ERR(("Ioctl buf alloc failed\n"));
		goto init_priv_mem_out;
	}
	wl->extra_buf = (void *)kzalloc(WL_EXTRA_BUF_MAX, GFP_KERNEL);
	if (!wl->extra_buf) {
		WL_ERR(("Extra buf alloc failed\n"));
		goto init_priv_mem_out;
	}

	wl->pmk_list = (void *)kzalloc(sizeof(*wl->pmk_list), GFP_KERNEL);
	if (!wl->pmk_list) {
		WL_ERR(("pmk list alloc failed\n"));
		goto init_priv_mem_out;
	}

	return 0;

init_priv_mem_out:
	wl_deinit_priv_mem(wl);

	return -ENOMEM;
}

static void wl_deinit_priv_mem(struct wl_cfg80211_priv *wl)
{
	kfree(wl->conf);
	wl->conf = NULL;
	kfree(wl->profile);
	wl->profile = NULL;
	kfree(wl->scan_req_int);
	wl->scan_req_int = NULL;
	kfree(wl->ioctl_buf);
	wl->ioctl_buf = NULL;
	kfree(wl->extra_buf);
	wl->extra_buf = NULL;
	kfree(wl->pmk_list);
	wl->pmk_list = NULL;
}

static s32 wl_create_event_handler(struct wl_cfg80211_priv *wl)
{
	sema_init(&wl->event_sync, 0);
	wl->event_tsk = kthread_run(wl_event_handler, wl, "wl_event_handler");
	if (IS_ERR(wl->event_tsk)) {
		wl->event_tsk = NULL;
		WL_ERR(("failed to create event thread\n"));
		return -ENOMEM;
	}
	return 0;
}

static void wl_destroy_event_handler(struct wl_cfg80211_priv *wl)
{
	if (wl->event_tsk) {
		send_sig(SIGTERM, wl->event_tsk, 1);
		kthread_stop(wl->event_tsk);
		wl->event_tsk = NULL;
	}
}

static s32 wl_init_cfg80211_priv(struct wl_cfg80211_priv *wl, struct wireless_dev *wdev)
{
	s32 err = 0;

	wl->wdev = wdev;

	wl->scan_request = NULL;
	wl->active_scan = true;
	wl_init_eq(wl);
	err = wl_init_priv_mem(wl);
	if (err)
		return err;

	if (wl_create_event_handler(wl))
		return -ENOMEM;

	wl_init_eloop_handler(&wl->el);

	if (err)
		return err;

	wl_init_conf(wl->conf);
	wl_init_prof(wl->profile);
	wl_link_down(wl);

	return err;
}

static void wl_deinit_cfg80211_priv(struct wl_cfg80211_priv *wl)
{
	wl_destroy_event_handler(wl);
	wl_flush_eq(wl);
	wl_link_down(wl);
	wl_deinit_priv_mem(wl);
}

s32 wl_cfg80211_attach(struct net_device *ndev, struct device *dev, int passive)
{
	struct wireless_dev *wdev;
	struct wl_cfg80211_priv *wl;
	s32 err = 0;

	if (!ndev) {
		WL_ERR(("ndev is invaild\n"));
		return -ENODEV;
	}

	err = wl_alloc_wdev(dev, &wdev);
	if (err < 0) {
		return err;
	}

	wdev->iftype = wl_mode_to_nl80211_iftype(WL_MODE_BSS);
	wl = wdev_to_wl(wdev);
	ndev->ieee80211_ptr = wdev;
	SET_NETDEV_DEV(ndev, wiphy_dev(wdev->wiphy));
	wdev->netdev = ndev;
	err = wl_init_cfg80211_priv(wl, wdev);
	if (err) {
		WL_ERR(("Failed to init iwm_priv (%d)\n", err));
		goto cfg80211_attach_out;
	}
	wl->passive = !!passive;

	if (!err) {
		WL_INF(("Registered CFG80211 phy\n"));
	}
	return err;

cfg80211_attach_out:
	wl_free_wdev(wl);
	return err;
}

void wl_cfg80211_detach(struct net_device *ndev)
{
	struct wl_cfg80211_priv *wl;

	if (ndev->ieee80211_ptr == NULL) {
                WL_ERR(( "NULL ndev->ieee80211ptr, unable to deref wl\n"));
                return;
        }
	wl = ndev_to_wl(ndev);

	wl_deinit_cfg80211_priv(wl);
	wl_free_wdev(wl);
}

static void wl_wakeup_event(struct wl_cfg80211_priv *wl)
{
	up(&wl->event_sync);
}

static s32 wl_event_handler(void *data)
{
	struct wl_cfg80211_priv *wl = (struct wl_cfg80211_priv *)data;
	struct wl_cfg80211_event_q *e;

	allow_signal(SIGTERM);
	while (!down_interruptible(&wl->event_sync)) {
		if (kthread_should_stop())
			break;
		e = wl_deq_event(wl);
		if (!e) {
			WL_ERR(("eqeue empty..\n"));
			BUG();
		}
		if (wl->el.handler[e->etype]) {
			WL_DBG(("event type (%d)\n", e->etype));
			wl->el.handler[e->etype] (wl, wl_to_ndev(wl), &e->emsg, e->edata);
		} else {
			WL_DBG(("Unknown Event (%d): ignoring\n", e->etype));
		}
		wl_put_event(e);
	}
	WL_DBG(("%s was terminated\n", __func__));
	return 0;
}

void
wl_cfg80211_event(struct net_device *ndev, const wl_event_msg_t * e, void *data)
{

	u32 event_type = EVENT_TYPE(e);

	struct wl_cfg80211_priv *wl = ndev_to_wl(ndev);
#if defined(WL_DBGMSG_ENABLE)
	s8 *estr = (event_type <= sizeof(wl_dbg_estr) / WL_DBG_ESTR_MAX - 1) ?
	    wl_dbg_estr[event_type] : (s8 *) "Unknown";
	WL_DBG(("event_type (%d):" "WLC_E_" "%s\n", event_type, estr));
#endif				
	if (!wl_enq_event(wl, event_type, e, data))
		wl_wakeup_event(wl);
}

static void wl_init_eq(struct wl_cfg80211_priv *wl)
{
	wl_init_eq_lock(wl);
	INIT_LIST_HEAD(&wl->eq_list);
}

static void wl_flush_eq(struct wl_cfg80211_priv *wl)
{
	struct wl_cfg80211_event_q *e;

	wl_lock_eq(wl);
	while (!list_empty(&wl->eq_list)) {
		e = list_first_entry(&wl->eq_list, struct wl_cfg80211_event_q, eq_list);
		list_del(&e->eq_list);
		kfree(e);
	}
	wl_unlock_eq(wl);
}

static struct wl_cfg80211_event_q *wl_deq_event(struct wl_cfg80211_priv *wl)
{
	struct wl_cfg80211_event_q *e = NULL;

	wl_lock_eq(wl);
	if (!list_empty(&wl->eq_list)) {
		e = list_first_entry(&wl->eq_list, struct wl_cfg80211_event_q, eq_list);
		list_del(&e->eq_list);
	}
	wl_unlock_eq(wl);

	return e;
}

static s32
wl_enq_event(struct wl_cfg80211_priv *wl, u32 event, const wl_event_msg_t *msg, void *data)
{
	struct wl_cfg80211_event_q *e;
	s32 err = 0;

	e = kzalloc(sizeof(struct wl_cfg80211_event_q), GFP_ATOMIC);
	if (!e) {
		WL_ERR(("event alloc failed\n"));
		return -ENOMEM;
	}

	e->etype = event;
	memcpy(&e->emsg, msg, sizeof(wl_event_msg_t));
	if (data) {
	}

	spin_lock(&wl->eq_lock);
	list_add_tail(&e->eq_list, &wl->eq_list);
	spin_unlock(&wl->eq_lock);

	return err;
}

static void wl_put_event(struct wl_cfg80211_event_q *e)
{
	kfree(e);
}

static s32 wl_set_mode(struct net_device *ndev, s32 iftype)
{
	s32 infra = 0;
	s32 ap = 0;
	s32 err = 0;

	switch (iftype) {
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_WDS:
		WL_ERR(("type (%d) : currently we do not support this mode\n",
			iftype));
		err = -EINVAL;
		return err;
	case NL80211_IFTYPE_ADHOC:
		break;
	case NL80211_IFTYPE_STATION:
		infra = 1;
		break;
	default:
		err = -EINVAL;
		WL_ERR(("invalid type (%d)\n", iftype));
		return err;
	}
	infra = htod32(infra);
	ap = htod32(ap);
	WL_DBG(("%s ap (%d), infra (%d)\n", ndev->name, ap, infra));
	err = wl_dev_ioctl(ndev, WLC_SET_INFRA, &infra, sizeof(infra));
	if (err) {
		WL_ERR(("WLC_SET_INFRA error (%d)\n", err));
		return err;
	}
	err = wl_dev_ioctl(ndev, WLC_SET_AP, &ap, sizeof(ap));
	if (err) {
		WL_ERR(("WLC_SET_AP error (%d)\n", err));
		return err;
	}

	return 0;
}

static void wl_update_wowl(struct net_device *ndev)
{
#ifdef CONFIG_PM
	struct wl_cfg80211_priv *wl = ndev_to_wl(ndev);
	struct wireless_dev *wdev = ndev->ieee80211_ptr;
	s32 offloads = 0;
	s32 err = 0;
	err = wl_dev_bufvar_get(wl_to_ndev(wl), "offloads",
		(s8 *)&offloads, sizeof(offloads));
	if (err == 0 && offloads == 1) {
		WL_INF(("Supports offloads\n"));
		wl->offloads = true;
	} else {
		WL_INF(("No offloads supported\n"));
		wl->offloads = false;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
		wdev->wiphy->wowlan = NULL;
#else
		memset(&wdev->wiphy->wowlan, 0, sizeof(struct wiphy_wowlan_support));
#endif 
#endif 
	}
#endif 
}

static s32 wl_update_wiphybands(struct wl_cfg80211_priv *wl)
{
	struct wiphy *wiphy;
	s32 phy_list;
	s8 phy;
	s32 err = 0;

	err = wl_dev_ioctl(wl_to_ndev(wl), WLC_GET_PHYLIST, &phy_list, sizeof(phy_list));
	if (err) {
		WL_ERR(("error (%d)\n", err));
		return err;
	}

	phy = ((char *)&phy_list)[0];
	WL_DBG(("%c phy\n", phy));

	if (phy == 'n' || phy == 'a' || phy == 'v') {
		wiphy = wl_to_wiphy(wl);
		wiphy->bands[NL80211_BAND_5GHZ] = &__wl_band_5ghz_n;
	}

	return err;
}

s32 wl_cfg80211_up(struct net_device *ndev)
{
	struct wl_cfg80211_priv *wl = ndev_to_wl(ndev);
	s32 err = 0;
	struct wireless_dev *wdev = ndev->ieee80211_ptr;

	wl_set_mode(ndev, wdev->iftype);
	err = wl_update_wiphybands(wl);
	if (err) {
		return err;
	} 

	wl_update_wowl(ndev);
	return 0;
}

s32 wl_cfg80211_down(struct net_device *ndev)
{
	struct wl_cfg80211_priv *wl = ndev_to_wl(ndev);
	s32 err = 0;

	if (wl->scan_request) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
		struct cfg80211_scan_info info = {
			.aborted = true,
		};
		cfg80211_scan_done(wl->scan_request, &info);
#else
		cfg80211_scan_done(wl->scan_request, true);	
#endif
		wl->scan_request = NULL;
	}

	return err;
}

static bool wl_is_ibssmode(struct wl_cfg80211_priv *wl)
{
	return wl->conf->mode == WL_MODE_IBSS;
}

static __used s32 wl_add_ie(struct wl_cfg80211_priv *wl, u8 t, u8 l, u8 *v)
{
	struct wl_cfg80211_ie *ie = wl_to_ie(wl);
	s32 err = 0;

	if (ie->offset + l + 2 > WL_TLV_INFO_MAX) {
		WL_ERR(("ei crosses buffer boundary\n"));
		return -ENOSPC;
	}
	ie->buf[ie->offset] = t;
	ie->buf[ie->offset + 1] = l;
	memcpy(&ie->buf[ie->offset + 2], v, l);
	ie->offset += l + 2;

	return err;
}


static void wl_link_up(struct wl_cfg80211_priv *wl)
{
	WL_DBG(("\n"));
}

static void wl_link_down(struct wl_cfg80211_priv *wl)
{
	struct wl_cfg80211_connect_info *conn_info = wl_to_conn(wl);

	WL_DBG(("\n"));

	kfree(conn_info->req_ie);
	conn_info->req_ie = NULL;
	conn_info->req_ie_len = 0;
	kfree(conn_info->resp_ie);
	conn_info->resp_ie = NULL;
	conn_info->resp_ie_len = 0;
}

static void wl_lock_eq(struct wl_cfg80211_priv *wl)
{
	spin_lock_irq(&wl->eq_lock);
}

static void wl_unlock_eq(struct wl_cfg80211_priv *wl)
{
	spin_unlock_irq(&wl->eq_lock);
}

static void wl_init_eq_lock(struct wl_cfg80211_priv *wl)
{
	spin_lock_init(&wl->eq_lock);
}

#endif 
