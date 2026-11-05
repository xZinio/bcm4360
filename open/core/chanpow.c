// SPDX-License-Identifier: ISC
/*
 * The regulatory transmit power limits of a channel: country at attach, the
 * regulatory database lookup, the per-rate container handed to the PHY
 * (docs/re/spec/channel-power.md, sections 1 and 3.2). The OLPC engine
 * (section 4) only acts while associated: TODO, not here.
 */
#include "core.h"
#include <bcm4360/clm.h>
#include <bcm4360/ratepwr.h>
#include <bcm4360/phy_api.h>
#include <bcm4360/stf.h>

#define CHANVEC		28
#define STF(c)		((u8 *)(c)->stf_state)

/* channel.md: the chanspec's bandwidth bits (>> 11): 2 = 20, 3 = 40, 4 = 80 MHz */
#define CS_BW(cs)	(((cs) >> 11) & 7)

static bool chan_bit(const u8 *v, u32 n)
{
	return n < CHANVEC * 8 && ((v[n >> 3] >> (n & 7)) & 1);
}

/* wlc_get_flags (1.5): the locale flag byte of a band; false if the database has none */
static bool get_flags(struct bcm4360_core *c, const struct bcm4360_clm_country_locales *loc,
		      u32 band, u8 *out)
{
	u64 F;
	u8 f = 0;

	if (bcm4360_clm_country_flags(&c->clm, loc, band, &F))
		return false;
	switch (F & 3) {
	case 1: f = 0x22; break;
	case 2: f = 0x02; break;
	default: break;
	}
	if (F & BIT(2))
		f |= 0x80;
	if (F & BIT(3))
		f |= 0x40;
	if (F & (1u << 29)) {
		f |= 0x10;
	} else {
		if (F & (1u << 30))
			f |= 0x08;
		if ((u32)(F >> 31) & 1)
			f |= 0x04;
	}
	if (F & (band ? BIT(27) : BIT(28)))
		f |= 0x01;
	*out = f;
	return true;
}

/* sub_06f468, wlc_country_lookup_rev (1.2); 0 = found */
static int country_lookup(struct bcm4360_core *c, const char *code, s32 rev_in, char *mapped,
			  u32 *rev, u32 *id)
{
	struct core_wlchw *w = &c->w;
	char cc[2];
	u32 r = 0, agg;
	bool mapped_by_agg = false;

	if (code[0] && code[1] && code[2] && code[3])
		return 1;
	mapped[0] = code[0];
	mapped[1] = code[1];
	mapped[2] = 0;
	if (rev_in >= 0) {
		r = (u32)rev_in;
	} else if (!hw_memcmp(code, w->srom_ccode, 3)) {
		r = w->srom_regrev;
	} else {
		if (w->srom_ccode[0]) {
			cc[0] = w->srom_ccode[0];
			cc[1] = w->srom_ccode[1];
		} else {
			cc[0] = cc[1] = 'w';
		}
		if (!bcm4360_clm_agg_country_lookup(&c->clm, cc, w->srom_regrev & 0xff, &agg)) {
			if (!bcm4360_clm_agg_country_map_lookup(&c->clm, agg, mapped, &r))
				mapped_by_agg = true;
		}
	}
	if (rev_in < 0 && !mapped_by_agg && mapped[0] == 'J' && mapped[1] == 'P' && r == 0 &&
	    FIELD(c->pub, PUB_COREREV, u32) > 5 &&
	    (w->srom_ccode[0] != 'J' || w->srom_ccode[1] != 'P' ||
	     FIELD(c->pub, 0x94, u8) < 3))
		r = 1;
	cc[0] = mapped[0];
	cc[1] = mapped[1];
	if (bcm4360_clm_country_lookup(&c->clm, cc, r, id)) {
		if (!r)
			return 1;
		r = 0;
		if (bcm4360_clm_country_lookup(&c->clm, cc, 0, id))
			return 1;
	}
	*rev = r;
	return 0;
}

