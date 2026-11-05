// SPDX-License-Identifier: ISC
/*
 * The core's transmit path and transmit status (docs/re/spec/frames.md,
 * sections 3 and 4): the prefix and the transmit header of MAC revision 42
 * in front of the frame, the DMA post, and the status of a frame.
 */
#include "core.h"
#include <bcm4360/dma.h>

/* MacTxControlLow */
#define TXC_ACK			0x0080
#define TXC_BEACON		0x0200
#define TXC_SEQ_BY_UCODE	0x0800
#define TXC_FIRST_FRAG		0x4000
/* MacTxControlHigh */
#define TXC_HI_FIXED_RATE	0x0002
#define TXC_HI_VHT		0x0004

/* RtsCtsControl */
#define RTS_LAST		0x0020
#define RTS_RTS			0x0004
#define RTS_CTS_SELF		0x0008
#define RTS_OFDM		0x0001

/* the flow control of the platform's queues (frames.md 3.8) */
#define TX_STOP_BELOW		4	/* free descriptors: the FIFO counts as full */
#define TX_WAKE_FROM		32

/* the transmit status registers (frames.md 4.1) */
#define D11_TXSTATUS0		0x170
#define D11_TXSTATUS1		0x174
#define D11_TXSTATUS2		0x178
#define D11_TXSTATUS3		0x17c
#define D11_TSF_LOW		0x180

#define PHYTX_TYPE_CCK		0
#define PHYTX_TYPE_OFDM		1
#define PHYTX_TYPE_HT		2
#define PHYTX_TYPE_VHT		3

static void put16(u8 *p, u32 off, u16 v)
{
	p[off] = v;
	p[off + 1] = v >> 8;
}

static bool rate_is_cck(const struct bcm4360_txrate *r)
{
	return !r->enc && (r->rate == 2 || r->rate == 4 || r->rate == 11 || r->rate == 22);
}

/* frames.md 3.5: the number of space-time streams of a rate */
static u32 txrate_nsts(const struct bcm4360_txrate *r)
{
	u32 nss = r->nss ? r->nss : 1;

	if (r->enc == 1)
		nss = r->rate < 32 ? r->rate / 8 + 1 : 1;
	if (r->flags & BCM4360_TX_STBC)
		nss = r->enc == 2 ? nss * 2 : nss + 1;
	return nss;
}

/* frames.md 3.3: the rate in 500 kb/s units (TxRate) */
static u32 txrate_500k(const struct bcm4360_txrate *r)
{
	static const u8 k6[10] = { 3, 6, 9, 12, 18, 24, 27, 30, 36, 40 };
	u32 nsd = r->bw == 0 ? 52 : r->bw == 1 ? 108 : 234;
	u32 nss, mcs = r->rate, x;

	if (!r->enc)
		return r->rate;
	if (r->enc == 1) {
		if (mcs == 32)
			return 12;
		nss = mcs / 8 + 1;
		mcs &= 7;
	} else {
		nss = r->nss ? r->nss : 1;
		if (mcs > 9)
			mcs = 9;
	}
	x = nsd * k6[mcs] * nss;
	return x * 1000 * ((r->flags & BCM4360_TX_SGI) ? 10 : 9) / 216 / 500;
}

