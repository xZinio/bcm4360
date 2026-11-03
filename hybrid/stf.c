// SPDX-License-Identifier: ISC
/*
 * Hybrid build: Broadcom's wlc_update_txppr_offset (wlc_stf.c: the per-rate
 * transmit power offsets of the transmit chains, after every computation of
 * the PHY's power targets) by the open code of open/lib/stf.c
 * (docs/re/spec/cca-stf.md, part 2). Switch open_stf, read at every call.
 *
 * Broadcom's function reads the "min_txpower" value from a stack word it
 * never writes, so the real driver's ceiling and chain mask there depend on
 * what earlier code left on the stack; the open code uses 0.
 *
 * The stf block and the fields of wlc are read at the offsets of the
 * specification; the services call the object's functions by name (taken
 * over or not, so every switch keeps working).
 */
#include "hybrid.h"
#include <bcm4360/stf.h>

#ifdef __KERNEL__
#include <linux/module.h>
#endif

bool bcm4360_open_stf = true;
/* the spatial policy, core words and single-stream mode (part 2), separately */
bool bcm4360_open_stf_policy = true;

#ifdef __KERNEL__
module_param_named(open_stf, bcm4360_open_stf, bool, 0444);
MODULE_PARM_DESC(open_stf, "transmit power offsets of the chains (wlc_update_txppr_offset): open code (1, default) or Broadcom's (0)");
module_param_named(open_stf_policy, bcm4360_open_stf_policy, bool, 0444);
MODULE_PARM_DESC(open_stf_policy, "spatial policy, core words, single-stream mode (wlc_stf.c part 2): open code (1, default, with open_stf) or Broadcom's (0)");
#endif

/* cca-stf.md, Data: "Fields of wlc, pub and the band state read here" */
#define WLC_PUB			0x000
#define WLC_OSH			0x008
#define WLC_HW			0x020
#define WLC_CLK			0x031
#define WLC_BAND		0x040
#define BAND_PI			0x010
#define WLC_CHANSPEC		0x518
#define WLC_STF			0x550
#define WLC_OL			0x750
#define PUB_COREREV		0x014

#define FIELD(p, off, type)	(*(type *)((u8 *)(p) + (off)))

/* the object's functions the services call (takeover.txt: take or use) */
u8 wlc_phy_txpower_get_target_max(void *pi);
u8 wlc_phy_txpower_get_target_min(void *pi);
void wlc_ol_curpwr_upd(void *ol, s32 pwr, u32 chanspec);
s32 wlc_stf_txchain_set(void *wlc, s32 txchain, u32 force, u32 id);
s32 wlc_stf_spatial_policy_set(void *wlc, s32 policy);
void *ppr_create(void *osh, u32 bw);
void ppr_delete(void *osh, void *p);
u32 ppr_get_ch_bw(const void *p);
void ppr_map_vec_all(void (*fn)(void *ctx, s8 *a, s8 *b), void *ctx, void *a, void *b);
s32 ppr_get_dsss(const void *p, u32 bw, u32 chains, s8 *out);
s32 ppr_get_ofdm(const void *p, u32 bw, u32 mode, u32 chains, s8 *out);
void wlc_bmac_stf_get_rateset_shm_offset(void *wlc_hw, void *rs, u32 off, void *out);
void wlc_bmac_stf_set_rateset_shm_offset(void *wlc_hw, void *rs, u32 off, void *in);

void wlc_bmac_txant_set(void *wlc_hw, u32 phytxant);
u32 wlc_valid_rate(void *wlc, u32 rspec, s32 band, u32 verbose);
void wlc_suspend_mac_and_wait(void *wlc);
void wlc_enable_mac(void *wlc);
void wlc_beacon_phytxctl_txant_upd(void *wlc, u32 rspec);
void wlc_beacon_phytxctl(void *wlc, u32 rspec, u32 chanspec);
void wlc_txc_upd(void *wlc);
void wlc_write_shm(void *wlc, u32 offset, u32 v);
u32 wlc_phy_cap_get(void *pi);
void wlc_bmac_band_stf_ss_set(void *wlc_hw, u32 stf_mode);
void wlc_mhf(void *wlc, u32 idx, u32 mask, u32 val, s32 bands);
s32 ppr_get_ht_mcs(const void *p, u32 bw, u32 nss, u32 mode, u32 chains, s8 *out);
s32 ppr_get_vht_mcs(const void *p, u32 bw, u32 nss, u32 mode, u32 chains, s8 *out);

