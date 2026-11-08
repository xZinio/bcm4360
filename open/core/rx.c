/*
 * The core's receive path (docs/re/spec/frames.md, section 1): wlc_bmac_recv
 * and the hardware checks of wlc_recv, with the rate decode of
 * wlc_recv_compute_rspec. The upper layer (filters, 802.3 conversion) is
 * mac80211's.
 */
#include "core.h"
#include <bcm4360/dma.h>
#include <bcm4360/phy_api.h>

/* frames.md 1.3: the receive header */
#define RXH_LEN			40
#define RXH_FRAMESIZE		0x00
#define RXH_PHYSTATUS0		0x04
#define RXH_PHYSTATUS5		0x0e
#define RXH_STATUS1		0x10
#define RXH_STATUS2		0x12
#define RXH_TSFTIME		0x14
#define RXH_CHAN		0x16
#define RXH_TSF_L		0x18	/* host part */
#define RXH_RSSI		0x1c
#define RXH_RSSI_INVALID	0x1f
#define RXH_RXPWR		0x20

#define PHY0_TYPE_MASK		0x0003
#define PHY0_SHORTPRE		0x0080
#define PHY0_BAD		0x0310	/* unsupported rate, PLCP violation, PLCP check */
#define ST1_FCSERR		0x0001
#define ST1_PAD			0x0004
#define ST1_DECRYPTED		0x0008
#define ST1_DECRYPT_ERR	0x0010
#define ST1_KEY_SHIFT		5
#define ST1_KEY_MASK		0x3f
#define ST2_AMSDU		0x0001
#define ST2_MIC_CHECKED	0x0008
#define ST2_MIC_ERR		0x0010
#define ST2_PHYSTATUS_VALID	0x0100

#define TYPE_CCK		0
#define TYPE_OFDM		1
#define TYPE_HT			2
#define TYPE_VHT		3

#define PLCP_LEN		6
#define FCS_LEN			4
#define MIN_AFTER_PAD		8
#define D11_TSF_LOW		0x180
#define D11_TSF_HIGH		0x184

static u16 rd16(const u8 *p, u32 off)
{
	return p[off] | (u16)p[off + 1] << 8;
}

/* frames.md 1.5, the table ofdm_rate_lookup (500 kb/s units), by L-SIG bits R1..R3 */
static const u8 ofdm_rate[8] = { 96, 48, 24, 12, 108, 72, 36, 18 };

struct rx_rate {
	u8 enc, rate, nss, bw, flags;
};

/* frames.md 1.5: Type 0 to 3 from the PLCP header */
static void rx_decode_rate(const u8 *rxh, const u8 *plcp, struct rx_rate *r)
{
	u16 phy0 = rd16(rxh, RXH_PHYSTATUS0);
	u32 a1;

	r->enc = r->rate = r->nss = r->bw = r->flags = 0;
	r->nss = 1;
	switch (phy0 & PHY0_TYPE_MASK) {
	case TYPE_CCK:
		r->rate = plcp[0] / 5;
		if (phy0 & PHY0_SHORTPRE)
			r->flags |= BCM4360_RX_SHORTPRE;
		break;
	case TYPE_OFDM:
		r->rate = ofdm_rate[plcp[0] & 7];
		break;
	case TYPE_HT:
		r->enc = 1;
		r->rate = plcp[0] & 0x7f;
		r->bw = plcp[0] >> 7;
		r->nss = r->rate < 32 ? r->rate / 8 + 1 : 1;
		if (plcp[3] & 0x80)
			r->flags |= BCM4360_RX_SGI;
		if (plcp[3] & 0x40)
			r->flags |= BCM4360_RX_LDPC;
		if (plcp[3] & 0x30)
			r->flags |= BCM4360_RX_STBC;
		break;
	default:
		r->enc = 2;
		a1 = plcp[0] | (u32)plcp[1] << 8;
		r->bw = plcp[0] & 3;
		if (r->bw > 2)
			r->bw = 2;
		r->nss = ((a1 >> 10) & 7) + 1;
		if (plcp[0] & 0x08) {
			r->flags |= BCM4360_RX_STBC;
			r->nss >>= 1;
		}
		r->rate = plcp[3] >> 4;
		if (plcp[3] & 0x01)
			r->flags |= BCM4360_RX_SGI;
		if (plcp[3] & 0x04)
			r->flags |= BCM4360_RX_LDPC;
		break;
	}
}