/* frames.md 3.6, wlc_compute_plcp: the six bytes for a rate and a frame length */
static void tx_plcp(u8 *plcp, const struct bcm4360_txrate *r, u32 len, bool to_ap)
{
	u32 v, us, nsts;

	hw_memset(plcp, 0, 6);
	if (rate_is_cck(r)) {
		plcp[0] = r->rate * 5;
		plcp[1] = 0x04;
		switch (r->rate) {
		case 2:
			us = 8 * len;
			break;
		case 4:
			us = 4 * len;
			break;
		case 11:
			us = (16 * len + 10) / 11;
			break;
		default:
			us = (8 * len + 10) / 11;
			if (us * 11 - 8 * len >= 8)
				plcp[1] |= 0x80;
			break;
		}
		plcp[2] = us;
		plcp[3] = us >> 8;
	} else if (!r->enc) {
		switch (r->rate) {
		case 12: v = 0xb; break;
		case 18: v = 0xf; break;
		case 24: v = 0xa; break;
		case 36: v = 0xe; break;
		case 48: v = 0x9; break;
		case 72: v = 0xd; break;
		case 96: v = 0x8; break;
		default: v = 0xc; break;
		}
		v |= (len & 0xfff) << 5;
		plcp[0] = v;
		plcp[1] = v >> 8;
		plcp[2] = v >> 16;
	} else if (r->enc == 1) {
		plcp[0] = r->rate | (r->bw ? 0x80 : 0);
		plcp[1] = len;
		plcp[2] = len >> 8;
		plcp[3] = 0x07;
		if (r->flags & BCM4360_TX_STBC)
			plcp[3] |= 0x10;
		if (r->flags & BCM4360_TX_LDPC)
			plcp[3] |= 0x40;
		if (r->flags & BCM4360_TX_SGI)
			plcp[3] |= 0x80;
	} else {
		nsts = txrate_nsts(r);
		v = (r->bw & 3) | 4 | ((r->flags & BCM4360_TX_STBC) ? 8 : 0);
		v |= (to_ap ? 0u : 63u) << 4;
		v |= ((nsts - 1) & 7) << 10;
		v |= 1u << 23;
		plcp[0] = v;
		plcp[1] = v >> 8;
		plcp[2] = v >> 16;
		plcp[3] = ((r->flags & BCM4360_TX_SGI) ? 1 : 0) |
			  ((r->flags & BCM4360_TX_LDPC) ? 4 : 0) | (r->rate << 4);
		plcp[4] = 0x02;
	}
}

/* frames.md 3.5, wlc_stf_txcore_get: the core mask for the rate's class */
static u32 tx_corecount_mask(struct bcm4360_core *c, const struct bcm4360_txrate *r)
{
	const u8 *stf = c->stf_state;
	u32 idx, m = 0;

	if (!r->enc)
		idx = rate_is_cck(r) ? 0 : 1;
	else
		idx = txrate_nsts(r) + 1;
	if (idx > 5)
		idx = 5;
	if (stf) {
		m = stf[0x17 + 2 * idx];		/* txcore[idx].mask */
		if (stf[0x22 + idx])			/* txcore_override[idx] */
			m = stf[0x22 + idx];
	}
	if (!m)
		m = c->info.txchain;		/* wlc-hw: the stf defaults are not made yet */
	return m & 0xf;
}

/* frames.md 3.5: the three PHY control words of an entry */
static void tx_phyctl(struct bcm4360_core *c, u8 *e, const struct bcm4360_txrate *r)
{
	u32 type, bwc = r->bw > 2 ? 2 : r->bw;
	u32 w0, w1, w2;

	if (!r->enc)
		type = rate_is_cck(r) ? PHYTX_TYPE_CCK : PHYTX_TYPE_OFDM;
	else
		type = r->enc == 1 ? PHYTX_TYPE_HT : PHYTX_TYPE_VHT;
	w0 = type | 4 | (tx_corecount_mask(c, r) << 6) | (bwc << 14);
	if ((r->flags & BCM4360_TX_SHORTPRE) && rate_is_cck(r) && r->rate != 2)
		w0 |= 0x10;
	/* sub-channel in the channel; the power field (bits 3..8) is 0: wlc_stf_get_pwrperrate
	 * is not specified yet (TODO, questions) */
	w1 = ((c->chanspec >> 8) & 7) >> bwc;
	if (r->enc == 1)
		w2 = r->rate;
	else if (r->enc == 2)
		w2 = ((r->nss ? r->nss : 1) - 1) << 4 | r->rate;
	else if (rate_is_cck(r))
		w2 = r->rate == 2 ? 0 : r->rate == 4 ? 1 : r->rate == 11 ? 2 : 3;
	else
		w2 = r->rate == 12 ? 0 : r->rate == 18 ? 1 : r->rate == 24 ? 2 : r->rate == 36 ? 3 :
		     r->rate == 48 ? 4 : r->rate == 72 ? 5 : r->rate == 96 ? 6 : 7;
	if (r->flags & BCM4360_TX_STBC)
		w2 |= 0x40;
	put16(e, 0, w0);
	put16(e, 2, w1);
	put16(e, 4, w2);
}

