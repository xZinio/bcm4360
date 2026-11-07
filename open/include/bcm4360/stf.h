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
/* wlc_stf_spatial_policy_set(wlc, policy): 0, or -23 for other PHY types */
s32 bcm4360_stf_spatial_policy_set(void *wlc, s32 policy);
/* wlc_stf_spatialpolicy_set_complete(wlc) */
void bcm4360_stf_spatialpolicy_set_complete(void *wlc);
/* wlc_stf_txcore_shmem_write(wlc, force) */
void bcm4360_stf_txcore_shmem_write(void *wlc, bool force);
/* wlc_stf_ss_update(wlc, bs): bs a band state; always 0 */
s32 bcm4360_stf_ss_update(void *wlc, void *bs);
/* wlc_stf_ss_algo_channel_get(wlc, out, chanspec) */
void bcm4360_stf_ss_algo_channel_get(void *wlc, u16 *out, u32 chanspec);
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
/* wlc_update_txppr_offset step 1: the minimum transmit power in dBm (the
 * iovar "min_txpower", which the driver does not have; Broadcom's code reads
 * an uninitialised int there). 0 = no floor, as the specification says;
 * module parameter stf_min_txpower (cca-stf.md, open question 1) */
s32 bcm4360_stf_env_min_txpower(void *wlc);
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
/* the current band state (wlc+0x40) and its fields: band type (1 = 5 GHz,
 * 2 = 2.4 GHz), PHY type (0xb AC, 7 HT) */
void *bcm4360_stf_env_band(void *wlc);
u32 bcm4360_stf_env_bandtype(void *wlc);
u16 bcm4360_stf_env_phytype(void *wlc);
/* the band state's single-stream mode (bs+0x58) */
u8 bcm4360_stf_env_band_ss_mode(void *bs);
/* the sum of the transmit-pending counts of FIFOs 0..3 */
s32 bcm4360_stf_env_txpending_sum(void *wlc);
/* wlc+0x304 |= 0x40: the spatial policy completes when the FIFOs drain */
void bcm4360_stf_env_policy_defer(void *wlc);
/* wlc+0x2a8 += 1: the transmit header cache generation */
void bcm4360_stf_env_txc_gen_inc(void *wlc);
/* the u8 at pub+0x9d (boardflags2 bits 8..15) */
u8 bcm4360_stf_env_boardflags2_hi(void *wlc);
/* pub+0xe4, pub+0xe6: the word addresses of the core words in shared memory */
u16 bcm4360_stf_env_shm_core_base(void *wlc, u32 which);
/* board vendor and chip id of the backplane (sih+0x30, sih+0x3c) */
u32 bcm4360_stf_env_board_vendor(void *wlc);
u32 bcm4360_stf_env_chip(void *wlc);
/* the u16 at (wlc+0x690)+0x14 (the coexistence module's state) */
u16 bcm4360_stf_env_coex_word(void *wlc);
/* pub+0x34: the driver is up */
bool bcm4360_stf_env_up(void *wlc);
/* the beacon's rate (wlc+0x584) */
u32 bcm4360_stf_env_beacon_rate(void *wlc);
/* the object's functions */
void bcm4360_stf_env_txant_set(void *wlc, u16 phytxant);		/* wlc_bmac_txant_set */
bool bcm4360_stf_env_valid_rate(void *wlc, u32 rspec, u32 band);	/* wlc_valid_rate(.., 0) */
void bcm4360_stf_env_suspend_mac_and_wait(void *wlc);
void bcm4360_stf_env_enable_mac(void *wlc);
void bcm4360_stf_env_beacon_phytxctl_txant_upd(void *wlc, u32 rspec);
void bcm4360_stf_env_beacon_phytxctl(void *wlc, u32 rspec, u16 chanspec);
void bcm4360_stf_env_txc_upd(void *wlc);
void bcm4360_stf_env_write_shm(void *wlc, u32 offset, u16 value);	/* wlc_write_shm */
u32 bcm4360_stf_env_phy_cap(void *wlc);				/* wlc_phy_cap_get(pi) */
void bcm4360_stf_env_band_stf_ss_set(void *wlc, u8 mode);		/* wlc_bmac_band_stf_ss_set */
void bcm4360_stf_env_mhf(void *wlc, u8 idx, u16 mask, u16 val, s32 bands);	/* wlc_mhf */
void bcm4360_stf_env_ppr_get_ht_mcs(void *p, u32 bw, u32 nss, u32 mode, u32 chains, s8 *out);
void bcm4360_stf_env_ppr_get_vht_mcs(void *p, u32 bw, u32 nss, u32 mode, u32 chains, s8 *out);
/* wlc_bmac_stf_get/set_rateset_shm_offset(wlc_hw, rs, off, buf) (bmac-run R6);
 * rs is a rate set (u32 count, then the rate bytes) */
void bcm4360_stf_env_get_rateset_shm_offset(void *wlc, const void *rs, u16 off, u8 *buf);
void bcm4360_stf_env_set_rateset_shm_offset(void *wlc, const void *rs, u16 off, u8 *buf);

#endif
