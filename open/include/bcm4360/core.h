/*
 * The core of the open driver (docs/re/07-open-driver.md): what Broadcom's
 * upper layer did to the hardware, without the upper layer. It joins the
 * open layers (chip, MAC, PHY, DMA, regulatory lookup) into one device and
 * gives the platform - the Linux mac80211 driver in drv/, or the emulator -
 * a small interface: attach, up, down, channel, periodic work, interrupts,
 * frames.
 *
 * Specifications: docs/re/spec/wlc-hw.md (the upper layer's hardware writes),
 * docs/re/spec/frames.md (receive and transmit headers, transmit status,
 * interrupts), docs/re/spec/chip-attach.md (backplane enumeration, OTP, PCIe),
 * docs/re/spec/channel-power.md (power limits per channel), and the
 * specifications of the open layers below.
 *
 * Calls into the core are serialised by the platform (one lock), except
 * bcm4360_core_isr(), which may run concurrently in interrupt context and only
 * touches the interrupt registers.
 */
#ifndef BCM4360_CORE_H
#define BCM4360_CORE_H

#include <bcm4360/types.h>
#include <bcm4360/hw.h>

struct bcm4360_core;

/* What attach found out about the card. */
struct bcm4360_core_info {
	u8  etheraddr[6];	/* the card's MAC address (SROM) */
	u8  bands;		/* bit 0: 2.4 GHz, bit 1: 5 GHz */
	u8  txchain, rxchain;	/* bit masks of the transmit / receive chains */
	u16 chip_id, chip_rev;
	u16 boardvendor, boardtype;
	char ccode[4];		/* the SROM's country code */
};

/* The receive status of one frame, from the receive header (frames.md). */
struct bcm4360_rxstatus {
	u64 tsf;		/* MAC time of the frame's start (64-bit TSF) */
	u16 chanspec;		/* the channel it was received on */
	s8  rssi;		/* dBm; 0: unknown */
	s8  chain_rssi[3];	/* dBm per receive chain; 0: unknown */
	u8  enc;		/* rate encoding: 0 legacy, 1 HT, 2 VHT */
	u8  rate;		/* legacy: 500 kb/s units; HT: MCS; VHT: MCS */
	u8  nss;		/* VHT: spatial streams */
	u8  bw;			/* 0: 20, 1: 40, 2: 80 MHz */
	u8  flags;		/* BCM4360_RX_* */
	u8  key;		/* with BCM4360_RX_DECRYPTED / _DECRYPT_ERR: the key entry used
				 * (ps-keys.md 2.5: RxStatus1 bits 5..10) */
};

#define BCM4360_RX_SGI		0x01	/* short guard interval */
#define BCM4360_RX_LDPC		0x02
#define BCM4360_RX_STBC		0x04
#define BCM4360_RX_SHORTPRE	0x08	/* short preamble (CCK) */
#define BCM4360_RX_FCS_FAIL	0x10	/* frame check sequence wrong */
#define BCM4360_RX_DECRYPTED	0x20	/* the hardware decrypted it */
#define BCM4360_RX_AMPDU	0x40	/* part of an A-MPDU */
/* the hardware tried to decrypt and the ICV (or, with hardware MIC keys, the TKIP MIC) check
 * failed (ps-keys.md 2.5: RxStatus1 bits 3 and 4; the frame is garbage, the platform drops it).
 * BCM4360_RX_DECRYPTED is not set with it. */
#define BCM4360_RX_DECRYPT_ERR	0x80

/* What the platform asks for when it transmits a frame. */
struct bcm4360_txrate {
	u8  enc;		/* 0 legacy, 1 HT, 2 VHT */
	u8  rate;		/* legacy: 500 kb/s units; HT, VHT: MCS */
	u8  nss;		/* VHT */
	u8  bw;			/* 0: 20, 1: 40, 2: 80 MHz */
	u8  flags;		/* BCM4360_TX_SGI, _LDPC, _STBC, _SHORTPRE */
	u8  tries;		/* attempts at this rate */
};