/* sub_072656, wlc_channels_commit (1.4): valid, restricted, quiet and radar channels */
static void channels_commit(struct bcm4360_core *c, u32 id)
{
	struct core_wlchw *w = &c->w;
	struct bcm4360_clm_country_locales loc;
	u8 tmpv[CHANVEC], tmpr[CHANVEC], vec[32], restricted[CHANVEC];
	u32 b, n;

	hw_memset(restricted, 0, sizeof(restricted));
	hw_memset(w->radar, 0, sizeof(w->radar));
	if (bcm4360_clm_country_def(&c->clm, id, &loc))
		return;
	for (b = 0; b < 2; b++) {
		u8 f = 0;

		hw_memset(tmpv, 0, sizeof(tmpv));
		hw_memset(tmpr, 0, sizeof(tmpr));
		hw_memset(vec, 0, sizeof(vec));
		get_flags(c, &loc, b, &f);
		w->locale_flags[b] = f;
		bcm4360_clm_country_channels(&c->clm, &loc, b, tmpv, tmpr);
		for (n = 0; n < CHANVEC; n++)
			restricted[n] |= tmpr[n];
		if (b == 1 && (f & 0x02)) {
			for (n = 0; n < 224; n++)
				if (chan_bit(tmpv, n) &&
				    ((n >= 52 && n <= 64) || (n >= 100 && n <= 140)))
					w->radar[n >> 3] |= 1 << (n & 7);
		}
		if (c->phy)
			bcm4360_phy_chanspec_band_validch(c->phy, b ? 1 : 2, vec);
		for (n = 0; n < CHANVEC; n++)
			w->valid[b][n] = tmpv[n] & vec[n];
	}
	/* wlc_quiet_channels_reset: the restricted channels; the radar ones with 802.11h (off) */
	hw_memcpy(w->quiet, restricted, CHANVEC);
}

/* sub_072da4, wlc_set_country_common, and sub_072656 step 7 (1.3, 1.4) */
static void country_commit(struct bcm4360_core *c, u32 id)
{
	struct bcm4360_clm_country_locales loc;
	u8 f = 0, *stf = STF(c);

	if (!bcm4360_clm_country_def(&c->clm, id, &loc)) {
		get_flags(c, &loc, 0, &f);
		if ((f & 0x10) && (FIELD(CORE_WLC_HW(c), 0x118, u32) == 2))
			get_flags(c, &loc, 1, &f);
		stf[0x10] = (f & 0x10) ? 1 : 0;
	}
	bcm4360_stf_ss_update(c->wlc, c->uband[0]);
	bcm4360_stf_ss_update(c->wlc, c->uband[1]);
	channels_commit(c, id);
	if (c->phy)
		bcm4360_phy_chanspec_ch14_widefilter_set(c->phy, false);
	if (c->up) {
		u16 cs = FIELD(c->wlc, WLC_CHANSPEC, u16);
		void *p = core_cp_limits(c, cs);

		if (p) {
			bcm4360_ratepwr_apply_max(p, 127);
			bcm4360_phy_txpower_limit_set(c->phy, p);
			core_cp_free(c, p);
		}
	}
}

/* wlc_set_countrycode_rev with rev = -1 (1.3): 0 or -2 */
int core_cp_set_country(struct bcm4360_core *c, const char *code)
{
	struct core_wlchw *w = &c->w;
	char mapped[4], adv[2];
	u32 rev = 0, id;

	if (!c->clm_ok)
		return -2;
	if (country_lookup(c, code, -1, mapped, &rev, &id))
		return -2;
	w->country = id;
	w->regrev = rev;
	w->ccode[0] = mapped[0];
	w->ccode[1] = mapped[1];
	w->ccode[2] = 0;
	if (!bcm4360_clm_country_advertised_cc(&c->clm, id, adv)) {
		w->abbrev[0] = adv[0];
		w->abbrev[1] = adv[1];
	} else {
		w->abbrev[0] = mapped[0];
		w->abbrev[1] = mapped[1];
	}
	w->abbrev[2] = 0;
	w->country_ok = 1;
	country_commit(c, id);
	return 0;
}

