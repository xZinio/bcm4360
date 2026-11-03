/* SPDX-License-Identifier: ISC */
/*
 * The per-rate transmit power offsets of the transmit chains ("space-time
 * features", Broadcom's wlc_stf.c): after every computation of the PHY's
 * transmit power targets, the PHY's per-rate offsets are mapped into the
 * driver's own container, the chains that cannot reach the minimum power
 * are taken out, the spatial policy of the band is applied and the legacy
 * rates' offsets go into the microcode's rate table.
 *
 * Specification: docs/re/spec/cca-stf.md, "Procedures, part 2":
 * wlc_update_txppr_offset and its callback sub_172c6c, with sub_174cd5
 * (the spatial policy of the band). The state (the stf block at wlc+0x550)
 * is Broadcom's and read by other code: it is used in place. Everything of
 * the driver is reached through the services below, with the caller's
 * handle `wlc`.
 */
#ifndef BCM4360_STF_H
#define BCM4360_STF_H

#include <bcm4360/types.h>

/* wlc_update_txppr_offset(wlc, ofs): ofs is the PHY's offsets container */
void bcm4360_stf_update_txppr_offset(void *wlc, void *ofs);
/* sub_172c6c, the callback of ppr_map_vec_all (ctx: the ceiling, one byte) */
void bcm4360_stf_txppr_mapfn(void *ctx, s8 *a, s8 *b);

/* services of the caller (cca-stf.md, Data: "The stf state", "Fields of wlc") */

/* the stf block (wlc+0x550) */
u8 *bcm4360_stf_env_state(void *wlc);
/* the OS handle (wlc+0x08) */
void *bcm4360_stf_env_osh(void *wlc);
/* the upper layer's current chanspec (the low 16 bits of wlc+0x518) */
u16 bcm4360_stf_env_chanspec(void *wlc);
/* wlc+0x31 (set by wlc_up, cleared by wlc_down) */
bool bcm4360_stf_env_clk(void *wlc);
/* the MAC core revision (pub+0x14) */
u32 bcm4360_stf_env_corerev(void *wlc);
/* wlc_phy_txpower_get_target_max/min(pi) of the current band's PHY (s8) */
s8 bcm4360_stf_env_target_max(void *wlc);
s8 bcm4360_stf_env_target_min(void *wlc);
/* wlc_ol_curpwr_upd(wlc+0x750, tmax, cs) */
void bcm4360_stf_env_ol_curpwr_upd(void *wlc, s32 tmax, u16 cs);
/* wlc_stf_txchain_set(wlc, mask, 1, 3) */
void bcm4360_stf_env_txchain_set(void *wlc, s32 mask);
/* wlc_stf_spatial_policy_set(wlc, policy) (result dropped) */
void bcm4360_stf_env_spatial_policy_set(void *wlc, s32 policy);
/* the per-rate power container (ppr.md): ppr_create, ppr_delete,
 * ppr_get_ch_bw, ppr_map_vec_all, ppr_get_dsss, ppr_get_ofdm */
void *bcm4360_stf_env_ppr_create(void *osh, u32 bw);
void bcm4360_stf_env_ppr_delete(void *osh, void *p);
u32 bcm4360_stf_env_ppr_get_ch_bw(void *p);
void bcm4360_stf_env_ppr_map_vec_all(void (*fn)(void *ctx, s8 *a, s8 *b), void *ctx,
				     void *a, void *b);
void bcm4360_stf_env_ppr_get_dsss(void *p, u32 bw, u32 chains, s8 *out);
void bcm4360_stf_env_ppr_get_ofdm(void *p, u32 bw, u32 mode, u32 chains, s8 *out);
/* wlc_bmac_stf_get/set_rateset_shm_offset(wlc_hw, rs, off, buf) (bmac-run R6);
 * rs is a rate set (u32 count, then the rate bytes) */
void bcm4360_stf_env_get_rateset_shm_offset(void *wlc, const void *rs, u16 off, u8 *buf);
void bcm4360_stf_env_set_rateset_shm_offset(void *wlc, const void *rs, u16 off, u8 *buf);

#endif