#define BCM4360_TX_SGI		0x01
#define BCM4360_TX_LDPC		0x02
#define BCM4360_TX_STBC		0x04
#define BCM4360_TX_SHORTPRE	0x08

struct bcm4360_txparams {
	u8  fifo;		/* transmit FIFO (access category, frames.md) */
	u8  nrates;		/* 1..4 */
	struct bcm4360_txrate rates[4];
	u8  no_ack;		/* do not expect an acknowledgement */
	u8  rts;		/* protect with RTS/CTS */
	u8  cts;		/* protect with CTS-to-self */
	u8  seq_by_hw;		/* let the hardware fill in the sequence number (not for ampdu) */
	/* A-MPDU (docs/re/spec/ampdu.md 1.4-1.6) */
	u8  ampdu;		/* the frame is an MPDU of a Block Ack session (mac80211's
				 * IEEE80211_TX_CTL_AMPDU); it must carry the sequence number
				 * the platform assigned (in its sequence control field) */
	u8  tid;		/* the TID of the session; valid with ampdu */
	u8  peer;		/* the platform's index of the peer (< BCM4360_AMPDU_NPEERS),
				 * as given to bcm4360_core_ampdu_tx_start(); valid with ampdu */
	/* hardware encryption (docs/re/spec/ps-keys.md 2.4). The platform has built the
	 * Protected bit and the IV (the CCMP / TKIP header after the 802.11 header and QoS
	 * control) and appended the TKIP MIC; the core's engine encrypts and appends the
	 * ICV / CCMP MIC. 0: the frame goes as it is. */
	u8  hw_key;		/* the key entry returned by bcm4360_core_set_key(), plus 1 */
	u16 tkip_p1k[5];	/* TKIP: the transmit phase-1 key for this frame's IV32 */
};

#define BCM4360_AMPDU_NPEERS	8

/* The transmit status of one frame (frames.md). */
struct bcm4360_txstatus {
	u8  acked;
	u8  rate_idx;		/* index into the frame's rates[] that went out last */
	u8  tries;		/* attempts in all */
	u8  ampdu;		/* reported as part of an A-MPDU */
	/* only with ampdu (ampdu.md 2.6). The aggregate's status is reported once per MPDU, in
	 * posting order; `acked` is that MPDU's Block Ack bit. The first MPDU of an aggregate
	 * carries the aggregate: ampdu_len (the MPDUs it completes) and ampdu_ack_len (how many
	 * were acknowledged), tries and rate_idx (1 try at rate 0, plus 1 at rate_idx 1 when
	 * MPDUs were also sent at the fallback rates). The others have ampdu_len 0 and tries 0:
	 * they only carry `acked`. */
	u8  ampdu_len;
	u8  ampdu_ack_len;
};

/* Receive filters (BCM4360_FILTER_*): what the MAC passes up. */
#define BCM4360_FILTER_PROMISC		0x01	/* frames to other addresses */
#define BCM4360_FILTER_BEACONS		0x02	/* beacons of other networks */
#define BCM4360_FILTER_CONTROL		0x04	/* control frames */
#define BCM4360_FILTER_FCSFAIL		0x08	/* frames with a wrong FCS */
#define BCM4360_FILTER_PROBE_RESP	0x10	/* probe responses to others */

/* The BSS the station is in (wlc-hw.md, "Joining"). */
struct bcm4360_bss {
	u8  bssid[6];
	u8  associated;
	u16 aid;
	u16 beacon_int;		/* TU */
	u8  dtim_period;
	u8  short_slot;
	u8  short_preamble;
	u8  cts_prot;		/* ERP protection */
	u64 tsf;		/* the TSF of the last beacon, for the TBTT */
	u16 basic_rates;	/* bit i: legacy rate i of the band is basic */
};

/* Attach: find the card's cores, read the SROM, attach the open layers. The
 * platform has mapped BAR0 into hw and filled in pdev. NULL on failure, *err
 * says why. */
struct bcm4360_core *bcm4360_core_attach(struct bcm4360_hw *hw, void *osh, u16 vendor,
					 u16 device, void *plat, int *err);
