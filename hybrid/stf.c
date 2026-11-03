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

#ifdef __KERNEL__
module_param_named(open_stf, bcm4360_open_stf, bool, 0444);
MODULE_PARM_DESC(open_stf, "transmit power offsets of the chains (wlc_update_txppr_offset): open code (1, default) or Broadcom's (0)");
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

/* cca-stf.md, wlc_update_txppr_offset, step 8: force, and the requester id */
#define TXCHAIN_FORCE		1
#define TXCHAIN_ID_TXPPR	3

HYB_TAKE(void, wlc_update_txppr_offset, (void *wlc, void *ofs));

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