/*
 * frames.md 1.5 "A-MPDU (receive)": the PLCP bytes of the MPDUs after the
 * first are zero; they take the rate of the first one. Returns true if the
 * frame is part of an A-MPDU.
 */
static bool rx_ampdu_rate(struct core_frames *fr, const u8 *rxh, const u8 *plcp, struct rx_rate *r)
{
	u16 phy0 = rd16(rxh, RXH_PHYSTATUS0), st2 = rd16(rxh, RXH_STATUS2);
	u32 type = phy0 & PHY0_TYPE_MASK;
	bool zero = !(plcp[0] | plcp[1] | plcp[2]);
	bool ampdu = false;

	if (type != TYPE_HT && type != TYPE_VHT)
		return false;
	if (zero) {
		/* a following (or the last) MPDU: the remembered rate */
		if (fr->rate_valid) {
			r->enc = fr->rate_enc;
			r->rate = fr->rate_rate;
			r->nss = fr->rate_nss;
			r->bw = fr->rate_bw;
			r->flags = fr->rate_flags;
		}
		return true;
	}
	if (type == TYPE_HT)
		ampdu = plcp[3] & 0x08;
	else
		ampdu = !(st2 & ST2_PHYSTATUS_VALID);	/* set: a single MPDU */
	if (ampdu) {
		fr->rate_valid = true;
		fr->rate_enc = r->enc;
		fr->rate_rate = r->rate;
		fr->rate_nss = r->nss;
		fr->rate_bw = r->bw;
		fr->rate_flags = r->flags;
	}
	return ampdu;
}

/* frames.md 1.7: the TSF of the frame from the batch's reading (lo, hi) */
static u64 rx_tsf(u32 lo, u32 hi, u16 rxtsf)
{
	u32 t = (lo & 0xffff0000) | rxtsf;

	if ((lo & 0xffff) < rxtsf)
		t -= 0x10000;
	if (lo < t)
		hi--;
	return (u64)hi << 32 | t;
}

/* the PHY of the current band (wlc_hw+0xe8 -> +0x28), else the first */
static struct bcm4360_phy *rx_phy(struct bcm4360_core *c)
{
	void *band = FIELD(CORE_WLC_HW(c), HW_BAND, void *);

	return band ? FIELD(band, BAND_PI, struct bcm4360_phy *) : c->phy;
}

/*
 * ps-keys.md 2.5: the core's decryption. RxStatus1 bit 3 (attempted) with bit 4 (ICV check
 * failed): the frame is garbage. RxStatus2 bits 3 and 4 (the core checked the TKIP Michael
 * MIC / it was wrong) mean something only with the hardware MIC keys, which are never
 * written (keys.c): ignored unless c->pk.hw_mic.
 */
static void rx_crypto(struct bcm4360_core *c, struct bcm4360_rxstatus *rs, u16 st1, u16 st2)
{
	if (!(st1 & ST1_DECRYPTED))
		return;
	if ((st1 & ST1_DECRYPT_ERR) || (c->pk.hw_mic && (st2 & ST2_MIC_CHECKED) &&
					(st2 & ST2_MIC_ERR)))
		rs->flags |= BCM4360_RX_DECRYPT_ERR;
	else
		rs->flags |= BCM4360_RX_DECRYPTED;
}

/*
 * frames.md 1.8: fill the status of one frame whose receive header is at
 * the start of `d` and cut the packet down to the 802.11 frame and its FCS.
 * False: the frame is dropped (1.4, steps 1 to 6).
 */
