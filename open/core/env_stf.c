// SPDX-License-Identifier: ISC
/*
 * The services of stf.h, cca.h, channel.h and srom.h (hybrid/stf.c, cca.c,
 * chan.c, srom.c with the open functions in place of Broadcom's). `void
 * *wlc` is the core's block of Broadcom's wlc_info_t layout.
 */
#include "core.h"
#include <bcm4360/stf.h>
#include <bcm4360/cca.h>
#include <bcm4360/channel.h>
#include <bcm4360/ratepwr.h>
#include <bcm4360/srom.h>
#include <bcm4360/nvram.h>
#include <bcm4360/phy_api.h>

#define WLCC(wlc)	core_of_wlc(wlc)
#define PUBOF(wlc)	FIELD(wlc, WLC_PUB, void *)
#define PIOF(wlc)	(WLCC(wlc)->phy)

/* the MAC layer's view, for a call into it from the upper layer's code */
#define MAC_CALL(wlc, stmt)	CORE_MAC_CALL(WLCC(wlc), stmt)

/* --- stf (cca-stf.md part 2) --- */

u8 *bcm4360_stf_env_state(void *wlc) { return FIELD(wlc, WLC_STF, u8 *); }
void *bcm4360_stf_env_osh(void *wlc) { return FIELD(wlc, WLC_OSH, void *); }
u16 bcm4360_stf_env_chanspec(void *wlc) { return FIELD(wlc, WLC_CHANSPEC, u16); }
bool bcm4360_stf_env_clk(void *wlc) { return FIELD(wlc, WLC_CLK, u8) != 0; }
u32 bcm4360_stf_env_corerev(void *wlc) { return FIELD(PUBOF(wlc), PUB_COREREV, u32); }

/* the floor of step 1: the hybrid's module parameter, 0 by default */
s32 bcm4360_stf_env_min_txpower(void *wlc)
{
	(void)wlc;
	return 0;
}

s8 bcm4360_stf_env_target_max(void *wlc)
{
	return (s8)bcm4360_phy_txpower_get_target_max(PIOF(wlc));
}

s8 bcm4360_stf_env_target_min(void *wlc)
{
	return (s8)bcm4360_phy_txpower_get_target_min(PIOF(wlc));
}

/* no offload firmware */
void bcm4360_stf_env_ol_curpwr_upd(void *wlc, s32 tmax, u16 cs)
{
	(void)wlc; (void)tmax; (void)cs;
}

/* wlc_stf_txchain_set(wlc, mask, force 1, id 3): TODO wlc-hw (no specification yet) */
void bcm4360_stf_env_txchain_set(void *wlc, s32 mask)
{
	(void)wlc; (void)mask;
}

void bcm4360_stf_env_spatial_policy_set(void *wlc, s32 policy)
{
	bcm4360_stf_spatial_policy_set(wlc, policy);
}

void *bcm4360_stf_env_ppr_create(void *osh, u32 bw) { return bcm4360_ratepwr_create(osh, bw); }
void bcm4360_stf_env_ppr_delete(void *osh, void *p) { bcm4360_ratepwr_delete(osh, p); }
u32 bcm4360_stf_env_ppr_get_ch_bw(void *p) { return bcm4360_ratepwr_get_ch_bw(p); }

void bcm4360_stf_env_ppr_map_vec_all(void (*fn)(void *ctx, s8 *a, s8 *b), void *ctx, void *a,
				     void *b)
{
	bcm4360_ratepwr_map_vec_all(fn, ctx, a, b);
}

void bcm4360_stf_env_ppr_get_dsss(void *p, u32 bw, u32 chains, s8 *out)
{
	bcm4360_ratepwr_get_dsss(p, bw, chains, out);
}

void bcm4360_stf_env_ppr_get_ofdm(void *p, u32 bw, u32 mode, u32 chains, s8 *out)
{
	bcm4360_ratepwr_get_ofdm(p, bw, mode, chains, out);
}

void bcm4360_stf_env_ppr_get_ht_mcs(void *p, u32 bw, u32 nss, u32 mode, u32 chains, s8 *out)
{
	bcm4360_ratepwr_get_ht_mcs(p, bw, nss, mode, chains, out);
}

void bcm4360_stf_env_ppr_get_vht_mcs(void *p, u32 bw, u32 nss, u32 mode, u32 chains, s8 *out)
{
	bcm4360_ratepwr_get_vht_mcs(p, bw, nss, mode, chains, out);
}

void bcm4360_stf_env_get_rateset_shm_offset(void *wlc, const void *rs, u16 off, u8 *buf)
{
	MAC_CALL(wlc, bcm4360_bmac_stf_get_rateset_shm_offset(m, (const u8 *)rs + 4,
							       FIELD(rs, 0, u32), off, buf));
}

void bcm4360_stf_env_set_rateset_shm_offset(void *wlc, const void *rs, u16 off, u8 *buf)
{
	MAC_CALL(wlc, bcm4360_bmac_stf_set_rateset_shm_offset(m, FIELD(rs, 0, u32), off, buf));
}

