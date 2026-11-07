/*
 * Changes of the transmit and receive chains (docs/re/spec/coverage.md part 2,
 * G5): wlc_stf_txchain_set with its completion, wlc_stf_rxchain_set, the
 * completion at the drain of the transmit FIFOs, and the platform's entry
 * bcm4360_core_set_chains(). Works on the stf block (cca-stf.md "The stf
 * state") in place.
 */
#include "core.h"
#include <bcm4360/stf.h>
#include <bcm4360/phy_api.h>

#define STF(c)			((u8 *)(c)->stf_state)
#define STF_HW_TXCHAIN		0x00
#define STF_TXCHAIN		0x01
#define STF_TXSTREAMS		0x02
#define STF_HW_RXCHAIN		0x03
#define STF_RXCHAIN		0x05
#define STF_RXSTREAMS		0x06
#define STF_TXANT		0x08
#define STF_COLUMN		0x15
#define STF_CORE1_FIRST		0x8a
#define STF_REQ(id)		(0x5a + (id))	/* the five requesters' masks */
#define STF_PENDING		0x58
#define STF_NREQ		5
#define STF_TXCORE_NSTS(i)	(0x16 + 2 * (i))
#define STF_TXCORE_MASK(i)	(0x17 + 2 * (i))
#define STF_OVR(i)		(0x22 + (i))
#define STF_OVR_CB		0x80	/* a callback pointer, never set by the open core */
#define STF_POLICY_BAND		0x5f
#define NCLASSES		6

#define WLC_FLAG_TXCHAIN	0x20	/* wlc+0x304 bit 5: the chain change waits for the FIFOs */
#define WLC_FLAG_POLICY		0x40	/* bit 6: the same for the spatial policy */
#define WLC_HTCAP_381		0x381	/* the HT capability copies (bit 7: STBC transmit) */
#define WLC_HTCAP_71B		0x71b
#define BAND_RSPEC_OVR		0x50	/* the band's fixed rate overrides */
#define BAND_MRSPEC_OVR		0x54
#define BAND_STBC_TX		0x59
#define SHM_TXBF_BASE		0x0b0
#define PUB_TXBF_CAP		0xe8
#define PUB_NBANDS_B		0x44

#define E_REFUSED		(-29)

static u32 popcount8(u32 v)
{
	u32 n = 0;

	for (v &= 0xff; v; v >>= 1)
		n += v & 1;
	return n;
}

static void *band_of(struct bcm4360_core *c, u32 unit)
{
	return FIELD(c->wlc, WLC_BANDSTATE(unit), void *);
}

/* the transmit-pending counts of the four FIFOs added up (frames.md 3.8) */
static s32 txpending(struct bcm4360_core *c)
{
	return bcm4360_stf_env_txpending_sum(c->wlc);
}

/* cca-stf.md, sub_172af1 (wlc_stf_txcore_default) */
static u8 txcore_default(const u8 *stf, u32 nsts)
{
	u8 t = stf[STF_TXCHAIN];

	if (nsts > stf[STF_TXSTREAMS])
		return 0;
	if (nsts == 1) {
		if (!stf[STF_CORE1_FIRST])
			return (t & 1) ? 1 : (t & 2) ? 2 : 4;
		return (t & 2) ? 2 : (t & 4) ? 4 : 1;
	}
	if (nsts == 2)
		return ((t & 5) == 5) ? 5 : ((t & 3) == 3) ? 3 : 6;
	return 7;
}

/* G5, wlc_stf_txcore_table_upd (sub_172b5a): the core table of cca-stf.md "Tables" */
static void txcore_table_upd(struct bcm4360_core *c)
{
	static const u8 nsts[NCLASSES] = { 1, 1, 1, 2, 3, 4 };
	static const u8 col[NCLASSES][2] = {
		{ 1, 2 }, { 1, 2 }, { 1, 2 }, { 3, 6 }, { 7, 7 }, { 0x0f, 0x0f },
	};
	u8 *stf = STF(c);
	u32 i, k = stf[STF_TXCHAIN] == 6 ? 1 : 0;

	stf[STF_COLUMN] = k + 1;
	for (i = 0; i < NCLASSES; i++) {
		stf[STF_TXCORE_NSTS(i)] = nsts[i];
		stf[STF_TXCORE_MASK(i)] = stf[STF_CORE1_FIRST] ? txcore_default(stf, nsts[i])
							      : col[i][k];
	}
}