/* frames.md 3.7: RtsCtsControl of an entry (protection frame at 6 Mb/s OFDM or 1 Mb/s CCK) */
static u16 tx_rtsctl(const struct bcm4360_txparams *p, const struct bcm4360_txrate *r, bool last,
		     bool rts)
{
	u16 v = last ? RTS_LAST : 0;

	if (rts)
		v |= RTS_RTS;
	else if (p->cts)
		v |= RTS_CTS_SELF;
	if (rts || p->cts) {
		if (rate_is_cck(r))
			v |= 0xa << 8;			/* 1 Mb/s CCK */
		else
			v |= RTS_OFDM | 0xb << 8;	/* 6 Mb/s OFDM */
	}
	return v;
}

/* frames.md 3.3: the length of the 802.11 header (IVOffset) */
static u32 tx_hdrlen(const u8 *f, u32 len)
{
	u32 type = (f[0] >> 2) & 3, n;

	if (type == 1)
		return 16;
	n = (type == 2 && (f[1] & 3) == 3) ? 30 : 24;
	if (type == 2 && (f[0] & 0x80))
		n += 2;
	return n < len ? n : len;
}

/* frames.md 3.3, 3.4: the header in front of the frame*/
static void tx_header(struct bcm4360_core *c, u8 *h, const u8 *f, u32 len,
		      const struct bcm4360_txparams *p, u16 id, bool agg)
{
	/* ampdu.md 1.4 step 3: RTS/CTS before every A-MPDU; step 1: the host's sequence number */
	bool rts = p->rts || agg;
	bool hwseq = p->seq_by_hw && !p->ampdu;

	u32 type = (f[0] >> 2) & 3, i;
	u16 low = TXC_FIRST_FRAG, high = 0;
	bool qos = type == 2 && (f[0] & 0x80);
	bool to_ap = (f[1] & 3) == 1;
	u32 hdrlen = tx_hdrlen(f, len);

	hw_memset(h, 0, TX_HDR_LEN);
	if (!(f[4] & 1) && !p->no_ack && (type != 2 || !qos || !(f[hdrlen - 2] & 0x60)))
		low |= TXC_ACK;
	if (type == 0 && (f[0] & 0xf0) == 0x80)
		low |= TXC_BEACON;
	if (hwseq)
		low |= TXC_SEQ_BY_UCODE;
	if (p->nrates == 1)
		high |= TXC_HI_FIXED_RATE;
	for (i = 0; i < p->nrates; i++)
		if (p->rates[i].enc == 2)
			high |= TXC_HI_VHT;
	put16(h, TXH_CTL_LOW, low);
	put16(h, TXH_CTL_HIGH, high);
	put16(h, TXH_CHANSPEC, c->chanspec);
	h[TXH_IVOFFSET] = hdrlen;
	put16(h, TXH_FRAMELEN, len + 4);
	put16(h, TXH_FRAMEID, id);
	if (!hwseq && len >= 24)
		put16(h, TXH_SEQ, f[22] | (u16)f[23] << 8);
	for (i = 0; i < p->nrates; i++) {
		u8 *e = h + TX_RATE_ENTRY(i);
		const struct bcm4360_txrate *r = &p->rates[i];

		tx_phyctl(c, e, r);
		tx_plcp(e + 6, r, len + 4, to_ap);
		put16(e, 0x0e, txrate_500k(r));
		put16(e, 0x10, tx_rtsctl(p, r, i == p->nrates - 1, rts));
	}
}

/* frames.md 3.2, 3.8: the free descriptors of a FIFO's engine */
static struct bcm4360_dma *tx_engine(struct bcm4360_core *c, u32 fifo)
{
	if (fifo >= FR_NFIFO || !c->wlc)
		return NULL;
	return FIELD(CORE_WLC_HW(c), HW_DI(fifo), struct bcm4360_dma *);
}

/* frames.md 3.2: the headroom needed in front of a frame */
u32 bcm4360_core_tx_headroom(struct bcm4360_core *c)
{
	(void)c;
	return TX_HEADROOM;
}