/* cca-stf.md, Data: more fields of wlc, pub, the band state and sih */
#define WLC_CORESTATE		0x038
#define CORESTATE_TXPEND(f)	(0x38 + 2 * (f))	/* s16, FIFOs 0..3 */
#define WLC_TXC_GEN		0x2a8
#define WLC_FLAGS_304		0x304
#define POLICY_DEFERRED		0x40
#define WLC_BCN_RATE		0x584
#define WLC_COEX		0x690
#define COEX_WORD		0x014
#define PUB_UP			0x034
#define PUB_BOARDFLAGS2_HI	0x09d
#define PUB_SHM_CORE_BASE	0x0e4			/* and +2 */
#define PUB_SIH			0x100
#define SIH_BOARDVENDOR		0x030
#define SIH_CHIP		0x03c
#define BAND_TYPE		0x000
#define BAND_PHYTYPE		0x008
#define BAND_SS_MODE		0x058

/* cca-stf.md, wlc_update_txppr_offset, step 8: force, and the requester id */
#define TXCHAIN_FORCE		1
#define TXCHAIN_ID_TXPPR	3

HYB_TAKE(void, wlc_update_txppr_offset, (void *wlc, void *ofs));
HYB_TAKE(s32, wlc_stf_spatial_policy_set, (void *wlc, s32 policy));
HYB_TAKE(void, wlc_stf_spatialpolicy_set_complete, (void *wlc));
HYB_TAKE(void, wlc_stf_txcore_shmem_write, (void *wlc, u32 force));
HYB_TAKE(s32, wlc_stf_ss_update, (void *wlc, void *bs));
HYB_TAKE(void, wlc_stf_ss_algo_channel_get, (void *wlc, u16 *out, u32 chanspec));

s32 wlc_stf_spatial_policy_set(void *wlc, s32 policy)
{
	if (!bcm4360_open_stf || !bcm4360_open_stf_policy)
		return bcm4360_blob_wlc_stf_spatial_policy_set(wlc, policy);
	return bcm4360_stf_spatial_policy_set(wlc, policy);
}

void wlc_stf_spatialpolicy_set_complete(void *wlc)
{
	if (!bcm4360_open_stf || !bcm4360_open_stf_policy) {
		bcm4360_blob_wlc_stf_spatialpolicy_set_complete(wlc);
		return;
	}
	bcm4360_stf_spatialpolicy_set_complete(wlc);
}

void wlc_stf_txcore_shmem_write(void *wlc, u32 force)
{
	if (!bcm4360_open_stf || !bcm4360_open_stf_policy) {
		bcm4360_blob_wlc_stf_txcore_shmem_write(wlc, force);
		return;
	}
	bcm4360_stf_txcore_shmem_write(wlc, (u8)force != 0);
}

s32 wlc_stf_ss_update(void *wlc, void *bs)
{
	if (!bcm4360_open_stf || !bcm4360_open_stf_policy)
		return bcm4360_blob_wlc_stf_ss_update(wlc, bs);
	return bcm4360_stf_ss_update(wlc, bs);
}

void wlc_stf_ss_algo_channel_get(void *wlc, u16 *out, u32 chanspec)
{
	if (!bcm4360_open_stf || !bcm4360_open_stf_policy) {
		bcm4360_blob_wlc_stf_ss_algo_channel_get(wlc, out, chanspec);
		return;
	}
	bcm4360_stf_ss_algo_channel_get(wlc, out, chanspec);
}

void wlc_update_txppr_offset(void *wlc, void *ofs)
{
	if (!bcm4360_open_stf) {
		bcm4360_blob_wlc_update_txppr_offset(wlc, ofs);
		return;
	}
	bcm4360_stf_update_txppr_offset(wlc, ofs);
}