void bcm4360_core_detach(struct bcm4360_core *c);
void bcm4360_core_get_info(struct bcm4360_core *c, struct bcm4360_core_info *info);

/* Up: power up, download the microcode, initialise, set the channel, enable
 * the MAC and the interrupts. Down: the reverse. */
int bcm4360_core_up(struct bcm4360_core *c, u16 chanspec);
void bcm4360_core_down(struct bcm4360_core *c);
/* system suspend, after bcm4360_core_down: the next bcm4360_core_up powers
 * the hardware up again as after attach (open/core/pm.c) */
void bcm4360_core_suspend(struct bcm4360_core *c);

int bcm4360_core_set_chanspec(struct bcm4360_core *c, u16 chanspec);
/* the transmit power the platform allows, quarter dBm; 0: no limit of its own */
void bcm4360_core_set_txpower(struct bcm4360_core *c, u32 qdbm);
/* the country for the regulatory power limits (CLM), e.g. "DE" */
int bcm4360_core_set_country(struct bcm4360_core *c, const char *ccode);
/* The periodic work (the PHY's calibrations, interference mitigation,
 * counters) runs once a second while up, on a timer the core arms itself
 * through bcm4360_plat_timer_*(), as Broadcom's upper layer did. */

void bcm4360_core_set_macaddr(struct bcm4360_core *c, const u8 *addr);
void bcm4360_core_set_bss(struct bcm4360_core *c, const struct bcm4360_bss *bss);
void bcm4360_core_set_filter(struct bcm4360_core *c, u32 filter);
/* contention parameters of a transmit FIFO */
void bcm4360_core_set_edca(struct bcm4360_core *c, u8 fifo, u16 cwmin, u16 cwmax, u8 aifs,
			   u16 txop);
void bcm4360_core_set_scanning(struct bcm4360_core *c, bool on);
u64 bcm4360_core_get_tsf(struct bcm4360_core *c);

/* Interrupts. bcm4360_core_isr(): in interrupt context, reads and
 * acknowledges the MAC's interrupts and masks them; true if there is work
 * for bcm4360_core_dpc(), which then runs in process or softirq context:
 * receives frames (bcm4360_plat_rx()), handles transmit status
 * (bcm4360_plat_txstatus()), and unmasks the interrupts once done (returns
 * false) or returns true if the budget ran out. */
bool bcm4360_core_isr(struct bcm4360_core *c);
bool bcm4360_core_dpc(struct bcm4360_core *c, u32 budget);
void bcm4360_core_intrsoff(struct bcm4360_core *c);
void bcm4360_core_intrson(struct bcm4360_core *c);

/* Transmit one frame (an osl packet whose data begins with the 802.11
 * header; the core prepends its transmit header in the headroom). 0: taken;
 * 1: taken, and the FIFO is full now (the platform stops its queues until
 * bcm4360_plat_tx_wake()); -1: not taken (down, or no room), the platform
 * frees it. */
int bcm4360_core_tx(struct bcm4360_core *c, void *pkt, const struct bcm4360_txparams *p);
/* headroom the core needs in front of a frame to transmit */
u32 bcm4360_core_tx_headroom(struct bcm4360_core *c);
/* received frames dropped by the core, by reason (since the last up): too short for the
 * receive header, longer than its buffer, PHY error, FCS error, too short a frame, A-MSDU */
#define BCM4360_RX_DROP_REASONS	6
void bcm4360_core_rx_stats(struct bcm4360_core *c, u32 *frames, u32 *drop);

/* A-MPDU transmit sessions (docs/re/spec/ampdu.md 1, 3.1). The hardware needs nothing
 * for a session: these set what the core puts into the headers of the session's MPDUs.
 * `peer` is the platform's index of a station (< BCM4360_AMPDU_NPEERS).
 *  tx_start:       a session for (peer, tid) begins; the next MPDU's sequence number is
 *                  `ssn`; `max_mpdus`: the most MPDUs of the peer in one A-MPDU (ampdu.md
 *                  1.3: 32/48/64 by its spatial streams); `density`: the peer's minimum
 *                  MPDU start spacing code (802.11 A-MPDU density, 0..7), raised to the
 *                  own minimum (5). MPDUs are not aggregated yet.
 *  tx_operational: the peer has accepted; `buf_size`: its Block Ack window (1..64; 0: 64).
 *                  From here MPDUs with txparams.ampdu are aggregated (if they go out at
 *                  an HT or VHT rate).
 *  tx_stop:        the session is over; MPDUs already posted complete as usual, later
 *                  ones with txparams.ampdu go out as single frames. */