/* frames.md 3: build prefix and header, post to the FIFO */
int bcm4360_core_tx(struct bcm4360_core *c, void *pkt, const struct bcm4360_txparams *p)
{
	struct bcm4360_dma *di;
	u32 len, avail;
	u8 *f, *h;
	u16 id;

	if (!c->up || !p->nrates || p->nrates > TX_NRATES)
		return -1;
	di = tx_engine(c, p->fifo);
	if (!di)
		return -1;
	len = osl_pktlen(c->osh, pkt);
	if (len < 10)
		return -1;
	avail = ((struct bcm4360_dma_pub *)di)->txavail;
	if (avail < 2)
		return -1;

	f = osl_pktdata(c->osh, pkt);
	h = osl_pktpush(c->osh, pkt, TX_HEADROOM);
	id = p->fifo | ((c->fr.txid & 0x3f) << 9);
	tx_header(c, h + TX_PREFIX_LEN, f, len, p, id, core_ampdu_is_agg(c, p));
	h[0] = 0x02;				/* the prefix (3.2); byte 2 bit 1 is the A-MPDU epoch */
	h[1] = h[2] = h[3] = 0;
	core_ampdu_header(c, h, h + TX_PREFIX_LEN, f, p);	/* ampdu.md 1.4-1.6 */
	c->fr.txid++;
	if (bcm4360_dma_txfast(di, pkt, true))
		return -1;			/* the engine freed the packet */
	avail = ((struct bcm4360_dma_pub *)di)->txavail;
	if (avail < TX_STOP_BELOW) {
		c->fr.stopped[p->fifo] = true;
		return 1;
	}
	return 0;
}

/* frames.md 4.3 step 10: the packet leaves as the platform gave it to us */
static void tx_report(struct bcm4360_core *c, u32 fifo, struct bcm4360_dma *di, void *pkt,
		      const struct bcm4360_txstatus *ts)
{
	osl_pktpull(c->osh, pkt, TX_HEADROOM);
	c->fr.tx_status++;
	bcm4360_plat_txstatus(c->plat, pkt, ts);
	if (c->fr.stopped[fifo] &&
	    ((struct bcm4360_dma_pub *)di)->txavail >= TX_WAKE_FROM) {
		c->fr.stopped[fifo] = false;
		bcm4360_plat_tx_wake(c->plat, fifo);
	}
}

#define AMPDU_MAX_STATUS	64	/* MPDUs one status completes (the bitmap's width) */
#define AMPDU_MAX_REPOST	64	/* consecutive re-posts before the MPDUs count as lost */

/*
 * ampdu.md 2.2, 2.3, 2.6: the status of an aggregate. `pkt` is the head MPDU
 * (already taken from the ring); the NCONS - 1 after it are taken here. w0, w2:
 * the status words, bm0, bm1: the Block Ack bitmap (v1, v2). True: fatal.
 */
static bool tx_status_ampdu(struct bcm4360_core *c, struct bcm4360_dma *di, u32 fifo, void *pkt,
			    u32 w0, u32 w2, u32 bm0, u32 bm1)
{
	struct core_ampdu *a = &c->fr.ampdu;
	void *pkts[AMPDU_MAX_STATUS];
	struct bcm4360_txstatus ts;
	u32 ncons = (w0 >> 8) & 0x7f, reason = (w0 >> 4) & 0xf, i, nack = 0;
	bool fb = ((w2 >> 16) & 0xff) != 0;

	if (ncons > AMPDU_MAX_STATUS) {
		osl_pktfree(c->osh, pkt, true);
		return true;
	}
	pkts[0] = pkt;
	for (i = 1; i < ncons; i++) {
		pkts[i] = bcm4360_dma_getnexttxp(di, 2);	/* their ids are not checked (2.3 step 3) */
		if (!pkts[i])
			break;
	}
	if (i < ncons) {
		while (i--)
			osl_pktfree(c->osh, pkts[i], true);
		return true;
	}
	a->nstatus++;
	if (reason == 1 && a->repost < AMPDU_MAX_REPOST) {
		/* ampdu.md 2.3 step 2: the receiver was asleep, nothing was sent: post them again */
		for (i = 0; i < ncons; i++) {
			if (bcm4360_dma_txfast(di, pkts[i], true))
				a->nholes++;	/* no room: the engine freed it; lost */
			else
				a->nreposts++;
		}
		a->repost++;
		return false;
	}
	a->repost = 0;
	for (i = 0; i < ncons; i++)
		if (reason == 0 && ((i < 32 ? bm0 >> i : bm1 >> (i - 32)) & 1))
			nack++;
	a->nholes += ncons - nack;
	for (i = 0; i < ncons; i++) {
		hw_memset(&ts, 0, sizeof(ts));
		ts.ampdu = 1;
		ts.acked = reason == 0 && ((i < 32 ? bm0 >> i : bm1 >> (i - 32)) & 1);
		if (!i) {
			ts.ampdu_len = ncons;
			ts.ampdu_ack_len = nack;
			ts.tries = fb ? 2 : 1;
			ts.rate_idx = fb ? 1 : 0;
		}
		tx_report(c, fifo, di, pkts[i], &ts);
	}
	return false;
}