static void *pi_of(void *wlc)
{
	return FIELD(FIELD(wlc, WLC_BAND, void *), BAND_PI, void *);
}

u8 *bcm4360_stf_env_state(void *wlc)
{
	return FIELD(wlc, WLC_STF, u8 *);
}

void *bcm4360_stf_env_osh(void *wlc)
{
	return FIELD(wlc, WLC_OSH, void *);
}

u16 bcm4360_stf_env_chanspec(void *wlc)
{
	return FIELD(wlc, WLC_CHANSPEC, u16);
}

bool bcm4360_stf_env_clk(void *wlc)
{
	return FIELD(wlc, WLC_CLK, u8) != 0;
}

u32 bcm4360_stf_env_corerev(void *wlc)
{
	return FIELD(FIELD(wlc, WLC_PUB, void *), PUB_COREREV, u32);
}

s8 bcm4360_stf_env_target_max(void *wlc)
{
	return (s8)wlc_phy_txpower_get_target_max(pi_of(wlc));
}

s8 bcm4360_stf_env_target_min(void *wlc)
{
	return (s8)wlc_phy_txpower_get_target_min(pi_of(wlc));
}

void bcm4360_stf_env_ol_curpwr_upd(void *wlc, s32 tmax, u16 cs)
{
	wlc_ol_curpwr_upd(FIELD(wlc, WLC_OL, void *), tmax, cs);
}

void bcm4360_stf_env_txchain_set(void *wlc, s32 mask)
{
	wlc_stf_txchain_set(wlc, mask, TXCHAIN_FORCE, TXCHAIN_ID_TXPPR);
}

void bcm4360_stf_env_spatial_policy_set(void *wlc, s32 policy)
{
	wlc_stf_spatial_policy_set(wlc, policy);
}

void *bcm4360_stf_env_ppr_create(void *osh, u32 bw)
{
	return ppr_create(osh, bw);
}

void bcm4360_stf_env_ppr_delete(void *osh, void *p)
{
	ppr_delete(osh, p);
}

u32 bcm4360_stf_env_ppr_get_ch_bw(void *p)
{
	return ppr_get_ch_bw(p);
}

void bcm4360_stf_env_ppr_map_vec_all(void (*fn)(void *ctx, s8 *a, s8 *b), void *ctx,
				     void *a, void *b)
{
	ppr_map_vec_all(fn, ctx, a, b);
}

void bcm4360_stf_env_ppr_get_dsss(void *p, u32 bw, u32 chains, s8 *out)
{
	ppr_get_dsss(p, bw, chains, out);
}

void bcm4360_stf_env_ppr_get_ofdm(void *p, u32 bw, u32 mode, u32 chains, s8 *out)
{
	ppr_get_ofdm(p, bw, mode, chains, out);
}

void bcm4360_stf_env_get_rateset_shm_offset(void *wlc, const void *rs, u16 off, u8 *buf)
{
	wlc_bmac_stf_get_rateset_shm_offset(FIELD(wlc, WLC_HW, void *), (void *)rs, off, buf);
}

void bcm4360_stf_env_set_rateset_shm_offset(void *wlc, const void *rs, u16 off, u8 *buf)
{
	wlc_bmac_stf_set_rateset_shm_offset(FIELD(wlc, WLC_HW, void *), (void *)rs, off, buf);
}

void *bcm4360_stf_env_band(void *wlc)
{
	return FIELD(wlc, WLC_BAND, void *);
}

u32 bcm4360_stf_env_bandtype(void *wlc)
{
	return FIELD(FIELD(wlc, WLC_BAND, void *), BAND_TYPE, u32);
}

u16 bcm4360_stf_env_phytype(void *wlc)
{
	return FIELD(FIELD(wlc, WLC_BAND, void *), BAND_PHYTYPE, u16);
}

u8 bcm4360_stf_env_band_ss_mode(void *bs)
{
	return FIELD(bs, BAND_SS_MODE, u8);
}

s32 bcm4360_stf_env_txpending_sum(void *wlc)
{
	u8 *cs = FIELD(wlc, WLC_CORESTATE, u8 *);
	s32 sum = 0, f;

	for (f = 0; f < 4; f++)
		sum += FIELD(cs, CORESTATE_TXPEND(f), s16);
	return sum;
}