void *bcm4360_stf_env_band(void *wlc) { return FIELD(wlc, WLC_BAND, void *); }
u32 bcm4360_stf_env_bandtype(void *wlc) { return FIELD(FIELD(wlc, WLC_BAND, void *), 0x00, u32); }
u16 bcm4360_stf_env_phytype(void *wlc) { return FIELD(FIELD(wlc, WLC_BAND, void *), 0x08, u16); }
u8 bcm4360_stf_env_band_ss_mode(void *bs) { return FIELD(bs, 0x58, u8); }

s32 bcm4360_stf_env_txpending_sum(void *wlc)
{
	u8 *cs = FIELD(wlc, WLC_CORE, u8 *);
	s32 sum = 0, f;

	for (f = 0; f < 4; f++)
		sum += FIELD(cs, CORE_TXPKTPEND + 2 * f, s16);
	return sum;
}

void bcm4360_stf_env_policy_defer(void *wlc) { FIELD(wlc, WLC_FLAGS_304, u8) |= 0x40; }
void bcm4360_stf_env_txc_gen_inc(void *wlc) { FIELD(wlc, WLC_TXC_GEN, u32) += 1; }
u8 bcm4360_stf_env_boardflags2_hi(void *wlc) { return FIELD(PUBOF(wlc), PUB_BOARDFLAGS2_HI, u8); }

u16 bcm4360_stf_env_shm_core_base(void *wlc, u32 which)
{
	return FIELD(PUBOF(wlc), PUB_SHM_CORE_BASE + 2 * which, u16);
}

u32 bcm4360_stf_env_board_vendor(void *wlc) { return WLCC(wlc)->sihp.boardvendor; }
u32 bcm4360_stf_env_chip(void *wlc) { return WLCC(wlc)->sihp.chip; }
u16 bcm4360_stf_env_coex_word(void *wlc) { return FIELD(FIELD(wlc, WLC_COEX, void *), 0x14, u16); }
bool bcm4360_stf_env_up(void *wlc) { return FIELD(PUBOF(wlc), PUB_UP, u8) != 0; }
u32 bcm4360_stf_env_beacon_rate(void *wlc) { return FIELD(wlc, WLC_BCN_RATE, u32); }

/* wlc_bmac_txant_set, wlc_valid_rate, the beacon template and the transmit
 * descriptor cache belong to the frame code: TODO wlc-hw / frames */
void bcm4360_stf_env_txant_set(void *wlc, u16 phytxant) { (void)wlc; (void)phytxant; }

bool bcm4360_stf_env_valid_rate(void *wlc, u32 rspec, u32 band)
{
	(void)wlc; (void)rspec; (void)band;
	return true;
}

void bcm4360_stf_env_suspend_mac_and_wait(void *wlc) { MAC_CALL(wlc, bcm4360_bmac_suspend(m)); }
void bcm4360_stf_env_enable_mac(void *wlc) { MAC_CALL(wlc, bcm4360_bmac_enable(m)); }

void bcm4360_stf_env_beacon_phytxctl_txant_upd(void *wlc, u32 rspec)
{
	(void)wlc; (void)rspec;
}

void bcm4360_stf_env_beacon_phytxctl(void *wlc, u32 rspec, u16 chanspec)
{
	(void)wlc; (void)rspec; (void)chanspec;
}

void bcm4360_stf_env_txc_upd(void *wlc) { (void)wlc; }

void bcm4360_stf_env_write_shm(void *wlc, u32 offset, u16 value)
{
	MAC_CALL(wlc, bcm4360_bmac_set_shm(m, offset, value, 2));
}

u32 bcm4360_stf_env_phy_cap(void *wlc) { return bcm4360_phy_cap_get(PIOF(wlc)); }

void bcm4360_stf_env_band_stf_ss_set(void *wlc, u8 mode)
{
	MAC_CALL(wlc, bcm4360_bmac_band_stf_ss_set(m, mode));
}

void bcm4360_stf_env_mhf(void *wlc, u8 idx, u16 mask, u16 val, s32 bands)
{
	MAC_CALL(wlc, bcm4360_bmac_mhf(m, idx, mask, val, bands));
}

/* --- cca (cca-stf.md part 1) --- */

void *bcm4360_cca_env_osh(void *wlc) { return FIELD(wlc, WLC_OSH, void *); }
u16 bcm4360_cca_env_chanspec(void *wlc) { return FIELD(wlc, WLC_CHANSPEC, u16); }

s32 bcm4360_cca_env_stats_read(void *wlc, u32 v[9])
{
	s32 r = 0;

	MAC_CALL(wlc, r = bcm4360_bmac_cca_stats_get(m, v));
	return r;
}

bool bcm4360_cca_env_pm_enabled(void *wlc)
{
	u8 *cfg = FIELD(wlc, WLC_PRIMARY_BSSCFG, u8 *);
	u8 *pm = FIELD(cfg, 0x338, u8 *);

	return pm[8] != 0;
}