/*
 * frames.md 4.3: one status for the oldest finished frame of its FIFO.
 * w0..w3: the first package, bm0, bm1: v1, v2 of the second (the aggregate's
 * Block Ack bitmap, ampdu.md 2.2). True: fatal.
 */
static bool tx_status_one(struct bcm4360_core *c, u32 w0, u32 w1, u32 w2, u32 w3, u32 bm0, u32 bm1)
{
	struct bcm4360_txstatus ts;
	struct bcm4360_dma *di;
	u32 fifo = (w0 >> 16) & 7, ncons = (w0 >> 8) & 0x7f;
	u8 *d;
	void *pkt;

	di = tx_engine(c, fifo);
	if (!di)
		return true;
	pkt = bcm4360_dma_getnexttxp(di, 2);
	if (!pkt)
		return true;
	d = osl_pktdata(c->osh, pkt);
	if ((d[TX_PREFIX_LEN + TXH_FRAMEID] | (u16)d[TX_PREFIX_LEN + TXH_FRAMEID + 1] << 8) !=
	    (w0 >> 16) || !ncons) {
		osl_pktfree(c->osh, pkt, true);
		return true;
	}
	if (d[TX_PREFIX_LEN + TXH_CTL_LOW] & 0x40)
		return tx_status_ampdu(c, di, fifo, pkt, w0, w2, bm0, bm1);
	hw_memset(&ts, 0, sizeof(ts));
	ts.acked = ((w0 & 0x8000) || ncons > 1) && !((w0 >> 4) & 0xf) && !((w1 >> 16) & 0xff);
	ts.tries = (w2 & 0xff) + ((w2 >> 16) & 0xff) + (w3 & 0xff) + ((w3 >> 16) & 0xff);
	ts.rate_idx = (w3 >> 16) & 0xff ? 3 : (w3 & 0xff) ? 2 : ((w2 >> 16) & 0xff) ? 1 : 0;
	tx_report(c, fifo, di, pkt, &ts);
	return false;
}

/*
 * frames.md 4.1, wlc_bmac_txstatus: up to `bound` statuses (two packages of
 * four words each). True: the bound was reached.
 */
bool core_txstatus_run(struct bcm4360_core *c, u32 bound, bool *fatal)
{
	u32 n = 0, w0, w1, w2, w3, v0, v1, v2;

	*fatal = false;
	(void)d11_read32(c->hw, D11_TSF_LOW);	/* the time stamp of every status of this call */
	while (!*fatal && n < bound) {
		w0 = d11_read32(c->hw, D11_TXSTATUS0);
		if (!(w0 & 1))
			break;
		if (w0 == 0xffffffff)
			return false;
		w1 = d11_read32(c->hw, D11_TXSTATUS1);
		w2 = d11_read32(c->hw, D11_TXSTATUS2);
		w3 = d11_read32(c->hw, D11_TXSTATUS3);
		v0 = d11_read32(c->hw, D11_TXSTATUS0);
		v1 = d11_read32(c->hw, D11_TXSTATUS1);
		v2 = d11_read32(c->hw, D11_TXSTATUS2);
		(void)d11_read32(c->hw, D11_TXSTATUS3);
		if (!(v0 & 1))
			return false;
		*fatal = tx_status_one(c, w0, w1, w2, w3, v1, v2);
		n++;
	}
	if (*fatal) {
		c->fr.tx_fatal++;
		return false;
	}
	return n >= bound;
}