/* cca-stf.md, sub_174cd5 (wlc_stf_spatial_mode_upd): the band's configured policy */
static void spatial_mode_upd(struct bcm4360_core *c)
{
	const u8 *stf = STF(c);
	u16 cs = FIELD(c->wlc, WLC_CHANSPEC, u16);
	u8 ch = cs & 0xff, p;

	if (!(cs & 0xc000))
		p = stf[STF_POLICY_BAND];
	else
		p = stf[STF_POLICY_BAND + (ch <= 51 ? 1 : ch <= 99 ? 2 : ch <= 148 ? 3 : 4)];
	(void)bcm4360_stf_spatial_policy_set(c->wlc, (s8)p);
}

/* G5, sub_173b37 (wlc_stf_stbc_tx_upd): STBC transmit needs more than one stream and the PHY */
static void stbc_tx_upd(struct bcm4360_core *c)
{
	u8 *stf = STF(c);
	u8 *cur = FIELD(c->wlc, WLC_BAND, u8 *);
	bool ok = stf[STF_TXSTREAMS] > 1 && c->phy &&
		  (bcm4360_phy_cap_get(c->phy) & BCM4360_PHY_CAP_STBC);
	u8 v = ok ? cur[BAND_STBC_TX] : 0;
	u32 u;

	for (u = 0; u < 2; u++)
		if (band_of(c, u))
			((u8 *)band_of(c, u))[BAND_STBC_TX] = v;
	if (ok) {
		FIELD(c->wlc, WLC_HTCAP_381, u8) |= 0x80;
		FIELD(c->wlc, WLC_HTCAP_71B, u8) |= 0x80;
	} else {
		FIELD(c->wlc, WLC_HTCAP_381, u8) &= ~0x80;
		FIELD(c->wlc, WLC_HTCAP_71B, u8) &= ~0x80;
	}
}

/* G5, wlc_txbf_rxchain_upd: the receive streams of the beamforming block */
static void txbf_rxchain_upd(struct bcm4360_core *c)
{
	u8 *stf = STF(c);
	u32 b;

	if (!FIELD(c->pub, PUB_TXBF_CAP, u8) || !FIELD(c->wlc, WLC_CLK, u8))
		return;
	b = 2u * bcm4360_shm_read(c->hw, SHM_TXBF_BASE);
	bcm4360_shm_write(c->hw, b + 0xe4, (u16)(stf[STF_RXSTREAMS] - 1));
}

/* the PHY's chains: the active transmit mask and the receive mask */
static void phy_chains(struct bcm4360_core *c)
{
	u8 *stf = STF(c);

	if (c->phy && c->up)
		bcm4360_phy_stf_chain_set(c->phy, stf[STF_TXCHAIN], stf[STF_RXCHAIN]);
}

/* docs/re/spec/coverage.md, G5: wlc_stf_txchain_set_complete */
void core_stf_txchain_set_complete(struct bcm4360_core *c)
{
	u8 *stf = STF(c);
	u32 i;

	stf[STF_TXCHAIN] = stf[STF_PENDING];
	stf[STF_TXSTREAMS] = (u8)popcount8(stf[STF_TXCHAIN]);
	stf[STF_PENDING] = 0;
	if (stf[STF_TXCHAIN]) {
		for (i = 0; i < 2; i++)
			if (stf[STF_OVR(1 - i)] & ~stf[STF_TXCHAIN])
				stf[STF_OVR(1 - i)] = 0;	/* (and the callback, never set) */
	}
	stbc_tx_upd(c);
	for (i = 0; i < 2; i++)
		if (band_of(c, i))
			bcm4360_stf_ss_update(c->wlc, band_of(c, i));
	stf[STF_TXANT] = 3;
	txcore_table_upd(c);
	spatial_mode_upd(c);
	bcm4360_stf_env_txc_gen_inc(c->wlc);
	phy_chains(c);
	/*
	 * wlc_txbf_txchain_upd: writes nothing in this configuration (verified against the object
	 * with the beamforming capability on and no links); the per-link chain words are G8's.
	 */
}

/* G5 step 4: fixed rates over more than one chain stop with a change to one chain */
static bool fixed_rates_multi(struct bcm4360_core *c, bool clear)
{
	bool any = false;
	u32 u, n = FIELD(c->pub, PUB_NBANDS_B, u8) > 1 ? 2 : 1, o;

	for (u = 0; u < n; u++) {
		u8 *b = band_of(c, u);

		for (o = BAND_RSPEC_OVR; b && o <= BAND_MRSPEC_OVR; o += 4) {
			u32 r = FIELD(b, o, u32), enc = (r >> 24) & 3, mcs = r & 0x7f;
			u32 nss = enc == 2 ? (r >> 4) & 0xf : enc == 1 && mcs != 32 ? (mcs >> 3) + 1 : 1;

			if (nss > 1) {
				any = true;
				if (clear)
					FIELD(b, o, u32) = 0;
			}
		}
	}
	return any;
}