static bool rx_one(struct bcm4360_core *c, void *p, u32 lo, u32 hi)
{
	struct core_frames *fr = &c->fr;
	struct bcm4360_rxstatus rs;
	struct rx_rate rate;
	u8 *d = osl_pktdata(c->osh, p);
	u32 len = osl_pktlen(c->osh, p), n, pad, skip, i;
	u16 phy0, st1, st2;
	const u8 *plcp;
	bool ampdu;

	if (len < RXH_LEN) {
		fr->rx_drop[0]++;
		return false;
	}
	n = rd16(d, RXH_FRAMESIZE);
	st1 = rd16(d, RXH_STATUS1);
	st2 = rd16(d, RXH_STATUS2);
	phy0 = rd16(d, RXH_PHYSTATUS0);

	/* step 1.2.1 and 1.2.2: the host part of the header */
	d[RXH_TSF_L] = lo;
	d[RXH_TSF_L + 1] = lo >> 8;
	d[RXH_TSF_L + 2] = lo >> 16;
	d[RXH_TSF_L + 3] = lo >> 24;
	if (rx_phy(c))
		bcm4360_phy_rssi_compute(rx_phy(c), d);

	/* 1.4: the checks before the upper layer */
	pad = (st1 & ST1_PAD) ? 2 : 0;
	if (len < RXH_LEN + pad + PLCP_LEN || n + RXH_LEN > len) {
		fr->rx_drop[1]++;
		return false;
	}
	if (phy0 & PHY0_BAD) {
		fr->rx_drop[2]++;
		return false;
	}
	if ((st1 & ST1_FCSERR) && !(fr->rx_filter & BCM4360_FILTER_FCSFAIL)) {
		fr->rx_drop[3]++;
		return false;
	}
	if (n < pad + MIN_AFTER_PAD) {
		fr->rx_drop[4]++;
		return false;
	}
	if (st2 & ST2_AMSDU) {
		fr->rx_drop[5]++;
		return false;		/* TODO: A-MSDU sub-frames are not joined (reception off) */
	}

	/* 1.8 */
	plcp = d + RXH_LEN + pad;
	rx_decode_rate(d, plcp, &rate);
	ampdu = rx_ampdu_rate(fr, d, plcp, &rate);
	hw_memset(&rs, 0, sizeof(rs));
	rs.chanspec = rd16(d, RXH_CHAN);
	rs.tsf = rx_tsf(lo, hi, rd16(d, RXH_TSFTIME));
	if (!d[RXH_RSSI_INVALID]) {
		rs.rssi = (s8)d[RXH_RSSI];
		for (i = 0; i < 3; i++)
			if (c->info.rxchain & (1u << i))
				rs.chain_rssi[i] = (s8)d[RXH_RXPWR + i];
	}
	rs.enc = rate.enc;
	rs.rate = rate.rate;
	rs.nss = rate.nss;
	rs.bw = rate.bw;
	rs.flags = rate.flags;
	if (ampdu)
		rs.flags |= BCM4360_RX_AMPDU;
	if (st1 & ST1_FCSERR)
		rs.flags |= BCM4360_RX_FCS_FAIL;
	rs.key = (st1 >> ST1_KEY_SHIFT) & ST1_KEY_MASK;
	rx_crypto(c, &rs, st1, st2);

	/* the frame: header, pad and PLCP header removed; the FCS stays */
	skip = RXH_LEN + pad + PLCP_LEN;
	osl_pktpull(c->osh, p, (int)skip);
	osl_pktsetlen(c->osh, p, RXH_LEN + n - skip);
	c->fr.rx_frames++;
	if (!(st1 & ST1_FCSERR))
		core_ps_beacon(c, d + skip, RXH_LEN + n - skip, rate.rate, rate.enc, rate.flags);
	bcm4360_plat_rx(c->plat, p, &rs);
	return true;
}

/*
 * frames.md 1.2, wlc_bmac_recv: take up to `bound` frames, refill, read the
 * TSF, process each. True if the bound was reached.
 */
bool core_rx_run(struct bcm4360_core *c, u32 bound)
{
	struct bcm4360_dma *di = FIELD(CORE_WLC_HW(c), HW_DI(0), struct bcm4360_dma *);
	void *head = NULL, *tail = NULL, *p, *next;
	u32 taken = 0, lo, hi;

	if (!di)
		return false;
	while (taken < bound) {
		p = bcm4360_dma_rx(di);
		if (!p)
			break;
		if (tail)
			osl_pktsetnext(tail, p);
		else
			head = p;
		tail = p;
		taken++;
	}
	bcm4360_dma_rxfill(di);
	if (!head)
		return false;
	lo = d11_read32(c->hw, D11_TSF_LOW);
	hi = d11_read32(c->hw, D11_TSF_HIGH);	/* extra to wlc_bmac_recv: for the 64-bit TSF */
	for (p = head; p; p = next) {
		next = osl_pktnext(c->osh, p);
		osl_pktsetnext(p, NULL);
		if (!rx_one(c, p, lo, hi)) {
			c->fr.rx_dropped++;
			osl_pktfree(c->osh, p, false);
		}
	}
	return taken >= bound;
}

void bcm4360_core_rx_stats(struct bcm4360_core *c, u32 *frames, u32 *drop)
{
	u32 i;

	*frames = c->fr.rx_frames;
	for (i = 0; i < BCM4360_RX_DROP_REASONS; i++)
		drop[i] = c->fr.rx_drop[i];
}
