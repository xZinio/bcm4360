/* SPDX-License-Identifier: ISC */
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
};

#define BCM4360_RX_SGI		0x01	/* short guard interval */
#define BCM4360_RX_LDPC		0x02
#define BCM4360_RX_STBC		0x04
#define BCM4360_RX_SHORTPRE	0x08	/* short preamble (CCK) */
#define BCM4360_RX_FCS_FAIL	0x10	/* frame check sequence wrong */
#define BCM4360_RX_DECRYPTED	0x20	/* the hardware decrypted it */
#define BCM4360_RX_AMPDU	0x40	/* part of an A-MPDU */

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
	u8  seq_by_hw;		/* let the hardware fill in the sequence number */
};

/* The transmit status of one frame (frames.md). */
struct bcm4360_txstatus {
	u8  acked;
	u8  rate_idx;		/* index into the frame's rates[] that went out last */
	u8  tries;		/* attempts in all */
	u8  ampdu;		/* reported as part of an A-MPDU */
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

/* Provided by the platform; `plat` as given to bcm4360_core_attach(). */
void bcm4360_plat_rx(void *plat, void *pkt, const struct bcm4360_rxstatus *rs);
void bcm4360_plat_txstatus(void *plat, void *pkt, const struct bcm4360_txstatus *ts);
void bcm4360_plat_tx_wake(void *plat, u8 fifo);
/* a fatal error of the hardware: the platform restarts the device */
void bcm4360_plat_fatal(void *plat);
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