/* docs/re/spec/coverage.md, G5: wlc_stf_txchain_set(wlc, mask, force, id) */
int core_stf_txchain_set(struct bcm4360_core *c, u8 mask, bool force, u32 id)
{
	u8 *stf = STF(c);
	u8 old, n;
	u32 i, u;

	if (!stf || id >= STF_NREQ)
		return E_REFUSED;
	for (i = 0; i < STF_NREQ; i++)
		if (!stf[STF_REQ(i)])		/* never stored as 0 (step 3): not yet initialised */
			stf[STF_REQ(i)] = 0xff;
	old = stf[STF_REQ(id)];
	stf[STF_REQ(id)] = mask;
	n = stf[STF_HW_TXCHAIN];
	for (i = 0; i < STF_NREQ; i++)
		n &= stf[STF_REQ(i)];
	if (n == stf[STF_TXCHAIN])
		return 0;
	if ((n & ~stf[STF_HW_TXCHAIN]) || !n) {
		stf[STF_REQ(id)] = old;
		return E_REFUSED;
	}
	if (popcount8(stf[STF_TXCHAIN]) > 1 && popcount8(n) == 1) {
		if (fixed_rates_multi(c, false)) {
			if (!force) {
				stf[STF_REQ(id)] = old;
				return E_REFUSED;
			}
			fixed_rates_multi(c, true);
		}
		if (FIELD(c->wlc, WLC_BAND, u8 *)[BAND_STBC_TX] == 1)
			for (u = 0; u < 2; u++)
				if (band_of(c, u))
					((u8 *)band_of(c, u))[BAND_STBC_TX] = 0;
	}
	stf[STF_PENDING] = n;
	if ((n & stf[STF_TXCHAIN]) == stf[STF_TXCHAIN] || !txpending(c)) {
		FIELD(c->wlc, WLC_FLAGS_304, u8) &= ~WLC_FLAG_TXCHAIN;
		core_stf_txchain_set_complete(c);
	} else {
		FIELD(c->wlc, WLC_FLAGS_304, u8) |= WLC_FLAG_TXCHAIN;
	}
	return 0;
}

/* docs/re/spec/coverage.md, G5: wlc_stf_rxchain_set(wlc, mask) */
int core_stf_rxchain_set(struct bcm4360_core *c, u8 mask)
{
	u8 *stf = STF(c);

	if (!stf || mask == stf[STF_RXCHAIN])
		return 0;
	if ((mask & ~stf[STF_HW_RXCHAIN]) || !mask)
		return E_REFUSED;
	stf[STF_RXCHAIN] = mask;
	stf[STF_RXSTREAMS] = (u8)popcount8(mask);
	txbf_rxchain_upd(c);
	/*
	 * Streams changed to or from one: the SM power-save action frame is the platform's
	 * (mac80211 sends it); the PHY follows at once (G5, "associated: when leaving ...").
	 */
	phy_chains(c);
	return 0;
}

/* G5 step 5 and frames.md 4.3 step 10: the FIFOs have drained, the changes wait no longer */
void core_txfifo_complete(struct bcm4360_core *c)
{
	u8 *flags = &FIELD(c->wlc, WLC_FLAGS_304, u8);

	if (!(*flags & (WLC_FLAG_TXCHAIN | WLC_FLAG_POLICY)) || txpending(c))
		return;
	if (*flags & WLC_FLAG_TXCHAIN) {
		*flags &= ~WLC_FLAG_TXCHAIN;
		core_stf_txchain_set_complete(c);
	}
	if (*flags & WLC_FLAG_POLICY) {
		*flags &= ~WLC_FLAG_POLICY;
		bcm4360_stf_spatialpolicy_set_complete(c->wlc);
	}
}

/* G5: the platform's entry; 0 leaves a mask as it is */
int bcm4360_core_set_chains(struct bcm4360_core *c, u8 txmask, u8 rxmask)
{
	int r = 0;

	if (!c->up)
		return -1;		/* the chains are applied by the platform after the next up */
	if (txmask)
		r = core_stf_txchain_set(c, txmask, true, 0);
	if (!r && rxmask)
		r = core_stf_rxchain_set(c, rxmask);
	return r;
}

/* blocks of Broadcom's layout, for the tests (frames_check.py): 0 wlc, 1 stf, 2 pub */
void *core_test_block(struct bcm4360_core *c, u32 which)
{
	return which == 0 ? c->wlc : which == 1 ? c->stf_state : c->pub;
}