/* wlc_channel_mgr_attach (1.1), the part after wlc_bmac_attach */
void core_cp_attach(struct bcm4360_core *c)
{
	struct core_wlchw *w = &c->w;
	char code[4], mapped[4];
	const char *s;
	u32 rev, id, i;

	if (!c->clm_ok)
		return;
	hw_memset(w->srom_ccode, 0, 4);
	s = core_getvar(c->vars, "ccode");
	if (s)
		for (i = 0; i < 3 && s[i]; i++)
			w->srom_ccode[i] = s[i];
	w->srom_regrev = (u32)core_getintvar(c->vars, "regrev");
	hw_memcpy(code, w->srom_ccode, 4);
	if (country_lookup(c, code, -1, mapped, &rev, &id)) {
		code[0] = 'U'; code[1] = 'S'; code[2] = 0;
		(void)country_lookup(c, code, -1, mapped, &rev, &id);
	}
	if (!(c->sihp.boardvendor == 0x103c &&
	      ((code[0] == 'U' && code[1] == 'S') || (code[0] == 'J' && code[1] == 'P') ||
	       (code[0] == 'I' && code[1] == 'L'))) && code[0] != 'X') {
		code[0] = 'X'; code[1] = 'W'; code[2] = 0;
	}
	hw_memcpy(c->ccode, code, 3);
	(void)core_cp_set_country(c, code);
}

/* wlc_quiet_chanspec (2.2 step 6) */
bool core_cp_quiet(struct bcm4360_core *c, u16 cs)
{
	u32 n = cs & 0xff;
	u32 bw = CS_BW(cs);

	if (bw == 4)
		return chan_bit(c->w.quiet, n - 6) || chan_bit(c->w.quiet, n - 2) ||
		       chan_bit(c->w.quiet, n + 2) || chan_bit(c->w.quiet, n + 6);
	if (bw == 3)
		return chan_bit(c->w.quiet, n - 2) || chan_bit(c->w.quiet, n + 2);
	return chan_bit(c->w.quiet, n);
}

/* one group of the CLM output into the container (1.6 step 8) */
struct grp {
	u16 idx;
	u8 kind;	/* 0 DSSS, 1 OFDM, 2 VHT MCS, 3 HT MCS */
	u8 nss, mode, chains;
	u8 need_t;	/* least number of transmit streams */
	u8 txbf;
};

static const struct grp groups[] = {
	{ 0, 0, 0, 0, 1, 1, 0 }, { 4, 1, 0, 0, 1, 1, 0 }, { 12, 2, 1, 0, 1, 1, 0 },
	{ 24, 0, 0, 0, 2, 2, 0 }, { 28, 1, 0, 2, 2, 2, 0 }, { 36, 2, 1, 2, 2, 2, 0 },
	{ 48, 2, 2, 1, 2, 2, 0 }, { 60, 2, 2, 0, 2, 2, 0 },
	{ 104, 0, 0, 0, 3, 3, 0 }, { 108, 1, 0, 2, 3, 3, 0 }, { 116, 2, 1, 2, 3, 3, 0 },
	{ 128, 2, 2, 1, 3, 3, 0 }, { 140, 2, 2, 0, 3, 3, 0 }, { 152, 2, 3, 0, 3, 3, 0 },
	{ 72, 1, 0, 3, 2, 2, 1 }, { 80, 2, 1, 3, 2, 2, 1 }, { 92, 3, 2, 3, 2, 2, 1 },
	{ 164, 1, 0, 3, 3, 3, 1 }, { 172, 2, 1, 3, 3, 3, 1 }, { 184, 2, 2, 3, 3, 3, 1 },
	{ 196, 3, 3, 3, 3, 3, 1 },
};