void bcm4360_core_ampdu_tx_start(struct bcm4360_core *c, u8 peer, u8 tid, u16 ssn,
				 u8 max_mpdus, u8 density);
void bcm4360_core_ampdu_tx_operational(struct bcm4360_core *c, u8 peer, u8 tid, u8 buf_size);
void bcm4360_core_ampdu_tx_stop(struct bcm4360_core *c, u8 peer, u8 tid);

/* Hardware keys (docs/re/spec/ps-keys.md 2). Cipher numbers are the hardware's. Entries 0..3
 * are the group keys by key id, 4..53 the pairwise keys (one per peer address).
 * set_key: `key_id` selects the entry of a group key (addr NULL); a pairwise key (addr is the
 *   peer) takes the peer's entry, else the first free one. WEP-40 / WEP-104: key 5 / 13 bytes.
 *   CCMP: 16 bytes. TKIP: 32 bytes in mac80211's order (temporal key 16, transmit MIC key 8,
 *   receive MIC key 8); `rx_p1k` is the receive phase-1 key for the peer's address and IV32
 *   `rx_iv32` (NULL: all zero and IV32 0, for a new key). TKIP group keys are not done by the
 *   hardware (-95). Returns the entry (>= 0), or a negative error: -22 bad arguments, -28 no
 *   free entry, -95 not supported (the platform does it in software). Writes the hardware
 *   at once while up; else when the core is brought up.
 * del_key: the entry is written empty.
 * tkip_rx_update: a new receive phase-1 key and IV32 for a TKIP pairwise entry.
 * key_state: for power save (ps-keys.md 1.2 step 4): `wpa` a key was set since the association,
 *   `group` a group key was set; power save is not entered between the two. */
#define BCM4360_NKEYS		54
#define BCM4360_CIPHER_WEP40	1
#define BCM4360_CIPHER_TKIP	2
#define BCM4360_CIPHER_WEP104	3
#define BCM4360_CIPHER_CCMP	5
int bcm4360_core_set_key(struct bcm4360_core *c, u8 cipher, u8 key_id, const u8 *addr,
			 const u8 *key, u8 keylen, const u16 *rx_p1k, u32 rx_iv32);
void bcm4360_core_del_key(struct bcm4360_core *c, u8 entry);
void bcm4360_core_tkip_rx_update(struct bcm4360_core *c, u8 entry, u32 iv32, const u16 *p1k);
void bcm4360_core_key_state(struct bcm4360_core *c, bool wpa, bool group);

/* Power save (docs/re/spec/ps-keys.md 1; IEEE80211_CONF_PS). The platform sends the null
 * frames and parses the TIM (mac80211's PS_NULLFUNC_STACK mode); the core follows them: a
 * null data frame to the BSSID with the PM bit set, passed to bcm4360_core_tx(), sets the
 * hardware's HPS bit (the microcode puts the PM bit on the air, the core sets none itself),
 * holds WAKE until that frame is acknowledged "PM indicated", and one without the PM bit
 * clears HPS. bcm4360_core_set_ps() does the same from the platform's configuration. */
void bcm4360_core_set_ps(struct bcm4360_core *c, bool ps);

/* Changes of the chains (docs/re/spec/coverage.md G5). txmask / rxmask: the bit masks of the
 * transmit / receive chains to use (a subset of what bcm4360_core_info reports); 0 leaves
 * that side as it is. Call while up (-1 otherwise), e.g. from mac80211's config callback
 * (IEEE80211_CONF_CHANGE_SMPS: rxmask 1 for static SM power save, the full mask otherwise;
 * mac80211 sends the action frame itself) or for a user's antenna selection
 * (set_antenna: both masks). 0: done; -29: refused (empty or outside the hardware's chains). */