void bcm4360_stf_env_policy_defer(void *wlc)
{
	FIELD(wlc, WLC_FLAGS_304, u8) |= POLICY_DEFERRED;
}

void bcm4360_stf_env_txc_gen_inc(void *wlc)
{
	FIELD(wlc, WLC_TXC_GEN, u32) += 1;
}

static void *pub_of(void *wlc)
{
	return FIELD(wlc, WLC_PUB, void *);
}

u8 bcm4360_stf_env_boardflags2_hi(void *wlc)
{
	return FIELD(pub_of(wlc), PUB_BOARDFLAGS2_HI, u8);
}

u16 bcm4360_stf_env_shm_core_base(void *wlc, u32 which)
{
	return FIELD(pub_of(wlc), PUB_SHM_CORE_BASE + 2 * which, u16);
}

u32 bcm4360_stf_env_board_vendor(void *wlc)
{
	return FIELD(FIELD(pub_of(wlc), PUB_SIH, void *), SIH_BOARDVENDOR, u32);
}

u32 bcm4360_stf_env_chip(void *wlc)
{
	return FIELD(FIELD(pub_of(wlc), PUB_SIH, void *), SIH_CHIP, u32);
}

u16 bcm4360_stf_env_coex_word(void *wlc)
{
	return FIELD(FIELD(wlc, WLC_COEX, void *), COEX_WORD, u16);
}

bool bcm4360_stf_env_up(void *wlc)
{
	return FIELD(pub_of(wlc), PUB_UP, u8) != 0;
}

u32 bcm4360_stf_env_beacon_rate(void *wlc)
{
	return FIELD(wlc, WLC_BCN_RATE, u32);
}

void bcm4360_stf_env_txant_set(void *wlc, u16 phytxant)
{
	wlc_bmac_txant_set(FIELD(wlc, WLC_HW, void *), phytxant);
}

bool bcm4360_stf_env_valid_rate(void *wlc, u32 rspec, u32 band)
{
	return (u8)wlc_valid_rate(wlc, rspec, (s32)band, 0) != 0;
}

void bcm4360_stf_env_suspend_mac_and_wait(void *wlc)
{
	wlc_suspend_mac_and_wait(wlc);
}

void bcm4360_stf_env_enable_mac(void *wlc)
{
	wlc_enable_mac(wlc);
}

void bcm4360_stf_env_beacon_phytxctl_txant_upd(void *wlc, u32 rspec)
{
	wlc_beacon_phytxctl_txant_upd(wlc, rspec);
}

void bcm4360_stf_env_beacon_phytxctl(void *wlc, u32 rspec, u16 chanspec)
{
	wlc_beacon_phytxctl(wlc, rspec, chanspec);
}

void bcm4360_stf_env_txc_upd(void *wlc)
{
	wlc_txc_upd(wlc);
}

void bcm4360_stf_env_write_shm(void *wlc, u32 offset, u16 value)
{
	wlc_write_shm(wlc, offset, value);
}

u32 bcm4360_stf_env_phy_cap(void *wlc)
{
	return wlc_phy_cap_get(pi_of(wlc));
}

void bcm4360_stf_env_band_stf_ss_set(void *wlc, u8 mode)
{
	wlc_bmac_band_stf_ss_set(FIELD(wlc, WLC_HW, void *), mode);
}

void bcm4360_stf_env_mhf(void *wlc, u8 idx, u16 mask, u16 val, s32 bands)
{
	wlc_mhf(wlc, idx, mask, val, bands);
}

void bcm4360_stf_env_ppr_get_ht_mcs(void *p, u32 bw, u32 nss, u32 mode, u32 chains, s8 *out)
{
	ppr_get_ht_mcs(p, bw, nss, mode, chains, out);
}

void bcm4360_stf_env_ppr_get_vht_mcs(void *p, u32 bw, u32 nss, u32 mode, u32 chains, s8 *out)
{
	ppr_get_vht_mcs(p, bw, nss, mode, chains, out);
}