static void copy_groups(struct bcm4360_core *c, void *p, u32 sel, const s8 *lim, bool dsss)
{
	u8 t = STF(c)[2];
	bool txbf = FIELD(c->pub, 0xe8, u8) != 0;
	u32 i;

	for (i = 0; i < sizeof(groups) / sizeof(groups[0]); i++) {
		const struct grp *g = &groups[i];
		const s8 *v = lim + g->idx;

		if (g->need_t > t || (g->txbf && !txbf) || (g->kind == 0 && !dsss))
			continue;
		switch (g->kind) {
		case 0: bcm4360_ratepwr_set_dsss(p, sel, g->chains, v); break;
		case 1: bcm4360_ratepwr_set_ofdm(p, sel, g->mode, g->chains, v); break;
		case 2: bcm4360_ratepwr_set_vht_mcs(p, sel, g->nss, g->mode, g->chains, v); break;
		default: bcm4360_ratepwr_set_ht_mcs(p, sel, g->nss, g->mode, g->chains, v); break;
		}
	}
}

/* wlc_channel_reg_limits (1.6): a container for chanspec cs; every rate disabled on failure */
void *core_cp_limits(struct bcm4360_core *c, u16 cs)
{
	struct core_wlchw *w = &c->w;
	struct bcm4360_clm_country_locales loc;
	struct bcm4360_clm_limits_params par;
	s8 lw[BCM4360_CLM_NUMRATES], ls[BCM4360_CLM_NUMRATES], lh[BCM4360_CLM_NUMRATES];
	u32 bw = CS_BW(cs), pbw = bw == 4 ? 2 : (bw == 3 ? 1 : 0), unit, band, ch = cs & 0xff;
	u32 sb = cs & 0x700;
	s32 gain;
	void *p = bcm4360_ratepwr_create(c->osh, pbw);

	if (!p)
		return NULL;
	bcm4360_ratepwr_clear(p);
	if (!w->country_ok || bcm4360_clm_limits_params_init(&par))
		return p;
	unit = (cs & 0xc000) == 0xc000 ? 1 : 0;
	band = unit;
	gain = FIELD(c->uband[unit], 0x108, s8);
	FIELD(c->uband[unit], 0x109, u8) = FIELD(c->uband[unit], 0x110, u8);
	par.sar = 0x7f;
	par.bw = pbw;
	par.category = 0;
	if (bcm4360_clm_country_def(&c->clm, w->country, &loc))
		return p;
	if (c->phy)
		bcm4360_phy_set_filt_war(c->phy, false);
	if (bcm4360_clm_limits(&c->clm, &loc, band, ch, gain, 0, &par, lw))
		return p;
	if (pbw == 2) {
		if (bcm4360_clm_limits(&c->clm, &loc, band, ch, gain,
				       (sb == 0x200 || sb == 0x300) ? 2 : 1, &par, lh))
			return p;
		if (bcm4360_clm_limits(&c->clm, &loc, band, ch, gain,
				       sb == 0 ? 3 : (sb == 0x100 ? 4 : (sb == 0x200 ? 5 : 6)),
				       &par, ls))
			return p;
	} else if (pbw == 1) {
		if (bcm4360_clm_limits(&c->clm, &loc, band, ch, gain, sb == 0x100 ? 2 : 1, &par, ls))
			return p;
	}
	if (pbw == 0) {
		copy_groups(c, p, 0, lw, true);
	} else if (pbw == 1) {
		copy_groups(c, p, 1, lw, false);
		copy_groups(c, p, 0, ls, true);
		copy_groups(c, p, 3, ls, true);
	} else {
		copy_groups(c, p, 2, lw, false);
		copy_groups(c, p, 5, lh, false);
		copy_groups(c, p, 0, ls, true);
		copy_groups(c, p, 4, ls, true);
	}
	return p;
}

void core_cp_free(struct bcm4360_core *c, void *p)
{
	bcm4360_ratepwr_delete(c->osh, p);
}