void bcm4360_cca_env_read_tsf(void *wlc, u32 *lo, u32 *hi)
{
	MAC_CALL(wlc, bcm4360_bmac_read_tsf(m, lo, hi));
}

/* the module table of the upper layer: the core calls the watchdog itself */
s32 bcm4360_cca_env_module_register(void *wlc, const struct bcm4360_iovar *iovars,
				    const char *name, void *hdl, bcm4360_cca_iovar_fn doiovar,
				    bcm4360_cca_fn watchdog, bcm4360_cca_fn up, bcm4360_cca_fn down)
{
	(void)wlc; (void)iovars; (void)name; (void)hdl; (void)doiovar; (void)watchdog; (void)up;
	(void)down;
	return 0;
}

s32 bcm4360_cca_env_module_unregister(void *wlc, const char *name, void *hdl)
{
	(void)wlc; (void)name; (void)hdl;
	return 0;
}

/* --- channel.h: the regulatory database --- */

/*
 * TODO channel-power: these three have no handle of the core in their
 * signature; the database is in struct bcm4360_core.clm. Until the channel
 * code is specified (and the interface carries the handle) they report "no
 * database" (BCME_ERROR).
 */
int bcm4360_chan_env_clm_limits_params_init(struct bcm4360_clm_limits_params *p)
{
	return bcm4360_clm_limits_params_init(p);
}

int bcm4360_chan_env_clm_country_def(u32 country, struct bcm4360_clm_country_locales *loc)
{
	(void)country; (void)loc;
	return -1;
}

int bcm4360_chan_env_clm_limits(const struct bcm4360_clm_country_locales *loc, u32 band,
				u32 channel, s32 ant_gain, u32 limits_type,
				const struct bcm4360_clm_limits_params *p, s8 *limits)
{
	(void)loc; (void)band; (void)channel; (void)ant_gain; (void)limits_type; (void)p;
	(void)limits;
	return -1;
}

/* --- srom.h (hybrid/srom.c); ctx is the core's sihp --- */

bool bcm4360_srom_sprom_available(void *ctx)
{
	return core_si_is_sprom_available(core_of_sih(ctx));
}

/* srom-otp.md: the helpers keep the current core */
static void *cc_window(struct bcm4360_core *c, u32 *orig)
{
	*orig = bcm4360_si_coreidx(&c->si);
	return bcm4360_si_setcore(&c->si, 0x800, 0);
}

u32 bcm4360_srom_chipcontrol_read(void *ctx)
{
	struct bcm4360_core *c = core_of_sih(ctx);
	u32 orig, v;
	void *cc = cc_window(c, &orig);

	v = hw_read32(cc, CC_CHIPCTL);
	bcm4360_si_setcoreidx(&c->si, orig);
	return v;
}

void bcm4360_srom_chipcontrol_restore(void *ctx, u32 val)
{
	struct bcm4360_core *c = core_of_sih(ctx);
	u32 orig;
	void *cc = cc_window(c, &orig);

	hw_write32(cc, CC_CHIPCTL, val);
	bcm4360_si_setcoreidx(&c->si, orig);
}

void bcm4360_srom_chipcontrol_srom4360(void *ctx, bool on)
{
	struct bcm4360_core *c = core_of_sih(ctx);
	u32 orig, v;
	void *cc = cc_window(c, &orig);

	v = hw_read32(cc, CC_CHIPCTL);
	if (on)
		hw_write32(cc, CC_CHIPCTL, v & 0xffdffcf3);
	bcm4360_si_setcoreidx(&c->si, orig);
}

/* TODO chip-attach: otp_read_region (the SROM image in the OTP of a card without SROM) */
s32 bcm4360_srom_otp_read_region(void *ctx, s32 region, u16 *data, u32 *wlen)
{
	(void)ctx; (void)region; (void)data; (void)wlen;
	return -1;
}

/* the device-path forms of the global NVRAM (chip-attach.md A5): the NVRAM of this driver
 * holds none; TODO when a platform fills it */
const char *bcm4360_srom_getdevpathvar(void *ctx, const char *name)
{
	(void)ctx; (void)name;
	return NULL;
}

const char *bcm4360_srom_getnvramflvar(void *ctx, const char *name)
{
	(void)ctx;
	return bcm4360_nvram_get(name);
}

s32 bcm4360_srom_nvram_getall(void *ctx, char *buf, s32 count)
{
	(void)ctx;
	return bcm4360_nvram_getall(buf, count);
}

s32 bcm4360_srom_devpath(void *ctx, char *path, s32 size)
{
	(void)ctx;
	if (size > 0)
		path[0] = 0;
	return -1;
}

const char *bcm4360_srom_coded_devpathvar(void *ctx, char *out, s32 size, const char *name)
{
	(void)ctx; (void)out; (void)size; (void)name;
	return NULL;
}