int bcm4360_core_set_chains(struct bcm4360_core *c, u8 txmask, u8 rxmask);

/* The start of a join (docs/re/spec/coverage.md G7; mac80211 mgd_prepare_tx): the channel,
 * the response-rate map from the basic rates (bit i: legacy rate i of the band; 0: the mandatory
 * ones), a full calibration when channel or BSS are new, AMT[62] = BSSID (0x8002), the PHY held
 * and the MAC kept awake. Held until the association (bcm4360_core_set_bss with associated), or
 * bcm4360_core_join_end(), or 15 s. -19 if the core is not up. */
int bcm4360_core_join_start(struct bcm4360_core *c, u16 chanspec, const u8 *bssid,
			    u16 basic_rates, bool reassoc);
void bcm4360_core_join_end(struct bcm4360_core *c, bool success);

/* LEDs (G2): a frame was transmitted or delivered; GPIO 0 blinks 80/20 ms while they pass. */
void bcm4360_core_led_activity(struct bcm4360_core *c);

/* Radio switch, rfkill and MPC (G4). rfkill_poll: the state of the hardware switch (true: radio
 * disabled); reads the card when it is down and powered; the platform reports it with
 * wiphy_rfkill_set_hw_state(). The core calls bcm4360_plat_rfkill() (from its timers, in the
 * lock; it must not sleep) when the watchdog or the radio monitor sees the switch change.
 * radio_set: the software switch of Broadcom's glue (down with the radio monitor / up again
 * at once); mac80211's start/stop is that switch in the Linux driver, which does not call it.
 * mpc: the platform is idle (no association, no scan): the core goes down; not idle: it comes
 * up on `chanspec`. The caller (re)enables the interrupts after a down/up pair by
 * itself: the core does it. set_rssi: the RSSI of the associated AP for the coexistence
 * policy. */
bool bcm4360_core_rfkill_poll(struct bcm4360_core *c);
int bcm4360_core_radio_set(struct bcm4360_core *c, bool off);
int bcm4360_core_mpc(struct bcm4360_core *c, bool idle, u16 chanspec);
void bcm4360_core_set_rssi(struct bcm4360_core *c, s8 dbm);

/* Provided by the platform; `plat` as given to bcm4360_core_attach(). */
/* the hardware radio switch changed: true = radio disabled (WLC_E_RADIO); called with the lock */
void bcm4360_plat_rfkill(void *plat, bool hw_disabled);
void bcm4360_plat_rx(void *plat, void *pkt, const struct bcm4360_rxstatus *rs);
void bcm4360_plat_txstatus(void *plat, void *pkt, const struct bcm4360_txstatus *ts);
void bcm4360_plat_tx_wake(void *plat, u8 fifo);
/* a fatal error of the hardware: the platform restarts the device */
void bcm4360_plat_fatal(void *plat);
/* a step of bring-up or teardown, for the platform's trace (card debugging) */
void bcm4360_plat_trace(void *plat, const char *step);
/* an event worth logging always (the reason of a fatal error), with a value */
void bcm4360_plat_note(void *plat, const char *what, u32 val);
/* the same, logged only while the platform traces (card debugging) */
void bcm4360_plat_debug(void *plat, const char *what, u32 val);
/* Broadcom's regulatory database as clm_init() takes it (its clm_header,
 * spec clm.md), loaded from the firmware file; NULL if there is none. It
 * stays valid until bcm4360_core_detach() has returned. */
const void *bcm4360_plat_clm(void *plat);
/* Timers of the open layers (phy_env.h, bcm4360_timer_*): fn(arg) runs with
 * the platform's lock held, as every call into the core. */
void *bcm4360_plat_timer_init(void *plat, void (*fn)(void *arg), void *arg, const char *name);
void bcm4360_plat_timer_add(void *plat, void *timer, u32 ms, bool periodic);
bool bcm4360_plat_timer_del(void *plat, void *timer);	/* true: it was pending */
void bcm4360_plat_timer_free(void *plat, void *timer);

#endif
