/*
 * What Broadcom's upper layer (wlc.c and its modules) does to the hardware,
 * as the open core does it: attach, up and init, channel change, periodic
 * work, down and detach, the BSS state, the receive filters and scanning.
 *
 * Specification: docs/re/spec/wlc-hw.md (section numbers in the comments of
 * the functions). The fields that the open modules (stf, cca, the MAC layer)
 * read at Broadcom's offsets are kept in the blocks of Broadcom's layout
 * (core.h); the rest is in struct core_wlchw (wlchw.h).
 */
#include "core.h"
#include <bcm4360/bmac_attach.h>
#include <bcm4360/bmac_led.h>
#include <bcm4360/phy_api.h>
#include <bcm4360/stf.h>
#include <bcm4360/rate.h>
#include <bcm4360/dma.h>
#include <bcm4360/ratepwr.h>

#define TICK_MS			1000

/* D11 registers and bits used here (wlc-hw.md, bmac-init.md A2) */
#define D11_MACCONTROL		0x120
#define D11_MACCAPS		0x158
#define D11_TSF_LO		0x180
#define D11_TSF_HI		0x184
#define D11_TSF_CFPSTART	0x188	/* beacon period << 16 */
#define D11_TSF_CFPREP		0x18c	/* next TBTT */
#define D11_PRETBTT		0x612
#define D11_WME_REG		0x688
#define D11_RANDOM		0x65a
#define D11_BT_STATE		0x6c2
#define D11_REG_3DC		0x3dc
#define D11_REG_A00		0xa00
#define D11_FIFO_INTSTATUS(i)	(0x20 + 8 * (i))
#define D11_RXFIFO_PTR		0x224

#define MCTL_PSM_RUN		0x00000002
#define MCTL_BCNS_PROMISC	0x00100000
#define MCTL_KEEPCONTROL	0x00400000
#define MCTL_PROMISC		0x01000000
#define MCTL_HPS		0x02000000
#define MCTL_WAKE		0x04000000
#define MCTL_TBTT_HOLD		0x10000000

/* shared memory (wlc-hw.md) */
#define SHM_UCODE_MAJOR		0x000
#define SHM_UCODE_MINOR		0x002
#define SHM_DTIM		0x012
#define SHM_BAND_WORD		0x01c
#define SHM_KEY_BASE		0x056
#define SHM_3C			0x03c
#define SHM_4C			0x04c
#define SHM_74			0x074
#define SHM_LIFETIME		0x07c
#define SHM_82			0x082
#define SHM_PRETBTT		0x096
#define SHM_TXBF_BASE		0x0b0
#define SHM_BCN_LI		0x0b6
#define SHM_BA			0x0ba
#define SHM_DUTY_CCK		0x0a4
#define SHM_DUTY_OFDM		0x0b4
#define SHM_D6			0x0d6
#define SHM_STATS		0x0e0
#define SHM_RDIR_OFDM		0x1c0
#define SHM_RDIR_CCK		0x200
#define SHM_RTAB_OFDM		0x1e0
#define SHM_RTAB_CCK		0x220
#define SHM_EDCA_BASE		0x240
#define SHM_RETRY		0x300
#define SHM_SECINDEX		0x5e0

#define SEL_SHM			0x10000

/* addresses of the hardware key table etc. (wlc-hw.md "Data", rev >= 40) */
#define KEY_ENTRIES		54
#define SECINDEX_WORDS		68
#define SHM_CORE_BASE_WORD	0x2ea
#define AMT_OWN			63
#define AMT_BSSID		62
#define AMT_KEYS		50
#define AMT_ATTR_OWN		0x8008
#define AMT_ATTR_BSS_INFRA	0x8006
#define TPL_BSS			0x30
#define TPL_OWN			0x48

#define PHYTYPE_AC		11
#define JOIN_TIMEOUT_TICKS	15
#define AMT_ATTR_JOIN		0x8002

/* the chanspec's band: 0xc000 = 5 GHz */
static u32 band_unit_of(u16 cs)
{
	return (cs & 0xc000) == 0xc000 ? 1 : 0;
}

static u32 popcount8(u32 v)
{
	u32 n = 0;

	for (v &= 0xff; v; v >>= 1)
		n += v & 1;
	return n;
}

#define STF(c)		((u8 *)(c)->stf_state)

static u16 shm_r(struct bcm4360_core *c, u32 off)
{
	return bcm4360_shm_read(c->hw, (u16)off);
}

static void shm_w(struct bcm4360_core *c, u32 off, u16 v)
{
	bcm4360_shm_write(c->hw, (u16)off, v);
}

static void mctrl(struct bcm4360_core *c, u32 mask, u32 val)
{
	CORE_MAC_CALL(c, bcm4360_bmac_mctrl(m, mask, val));
}

static void mhf(struct bcm4360_core *c, u8 idx, u16 mask, u16 val, u32 bands)
{
	CORE_MAC_CALL(c, bcm4360_bmac_mhf(m, idx, mask, val, bands));
}

static void mac_suspend(struct bcm4360_core *c)
{
	CORE_MAC_CALL(c, bcm4360_bmac_suspend(m));
}

static void mac_enable(struct bcm4360_core *c)
{
	CORE_MAC_CALL(c, bcm4360_bmac_enable(m));
}

static s32 var_int(struct bcm4360_core *c, const char *name, s32 def)
{
	return core_getvar(c->vars, name) ? core_getintvar(c->vars, name) : def;
}

static bool driver_up(struct bcm4360_core *c)
{
	return FIELD(c->pub, PUB_UP, u8) != 0;
}

/* the upper layer's current band: unit 0 = 2.4 GHz, 1 = 5 GHz */
static u32 ubs_unit(struct bcm4360_core *c)
{
	return FIELD(FIELD(c->wlc, WLC_BAND, void *), 0x04, u32);
}

/* ---------------------------------------------------------------------- */
/* rates: wlc-hw.md 1.6                                                    */
/* ---------------------------------------------------------------------- */

/* bmac-run.md R6, rate_info: the PLCP code of a rate (bit 7: OFDM) */
static u8 rate_code(u32 r)
{
	switch (r) {
	case 2: return 0x0a;
	case 4: return 0x14;
	case 11: return 0x37;
	case 12: return 0x8b;
	case 18: return 0x8f;
	case 22: return 0x6e;
	case 24: return 0x8a;
	case 36: return 0x8e;
	case 48: return 0x89;
	case 72: return 0x8d;
	case 96: return 0x88;
	case 108: return 0x8c;
	default: return 0;
	}
}

/* wlc_rate_lookup_init(wlc, rs): the band's response-rate map from the basic rates of rs */
static void rate_lookup_init(struct bcm4360_core *c, const struct bcm4360_rateset *rs)
{
	struct core_wlchw *w = &c->w;
	u32 unit = ubs_unit(c), i;
	u8 *m = w->rmap[unit];
	const struct bcm4360_rateset *hw = &w->hwrs[unit];
	u8 bc = 0, bo = 0;

	hw_memset(m, 0, WLCHW_RATEMAP);
	for (i = 0; i < rs->count && i < 16; i++) {
		u8 r = rs->rates[i];

		if ((r & 0x80) && (r & 0x7f) < WLCHW_RATEMAP)
			m[r & 0x7f] = r & 0x7f;
	}
	for (i = 0; i < hw->count && i < 16; i++) {
		u8 r = hw->rates[i] & 0x7f;
		bool ofdm = rate_code(r) & 0x80;

		if (r >= WLCHW_RATEMAP)
			continue;
		if (m[r]) {
			if (ofdm)
				bo = r;
			else
				bc = r;
			continue;
		}
		m[r] = ofdm ? bo : bc;
		if (!m[r]) {
			if (ofdm)
				m[r] = r < 24 ? 12 : (r < 48 ? 24 : 48);
			else
				m[r] = r;
		}
	}
}

/* wlc_set_ratetable (1.6): the response-rate table SHM(0x1e0..) / SHM(0x220..) */
static void set_ratetable(struct bcm4360_core *c)
{
	struct core_wlchw *w = &c->w;
	u32 unit = ubs_unit(c), i;
	struct bcm4360_rateset rs;
	const u8 *m = w->rmap[unit];

	bcm4360_rateset_copy(&w->hwrs[unit], &rs);
	bcm4360_rateset_mcs_upd(&rs, FIELD(c->stf_state, 2, u8));
	for (i = 0; i < rs.count && i < 16; i++) {
		u32 r = rs.rates[i] & 0x7f, b = 0, ref;
		u8 rc = rate_code(r), bc;
		u16 word;

		ref = bcm4360_rate_rspec_reference_rate(r);
		if (ref < WLCHW_RATEMAP)
			b = m[ref];
		if (!b)
			b = rs.rates[0] & 0x7f;
		bc = rate_code(b);
		word = (bc & 0x80) ? w->rdir_ofdm[bc & 0xf] : w->rdir_cck[bc & 0xf];
		shm_w(c, ((rc & 0x80) ? SHM_RTAB_OFDM : SHM_RTAB_CCK) + 2 * (rc & 0xf), word);
	}
}

/* the default rate set of a band (wlc_default_rateset) and the hardware set (1.4 step 14) */
static void rates_band_init(struct bcm4360_core *c, u32 unit)
{
	struct core_wlchw *w = &c->w;
	struct bcm4360_rateset rs;
	u32 save = ubs_unit(c);
	void *wlc = c->wlc;

	bcm4360_rateset_default(&rs, NULL, PHYTYPE_AC, unit ? 1 : 2, false, 0x7f, 3, 20,
				FIELD(c->stf_state, 2, u8));
	bcm4360_rateset_filter(&rs, &w->hwrs[unit], false, 0, 0x7f, 3);
	bcm4360_rateset_bw_mcs_filter(&w->hwrs[unit], 20);
	/* the map is made for the band the upper layer is in */
	FIELD(wlc, WLC_BAND, void *) = FIELD(wlc, WLC_BANDSTATE(unit), void *);
	rate_lookup_init(c, &rs);
	FIELD(wlc, WLC_BAND, void *) = FIELD(wlc, WLC_BANDSTATE(save), void *);
}

/* ---------------------------------------------------------------------- */
/* MAC control bits: wlc-hw.md 1.7, 1.8, 5.6, 6.1                          */
/* ---------------------------------------------------------------------- */

/* wlc_enable_probe_req (1.8) */
static void enable_probe_req(struct bcm4360_core *c, u8 mask, u8 value)
{
	struct core_wlchw *w = &c->w;

	w->probe_req = (value & mask) | (w->probe_req & ~mask);
	if (driver_up(c))
		mhf(c, 4, 0x8000, w->probe_req ? 0 : 0x8000, 3);
}

/* wlc_mac_bcn_promisc (6.1) */
static void mac_bcn_promisc(struct bcm4360_core *c)
{
	struct core_wlchw *w = &c->w;
	bool want = (w->filter & BCM4360_FILTER_BEACONS) || w->bcn_promisc_scan;

	mctrl(c, MCTL_BCNS_PROMISC, want ? MCTL_BCNS_PROMISC : 0);
}

/* wlc_mac_promisc (6.1) */
static void mac_promisc(struct bcm4360_core *c)
{
	struct core_wlchw *w = &c->w;
	u32 v = 0;

	if (w->filter & BCM4360_FILTER_PROMISC)
		v = MCTL_PROMISC;
	if (w->filter & BCM4360_FILTER_CONTROL)
		v = MCTL_PROMISC | MCTL_KEEPCONTROL;
	mctrl(c, MCTL_PROMISC | MCTL_KEEPCONTROL, v);
	enable_probe_req(c, 4, (v & MCTL_PROMISC) ? 4 : 0);
}

/* sub_029597, wlc_ucode_mac_upd (1.7) */
static void ucode_mac_upd(struct bcm4360_core *c)
{
	struct core_wlchw *w = &c->w;

	if (w->home_chanspec == FIELD(c->wlc, WLC_CHANSPEC, u16) && w->associated)
		shm_w(c, SHM_BAND_WORD, 0);
	mac_bcn_promisc(c);
	mac_promisc(c);
}

/* wlc_stay_awake (5.6): what keeps the WAKE bit */
static bool stay_awake(struct bcm4360_core *c)
{
	return c->w.scanning != 0 || c->ctl.joining || core_ps_wake_req(c);	/* ps-keys.md 1.1: the requests of ps.c */
}

/* sub_03a14c, wlc_set_wake_ctrl (1.4 step 29) */
static void set_wake_ctrl(struct bcm4360_core *c)
{
	bool s = stay_awake(c);
	u32 budget = 0, waited = 0;

	(void)d11_read32(c->hw, D11_MACCONTROL);
	mctrl(c, MCTL_WAKE, s ? MCTL_WAKE : 0);
	if (!s)
		return;
	/* bmac-init A5 (wlc_bmac_wait_for_wake, no open entry point): a delay of 40 us, then
	 * until the microcode state SHM(0x40) is not "asleep" (4), at most fastpwrup_dly us */
	CORE_MAC_CALL(c, budget = m->fastpwrup_dly);
	hw_udelay(40);
	while (shm_r(c, 0x40) == 4 && waited < budget) {
		hw_udelay(10);
		waited += 10;
	}
}

/* ps.c: a wake request changed (ps-keys.md 1.1) */
void core_w_set_wake_ctrl(struct bcm4360_core *c)
{
	set_wake_ctrl(c);
}

/* sub_039dd4, wlc_bsinit (1.4 step 23): the band's MAC settings */
static void bsinit(struct bcm4360_core *c)
{
	set_ratetable(c);
	ucode_mac_upd(c);
}

/* ---------------------------------------------------------------------- */
/* duty cycle words: wlc-hw.md 1.4 step 18, 2.3                            */
/* ---------------------------------------------------------------------- */

/* wlc_stf_duty_cycle_set(wlc, duty, ofdm, 1) */
static void duty_cycle_set(struct bcm4360_core *c, u32 duty, bool ofdm)
{
	u16 v;

	if (duty > 100)
		return;
	v = duty ? (u16)((100 - duty) * 16 / duty) : 0;
	shm_w(c, ofdm ? SHM_DUTY_OFDM : SHM_DUTY_CCK, v);
}

/* wlc_set_pwrthrottle_config (1.4 step 18) */
static void set_pwrthrottle_config(struct bcm4360_core *c)
{
	u8 *stf = STF(c);

	if (c->sihp.boardvendor != 0x106b || stf[0x50] != 0)
		return;
	duty_cycle_set(c, FIELD(stf, 0x52, u16) & 0xff, true);
	duty_cycle_set(c, FIELD(stf, 0x54, u16) & 0xff, false);
}

/* sub_1731cf, wlc_stf_pwrthrottle_upd / wlc_stf_chanspec_upd (2.3) */
static void stf_chanspec_upd(struct bcm4360_core *c)
{
	u8 *stf = STF(c);
	u8 t = stf[0x50];
	u16 cs = FIELD(c->wlc, WLC_CHANSPEC, u16);
	u32 duty = 100, w16;
	u32 bw = (cs >> 11) & 7;	/* chanspec bandwidth bits: 2 = 20, 3 = 40, 4 = 80 */

	if (!driver_up(c) || FIELD(FIELD(c->wlc, WLC_BAND, void *), 0x08, u16) != PHYTYPE_AC)
		return;
	if ((t & 2) && stf[0x78] <= 100)
		duty = stf[0x78];
	if (!(t & 1)) {
		w16 = t ? 0xffff : 0;
	} else {
		w16 = 0xffff;
		if (FIELD(c->pub, PUB_COREREV, u32) >= 40 && c->sihp.boardtype != 0xc00 &&
		    c->sihp.boardtype != 0xc01 && (cs & 0xc000) == 0xc000) {
			if (bw == 3 && FIELD(stf, 0x6e, u8))
				w16 = FIELD(stf, 0x70, u16);
			else if (bw == 4 && FIELD(stf, 0x72, u8))
				w16 = FIELD(stf, 0x74, u16);
		}
		if (duty > stf[0x51])
			duty = stf[0x51];
	}
	duty_cycle_set(c, duty, false);
	duty_cycle_set(c, duty, true);
	if (FIELD(c->pub, PUB_COREREV, u32) >= 40)
		shm_w(c, SHM_D6, (u16)w16);
}

/* ---------------------------------------------------------------------- */
/* channel: wlc-hw.md 2                                                    */
/* ---------------------------------------------------------------------- */

/* sub_03649a, wlc_set_phy_chanspec (2.2) */
static void set_phy_chanspec(struct bcm4360_core *c, u16 cs)
{
	void *wlc = c->wlc;
	u8 *stf = STF(c);
	bool quiet;
	void *p;

	FIELD(wlc, WLC_CHANSPEC, u16) = cs;
	/* wlc_tpc_get_local_constraint_qdbm: 127, no limit; wlc_channel_set_chanspec: */
	p = core_cp_limits(c, cs);	/* wlc_channel_reg_limits (channel-power.md 1.6) */
	if (p)
		bcm4360_ratepwr_apply_max(p, 127);
	quiet = core_cp_quiet(c, cs);
	CORE_MAC_CALL(c, bcm4360_bmac_set_chanspec(m, cs, quiet, p));
	if (p)
		core_cp_free(c, p);
	stf_chanspec_upd(c);
	if (stf[0x0d])
		bcm4360_stf_ss_algo_channel_get(wlc, (u16 *)(stf + 0x0e), cs);
	bcm4360_stf_ss_update(wlc, FIELD(wlc, WLC_BAND, void *));
	CORE_MAC_CALL(c, bcm4360_bmac_txbw_update(m));
}

/* wlc_set_chanspec (2.1), the MAC suspended by the caller */
static void set_chanspec(struct bcm4360_core *c, u16 cs)
{
	struct core_wlchw *w = &c->w;
	void *wlc = c->wlc;
	u32 unit = band_unit_of(cs);
	u16 old = FIELD(wlc, WLC_CHANSPEC, u16);
	u32 obw = (old >> 11) & 7, nbw = (cs >> 11) & 7;

	bcm4360_cca_stats_upd(c->cca, 1);
	if (cs == old)
		return;
	if (unit != ubs_unit(c) || w->band_switch_pending) {
		FIELD(wlc, WLC_BAND, void *) = FIELD(wlc, WLC_BANDSTATE(unit), void *);
		w->band_switch_pending = 0;
		if (driver_up(c))
			bsinit(c);
	}
	set_phy_chanspec(c, cs);
	if (obw != nbw) {
		u8 bw = nbw == 4 ? 0x50 : (nbw == 3 ? 0x28 : (nbw == 5 ? 0xa0 : 0x14));

		bcm4360_rateset_bw_mcs_filter(&w->hwrs[ubs_unit(c)], bw);
	}
	ucode_mac_upd(c);
	bcm4360_cca_stats_tsf_upd(c->cca);
	FIELD(wlc, WLC_TXC_GEN, u32) += 1;
	CORE_MAC_CALL(c, bcm4360_bmac_ifsctl_edcrs_set(m, false));
}

/* the iovar `chanspec` (2.1) */
int core_w_set_chanspec(struct bcm4360_core *c, u16 cs)
{
	struct core_wlchw *w = &c->w;

	if (!c->up) {			/* radio off or MPC: the next up uses it */
		c->chanspec = cs;
		return 0;
	}
	w->defchanspec = cs;
	if (cs == FIELD(c->wlc, WLC_CHANSPEC, u16))
		return 0;
	if (!w->associated)
		w->home_chanspec = cs;
	mac_suspend(c);
	set_chanspec(c, cs);
	mac_enable(c);
	c->chanspec = cs;
	core_ps_chanspec(c);				/* awake until the next beacon (ps-keys.md 1.8) */
	return 0;
}

/* ---------------------------------------------------------------------- */
/* attach: wlc-hw.md 1.1                                                   */
/* ---------------------------------------------------------------------- */

/* the stf state's defaults: wlc_attach and wlc_stf_attach (1.1 steps 8, 9, 11) */
static void stf_attach(struct bcm4360_core *c)
{
	u8 *stf = STF(c);
	struct bcm4360_phy *pi = c->phy;
	u32 board = c->sihp.boardtype, vendor = c->sihp.boardvendor;
	u8 tx, rx, i;
	s32 v;

	v = var_int(c, "txchain", 3);
	tx = (u8)(v ? v : 3);
	v = var_int(c, "rxchain", 3);
	rx = (u8)(v ? v : 3);
	stf[0x00] = tx;			/* hw_txchain */
	stf[0x01] = tx;			/* txchain */
	stf[0x02] = (u8)popcount8(tx);	/* txstreams */
	stf[0x03] = rx;			/* hw_rxchain */
	stf[0x04] = (u8)popcount8(rx);	/* op_rxstreams */
	stf[0x05] = rx;			/* rxchain */
	stf[0x06] = (u8)popcount8(rx);	/* rxstreams */
	stf[0x0d] = 1;			/* ss_algosel_auto */
	FIELD(stf, 0x0e, u16) = 0xffff;	/* ss_algo_channel: unknown */
	stf[0x13] = 1;			/* LDPC */
	stf[0x14] = 0xff;
	FIELD(stf, 0x2a, u16) = SHM_CORE_BASE_WORD;
	stf[0x59] = 1;
	FIELD(stf, 0x6c, u16) = 0x0e;
	stf[0x88] = 31;
	for (i = 0; i < 5; i++)
		stf[0x5f + i] = 0xff;
	for (i = 0; i < 6; i++)
		stf[0x16 + 2 * i] = (u8)(i < 3 ? 1 : i - 1);
	/* the duty-cycle variables of the SROM */
	stf[0x52] = (u8)var_int(c, "tx_duty_cycle_ofdm", 0);
	stf[0x54] = (u8)var_int(c, "tx_duty_cycle_cck", 0);
	stf[0x6e] = (u8)var_int(c, "tx_duty_cycle_ofdm_40_5g", 0);
	stf[0x70] = (u8)var_int(c, "tx_duty_cycle_thresh_40_5g", 0);
	stf[0x72] = (u8)var_int(c, "tx_duty_cycle_ofdm_80_5g", 0);
	stf[0x74] = (u8)var_int(c, "tx_duty_cycle_thresh_80_5g", 0);
	if (vendor == 0x106b && (c->sihp.chip == 0x4360 || board == 0xf5 || board == 0xd6 ||
				 board == 0xef || board == 0x10f || board == 0xf4 ||
				 board == 0xe4 || board == 0x10e)) {
		stf[0x78] = 50;
		stf[0x51] = 100;
		stf[0x4c] = 0xff;
	} else if (board == 0x56e || (vendor == 0x106b && (board == 0xe9 || board == 0xd1))) {
		stf[0x78] = 25;
		stf[0x51] = 100;
		stf[0x4c] = 0xff;
		stf[0x38] = 1;
	} else if (board == 0x5da) {
		stf[0x78] = 100;
		stf[0x51] = 100;
		stf[0x4c] = 0xff;
	}
	/* the band's single-stream mode (cca-stf.md, band +0x58): 2.4 GHz 0, 5 GHz 1 */
	FIELD(c->uband[0], 0x58, u8) = 0;
	FIELD(c->uband[1], 0x58, u8) = 1;
	stf[0x3c] = (u8)(var_int(c, "temps_period", 10) >= 1 && var_int(c, "temps_period", 10) <= 14 ?
			 var_int(c, "temps_period", 10) : 10);
	(void)pi;
}

/* wlc_attach after wlc_bmac_attach (1.1 steps 4-13) */
void core_w_attach(struct bcm4360_core *c)
{
	struct core_wlchw *w = &c->w;
	void *wlc = c->wlc, *pub = c->pub, *hw = CORE_WLC_HW(c);
	struct bcm4360_phy *pi = c->phy;
	u8 *stf = STF(c);
	u32 i;

	/* defaults of wlc_attach / wlc_info_init */
	w->wme = 1;
	w->shortslot_ovr = 0xff;
	w->lifetime = 800;
	w->defchanspec = 0x1001;
	w->home_chanspec = 0x1001;
	FIELD(pub, 0x54, u8) = 1;
	FIELD(pub, 0x5e, u8) = 1;
	FIELD(pub, 0x5f, u8) = 1;
	FIELD(pub, 0x68, u8) = 3;
	FIELD(pub, 0xd4, u32) = KEY_ENTRIES;
	FIELD(pub, 0xdc, u32) = SHM_SECINDEX;
	FIELD(pub, 0xe0, u32) = SECINDEX_WORDS;
	FIELD(pub, PUB_SHM_CORE_BASE, u16) = SHM_CORE_BASE_WORD;
	FIELD(pub, PUB_SHM_CORE_BASE + 2, u16) = 0x360;
	FIELD(pub, 0xe8, u8) = 1;
	FIELD(wlc, 0x52a, u16) = 7;
	FIELD(wlc, 0x52c, u16) = 6;
	for (i = 0; i < 4; i++)
		w->retry[i] = 0x0600;
	FIELD(wlc, 0x71a, u8) = 1;
	FIELD(wlc, WLC_FLAG_718, u8) = 1;	/* bmac-init B1 step 27: the first init skips scratch 9 */

	/* step 5: the capabilities word (wlc_bmac_state_get) */
	w->machwcap = FIELD(hw, 0xa4, u32);
	/* step 7: the retry limits (E7) */
	CORE_MAC_CALL(c, bcm4360_bmac_retrylimit_upd(m, 7, 6));

	stf_attach(c);
	/* step 9: chains */
	bcm4360_stf_spatial_policy_set(wlc, 0);
	if (pi)
		bcm4360_phy_stf_chain_init(pi, stf[0], stf[3]);
	/* step 10: the permanent address is the interface address */
	hw_memcpy((u8 *)pub + PUB_ETHERADDR, (u8 *)hw + 0x178, 6);
	if (pi)
		stf[0x48] = bcm4360_phy_txpower_ipa_ison(pi);

	/* step 11: the module attaches */
	FIELD(hw, 0x1a1, u8) = 0;			/* wlc_bmac_antsel_type_set */
	if (pi)
		bcm4360_phy_antsel_type_set(pi, 0);
	mhf(c, 2, 0x0040, 0x0040, 3);			/* A-MPDU transmit */
	bcm4360_stf_ss_update(wlc, c->uband[0]);	/* wlc_stf_attach */
	bcm4360_stf_ss_update(wlc, c->uband[1]);
	c->cca = bcm4360_cca_attach(wlc, c->osh, 0, FIELD(pub, PUB_COREREV, u32));
	FIELD(wlc, WLC_CCA, void *) = c->cca;
	/* offloads (wlc_ol_attach): the power-management capability, then the TCM size */
	i = core_si_find_capability(c, 1);
	if (i)
		(void)hw_pcicfg_read(c->hw, i, 4);
	(void)bcm4360_si_tcm_size(&c->si);		/* no offload engine on this card */
	/* the offload state exists, empty (its semaphore byte +0x8c is 0): bmac-init C2, C15 */
	w->ol = hw_zalloc(0x100);
	FIELD(wlc, WLC_OFFLOAD, void *) = w->ol;
	core_cp_attach(c);				/* the channel manager: country of the SROM */

	/* step 12: the radio switch */
	CORE_MAC_CALL(c, if (bcm4360_bmac_radio_read_hwdisabled(m)) w->radio_dis |= 2;
		      else w->radio_dis &= ~2);
	FIELD(hw, 0x1a4, u32) = 0;			/* wlc_bmac_antsel_set */

	core_led_attach(c);				/* G2 */
	core_radio_attach(c);				/* G4 */
	c->ctl.hw_prev = (w->radio_dis & RADIO_DIS_HW) != 0;
	c->ctl.hw_known = 1;

	/* the rate sets of both bands (1.4 step 14), and the quiet channels (channel-power) */
	for (i = 0; i < 2; i++)
		rates_band_init(c, i);
}

/* ---------------------------------------------------------------------- */
/* up and init: wlc-hw.md 1.2 - 1.5                                        */
/* ---------------------------------------------------------------------- */

/* sub_179ae1, wlc_txbf_init (1.4 step 11) */
static void txbf_init(struct bcm4360_core *c)
{
	static const u16 tbl[16] = {
		0x3475, 0x3475, 0x3475, 0x217c, 0x237b, 0x217c, 0x217c, 0x217c,
		0x3276, 0x3276, 0x3475, 0x187e, 0x217c, 0x167e, 0x1d7d, 0x1f7c,
	};
	u8 *stf = STF(c);
	u32 b, k = popcount8(stf[1]), r = popcount8(stf[5]), i;

	b = 2u * shm_r(c, SHM_TXBF_BASE);
	shm_w(c, b + 0xe0, 0x186a);
	if (k == 2) {
		shm_w(c, b + 0x104, (u16)((stf[1] << 6) | 0x8003));
		shm_w(c, b + 0xe8, (u16)((stf[1] << 6) | 0x8002));
	}
	shm_w(c, b + 0xe4, (u16)(r - 1));
	shm_w(c, b + 0x6e0, shm_r(c, b + 0x6e0) & ~0x100);
	for (i = 0; i < 16; i++)
		shm_w(c, b + 0x120 + 2 * i, tbl[i]);
	shm_w(c, b + 0xf6, 0x41c2);
	shm_w(c, b + 0xf8, 0);
	shm_w(c, b + 0xfa, 0x17);
	shm_w(c, b + 0xfc, 0x24b);
	shm_w(c, b + 0xfe, 0x97);
	shm_w(c, b + 0x100, 0x500);
	shm_w(c, b + 0x102, 0);
}

/* wlc_key_hw_init_all (1.4 step 9) */
static void key_hw_init_all(struct bcm4360_core *c)
{
	static const u8 zero[6];
	u32 i;

	CORE_MAC_CALL(c, bcm4360_bmac_set_shm(m, c->w.key_base, 0, 16 * KEY_ENTRIES));
	for (i = 0; i < SECINDEX_WORDS; i++)
		shm_w(c, SHM_SECINDEX + 2 * i, 0);
	for (i = 0; i < KEY_ENTRIES - 4; i++)
		CORE_MAC_CALL(c, bcm4360_bmac_set_rcmta(m, i, zero));
	mhf(c, 0, 0x4000, 0, 3);
	core_keys_reinit(c);				/* every configured key again (ps-keys.md 2.3) */
}

/* wlc_set_mac (1.4 step 13): AMT[63], TPL(0x48) */
static void set_mac(struct bcm4360_core *c)
{
	struct core_wlchw *w = &c->w;
	u8 buf[8];

	CORE_MAC_CALL(c, bcm4360_bmac_write_amt(m, AMT_OWN, (u8 *)c->pub + PUB_ETHERADDR,
						AMT_ATTR_OWN));
	(void)w;
	hw_memcpy(buf, (u8 *)c->pub + PUB_ETHERADDR, 6);
	buf[6] = buf[7] = 0;
	CORE_MAC_CALL(c, bcm4360_bmac_write_template_ram(m, TPL_OWN, 8, buf));
}

/* wlc_set_bssid (1.4 step 13): AMT[62], TPL(0x30..0x43) */
static void set_bssid(struct bcm4360_core *c)
{
	struct core_wlchw *w = &c->w;
	static const u8 zero[6];
	u8 buf[20];
	u16 attr = 0;
	u32 i;

	for (i = 0; i < 6; i++)
		if (w->bssid[i])
			attr = AMT_ATTR_BSS_INFRA;
	CORE_MAC_CALL(c, bcm4360_bmac_write_amt(m, AMT_BSSID, i ? w->bssid : zero, attr));
	hw_memcpy(buf, w->bssid, 6);
	hw_memcpy(buf + 6, (u8 *)c->pub + PUB_ETHERADDR, 6);
	hw_memcpy(buf + 12, w->bssid, 6);
	buf[18] = buf[19] = 0;
	CORE_MAC_CALL(c, bcm4360_bmac_write_template_ram(m, TPL_BSS, 20, buf));
}

/* wlc_init up to the MAC enable and after it (1.4) */
static void wlc_init(struct bcm4360_core *c, u16 cs)
{
	struct core_wlchw *w = &c->w;
	void *wlc = c->wlc, *pub = c->pub;
	u8 *stf = STF(c);
	u32 i, v;

	w->defchanspec = cs;
	/* step 3 */
	CORE_MAC_CALL(c, bcm4360_bmac_init(m, cs, false));
	/* step 4: the rate directories */
	for (i = 0; i < 16; i++)
		w->rdir_ofdm[i] = shm_r(c, SHM_RDIR_OFDM + 2 * i);
	for (i = 0; i < 16; i++)
		w->rdir_cck[i] = shm_r(c, SHM_RDIR_CCK + 2 * i);
	/* step 6 */
	w->key_base = 2 * shm_r(c, SHM_KEY_BASE);
	/* step 7: wlc_bcn_li_upd */
	shm_w(c, SHM_BCN_LI, w->bcn_li);
	/* step 8 */
	if (!w->dtimnoslp)
		w->dtimnoslp = (shm_r(c, SHM_4C) >> 7) & 1;
	/* steps 9, 11 */
	key_hw_init_all(c);
	txbf_init(c);
	/* step 13 */
	set_mac(c);
	set_bssid(c);
	/* steps 14, 15 */
	w->home_chanspec = cs;
	/* step 16 */
	set_phy_chanspec(c, cs);
	/* step 17 */
	shm_w(c, SHM_74, 0);
	shm_w(c, SHM_82, 10000);
	if (c->phy)
		bcm4360_phy_tkip_rifs_war(c->phy, 0);
	/* step 18 */
	set_pwrthrottle_config(c);
	/* step 21: wlc_ampdu_shm_upd */
	shm_w(c, SHM_BA, 0xffff);
	shm_w(c, SHM_3C, 10);
	/* step 22 */
	FIELD(stf, 0x2a, u16) = FIELD(pub, PUB_SHM_CORE_BASE, u16);
	bcm4360_stf_txcore_shmem_write(wlc, true);
	/* step 23 */
	bsinit(c);
	/* step 24: WME */
	if (w->wme) {
		v = d11_read16(c->hw, D11_WME_REG);
		d11_write16(c->hw, D11_WME_REG, (u16)(v | 4));
	}
	/* step 26 */
	(void)shm_r(c, SHM_UCODE_MAJOR);	/* the version is read again at every init */
	(void)shm_r(c, SHM_UCODE_MINOR);
	/* step 28 */
	mac_enable(c);
	/* step 29 */
	set_wake_ctrl(c);
	/* step 30: wlc_rfaware_lifetime_set: nothing while down */
	/* step 32 */
	d11_write32(c->hw, D11_REG_3DC, 10000000);
	/* step 33 */
	for (i = 0; i < 4; i++) {
		u16 s = shm_r(c, SHM_RETRY + 2 * i), cur = w->retry[i], r = 0;
		u32 f;

		for (f = 0; f < 16; f += 4) {
			u16 fld = (cur >> f) & 0xf;

			r |= (fld ? fld : ((s >> f) & 0xf)) << f;
		}
		w->retry[i] = r;
	}
	/* step 34 */
	bcm4360_cca_stats_upd(c->cca, 0);
}

/* wlc_up (1.2 - 1.5) */
int core_w_up(struct bcm4360_core *c, u16 chanspec)
{
	struct core_wlchw *w = &c->w;
	void *wlc = c->wlc, *pub = c->pub;
	s32 r = 0;
	bool gone = false;
	u32 m0, i;

	CORE_MAC_CALL(c, gone = bcm4360_bmac_deviceremoved(m));
	if (gone)
		return -9;
	{	/* the frame path starts clean, before any interrupt is enabled */
		u32 f = c->fr.rx_filter;

		hw_memset(&c->fr, 0, sizeof(c->fr));
		c->fr.rx_filter = f;
	}
	c->chanspec = chanspec;
	if (!FIELD(pub, PUB_HW_UP, u8)) {
		w->led_mask = core_led_mask(c);
		bcm4360_bmac_led_hw_mask_init(CORE_WLC_HW(c), w->led_mask);
		CORE_MAC_CALL(c, bcm4360_bmac_hw_up(m));
		FIELD(pub, PUB_HW_UP, u8) = 1;
	}
	/* wlc_bmac_4331_epa_init: nothing on the 4360 */
	mhf(c, 4, 0x0080, STF(c)[0x59] ? 0x0080 : 0, 3);
	if (!w->radio_dis) {
		CORE_MAC_CALL(c, r = bcm4360_bmac_up_prep(m));
		if (r == -9)
			w->radio_dis |= 2;
	}
	if (w->radio_dis)
		return 0;
	if (r)
		return r;
	core_radio_monitor_stop(c);			/* G4: wlc_up, once the radio is allowed */
	/* wlc_bmac_set_ctrl_ePA: nothing on the 4360 */
	if (FIELD(CORE_WLC_HW(c), 0x1bc, u8) == 0xff) {
		CORE_MAC_CALL(c, bcm4360_bmac_set_ctrl_bt_shd0(m, true));
	} else {
		CORE_MAC_CALL(c, bcm4360_bmac_set_btswitch(m, FIELD(CORE_WLC_HW(c), 0x1bc, s8)));
	}
	FIELD(wlc, WLC_CLK, u8) = 1;
	w->clk_up = 1;
	mhf(c, 0, 0x0100, w->wme ? 0x0100 : 0, 3);
	if (FIELD(wlc, 0x60, u8))
		mhf(c, 1, 0x0008, 0x0008, 3);
	CORE_MAC_CALL(c, bcm4360_bmac_seci_upd(m));
	/* wl_init: wl_intrsoff, wlc_reset, wl_intrsrestore, wlc_init */
	m0 = core_intrsoff(c);
	CORE_MAC_CALL(c, bcm4360_bmac_reset(m));
	core_intrsrestore(c, m0);
	if (!m0)	/* observed: this restore to 0 is read back */
		(void)d11_read32(c->hw, D11_MACINTMASK);
	wlc_init(c, chanspec);
	FIELD(pub, PUB_UP, u8) = 1;

	/* 1.5 */
	enable_probe_req(c, 3, w->probe_req);
	mhf(c, 1, 0x0001, w->mhf2_bit0 ? 1 : 0, 3);
	/* wlc_rfaware_lifetime_set with the driver up */
	mac_suspend(c);
	shm_w(c, SHM_LIFETIME, w->lifetime);
	mhf(c, 3, 0x0020, w->lifetime ? 0x0020 : 0, 3);
	mac_enable(c);
	CORE_MAC_CALL(c, r = bcm4360_bmac_up_finish(m));
	for (i = 0; i < 4; i++)
		shm_w(c, SHM_RETRY + 2 * i, w->retry[i]);
	c->up = true;
	if (c->wd_timer)
		bcm4360_plat_timer_add(c->plat, c->wd_timer, TICK_MS, true);
	/* sub_0396ff: LDPC */
	if (c->phy)
		bcm4360_phy_ldpc_override_set(c->phy, STF(c)[0x13] != 0);
	stf_chanspec_upd(c);
	core_led_up(c);
	/* step 21: D11(0xa00) bit 0 */
	if (FIELD(wlc, 0x71a, u8)) {
		u32 v = d11_read16(c->hw, D11_REG_A00);

		if (v & 1)
			d11_write16(c->hw, D11_REG_A00, (u16)(v & 0xfffe));
	}
	return r;
}

/* ---------------------------------------------------------------------- */
/* periodic work: wlc-hw.md 3                                              */
/* ---------------------------------------------------------------------- */

/* wlc_bmac_btc_period_get, wlc_bmac_btc_params_get (3 step 7) */
void core_w_btc_period_get(struct bcm4360_core *c, u16 *period, u8 *state)
{
	struct core_wlchw *w = &c->w;
	u32 p = 0;
	u16 a, b = 0;

	CORE_MAC_CALL(c, p = m->btc_shm_base);
	*period = 0;
	*state = w->bt_active;
	if (!p)
		return;
	a = shm_r(c, p + 8);
	if (a) {
		b = shm_r(c, p + 0x90);
		if (b > 4)
			*period = a;
	}
}

/* the coexistence reads of the 2.4 GHz watchdog (3 step 7) */
static void watchdog_btc(struct bcm4360_core *c)
{
	struct core_wlchw *w = &c->w;
	u16 period;
	u8 state;
	u32 p = 0, mc;

	if (FIELD(c->pub, PUB_COREREV, u32) <= 14)
		return;
	core_w_btc_period_get(c, &period, &state);
	CORE_MAC_CALL(c, p = m->btc_shm_base);
	mc = d11_read32(c->hw, D11_MACCONTROL);
	if (mc & MCTL_PSM_RUN) {
		u16 v = d11_read16(c->hw, D11_BT_STATE);

		if (v != 0xffff) {
			if (w->bt_count < 5)
				w->bt_count++;
			if (w->bt_count >= 5)
				w->bt_active = 1;
		} else {
			w->bt_count = 0;
			w->bt_active = 0;
		}
	}
	if (p)
		(void)shm_r(c, p + 0x4e);	/* wlc_bmac_btc_params_get(hw, 0x27) */
	core_btc_ps_protection(c);		/* G3, on the 2.4 GHz band */
}

/* wlc_bmac_fifoerrors (R7): 1 if a FIFO reports an error */
static bool bmac_fifoerrors(struct bcm4360_core *c)
{
	u32 i;

	for (i = 0; i < CORE_NFIFO; i++) {
		u32 v = d11_read32(c->hw, D11_FIFO_INTSTATUS(i));

		if (v & 0xdc00)
			return true;
	}
	return false;
}

/* wlc_bmac_watchdog (R7) */
static void bmac_watchdog(struct bcm4360_core *c)
{
	void *hw = CORE_WLC_HW(c);
	struct bcm4360_dma *di;

	if (bmac_fifoerrors(c)) {
		bcm4360_plat_fatal(c->plat);
		return;
	}
	di = FIELD(hw, HW_DI(0), struct bcm4360_dma *);
	if (di)
		bcm4360_dma_rxfill(di);
	if (c->phy)
		bcm4360_phy_watchdog(c->phy);
}

/* wlc_statsupd (3 step 9): the 64 counter words of the microcode */
static void statsupd(struct bcm4360_core *c)
{
	struct core_wlchw *w = &c->w;
	u16 buf[64];
	u32 i;

	if (!driver_up(c) || !FIELD(c->wlc, WLC_CLK, u8))
		return;
	CORE_MAC_CALL(c, bcm4360_bmac_copyfrom_objmem(m, SHM_STATS, buf, 0x80, SEL_SHM));
	for (i = 0; i < 64; i++)
		w->stats_prev[i] = buf[i];
	w->stats_valid = 1;
}

/* wlc_stf_tempsense_upd (3 step 16) */
static void stf_tempsense_upd(struct bcm4360_core *c)
{
	u8 *stf = STF(c);
	u8 a, old, nw;

	if (!c->phy || stf[0x38] || FIELD(FIELD(c->wlc, WLC_BAND, void *), 0x08, u16) != PHYTYPE_AC)
		return;
	if (!stf[0x3c] || c->w.ticks % stf[0x3c])
		return;
	a = bcm4360_phy_stf_chain_active_get(c->phy) & 0xf;
	old = stf[0x50];
	nw = popcount8(a) < popcount8(stf[0]) ? (old | 2) : (old & ~2);
	if (nw == old)
		return;
	stf[0x50] = nw;
	if (driver_up(c)) {
		stf_chanspec_upd(c);
		core_w_stf_txchain_set(c, (nw & 2) ? a : stf[0]);
	}
}

/* wlc_stf_txchain_set: the transmit chains (cca-stf.md, acphy-api wlc_phy_stf_chain_set) */
void core_w_stf_txchain_set(struct bcm4360_core *c, u8 mask)
{
	(void)core_stf_txchain_set(c, mask, false, 4);	/* requester 4: temperature reduction */
}

/* wlc_watchdog (sub_0450f0), once a second while up */
void core_w_watchdog(struct bcm4360_core *c)
{
	struct core_wlchw *w = &c->w;
	bool gone = false;

	if (!driver_up(c))
		return;
	CORE_MAC_CALL(c, gone = bcm4360_bmac_deviceremoved(m));
	if (gone) {
		bcm4360_plat_fatal(c->plat);
		return;
	}
	w->ticks++;
	/* sub_030b4b: the radio switch */
	(void)core_radio_poll_hw(c);
	if (w->radio_dis) {			/* MPC or the hardware switch: down (G4) */
		core_radio_upd(c);
		return;
	}
	if (c->ctl.joining && ++c->ctl.join_ticks > JOIN_TIMEOUT_TICKS)
		bcm4360_core_join_end(c, false);	/* G7: never hold the PHY for ever */
	if (ubs_unit(c) == 0)
		watchdog_btc(c);
	bmac_watchdog(c);
	if (!(w->ticks % 30))
		statsupd(c);
	core_led_event(c);
	bcm4360_cca_watchdog(c->cca);	/* the channel statistics module */
	stf_tempsense_upd(c);
	core_ps_watchdog(c);				/* ps-keys.md 1.5, 1.8 */
}

/* ---------------------------------------------------------------------- */
/* down and detach: wlc-hw.md 4                                            */
/* ---------------------------------------------------------------------- */

static void clear_hw_association(struct bcm4360_core *c);

/* wlc_down (4.1) */
void core_w_down(struct bcm4360_core *c)
{
	struct core_wlchw *w = &c->w;
	u8 peer, tid;

	if (w->radio_dis)
		core_radio_monitor_start(c);		/* G4: the switch is read while down */
	c->ctl.joining = 0;				/* G7 */
	c->ctl.cal_valid = 0;
	CORE_MAC_CALL(c, bcm4360_bmac_down_prep(m));
	if (w->associated)
		clear_hw_association(c);
	set_wake_ctrl(c);				/* the scan abort */
	if (c->phy)
		bcm4360_phy_hold_upd(c->phy, 2, false);	/* wlc_scan_down */
	bcm4360_cca_down(c->cca);
	w->scanning = 0;
	w->bcn_promisc_scan = 0;
	core_ps_down(c);
	core_keys_forget(c);				/* the platform sets its keys again at the next start */
	for (peer = 0; peer < BCM4360_AMPDU_NPEERS; peer++)
		for (tid = 0; tid < 16; tid++)
			bcm4360_core_ampdu_tx_stop(c, peer, tid);
	if (c->wd_timer)
		bcm4360_plat_timer_del(c->plat, c->wd_timer);
	FIELD(c->pub, PUB_UP, u8) = 0;
	c->up = false;
	if (c->phy)
		bcm4360_phy_mute_upd(c->phy, false, 0xffffffff);
	core_led_down(c);
	CORE_MAC_CALL(c, bcm4360_bmac_down_finish(m));
	FIELD(c->wlc, WLC_CLK, u8) = 0;
	w->clk_up = 0;
}

/* wlc_detach, the part before wlc_bmac_detach (4.2 step 1) */
void core_w_detach(struct bcm4360_core *c)
{
	core_radio_detach(c);
	core_led_detach(c);
	if (c->w.ol) {
		FIELD(c->wlc, WLC_OFFLOAD, void *) = NULL;
		hw_free(c->w.ol, 0x100);
		c->w.ol = NULL;
	}
	if (FIELD(c->pub, PUB_HW_UP, u8) || c->w.led_mask)
		bcm4360_bmac_led_hw_deinit(CORE_WLC_HW(c), c->w.led_mask);
}

/* ---------------------------------------------------------------------- */
/* the BSS: wlc-hw.md 5                                                    */
/* ---------------------------------------------------------------------- */

/* wlc_pretbtt_set (5.2 step 15): infrastructure on the AC-PHY */
static void pretbtt_set(struct bcm4360_core *c)
{
	d11_write16(c->hw, D11_PRETBTT, 1024);
	shm_w(c, SHM_PRETBTT, 1024);
}

/* wlc_tsf_adopt_bcn (5.3) */
static void tsf_adopt(struct bcm4360_core *c, u64 tsf, u16 bi)
{
	u32 b = bi ? bi : 1024, p = b * 1024, lo = (u32)tsf, hi = (u32)(tsf >> 32), o, n;

	/* o = T mod P: from T in units of 1024 us modulo B, times 1024, plus the low 10 bits */
	o = (u32)((((tsf >> 10) % b) << 10) | (lo & 0x3ff));
	n = (lo - o) + p * ((o + 0x18fff + p) / p);
	mctrl(c, MCTL_TBTT_HOLD, MCTL_TBTT_HOLD);
	d11_write32(c->hw, D11_TSF_LO, lo);
	d11_write32(c->hw, D11_TSF_HI, hi);
	d11_write32(c->hw, D11_TSF_CFPSTART, (u32)b << 16);
	d11_write32(c->hw, D11_TSF_CFPREP, n);
	mctrl(c, MCTL_TBTT_HOLD, 0);
}

/* wlc_edcf_setparams (5.4): one access category's words in the shared memory */
static void edcf_apply_one(struct bcm4360_core *c, u8 fifo, const struct core_wlchw_edca *e,
			   bool suspend)
{
	static const u8 aci_of_fifo[4] = { 1, 0, 2, 3 };
	u16 blk[16];
	u32 aifs, base = SHM_EDCA_BASE + 0x20 * fifo, i;

	if (suspend)
		mac_suspend(c);
	hw_memset(blk, 0, sizeof(blk));
	aifs = e->aifs;
	if (aci_of_fifo[fifo & 3] == 2 && e->txop == 0 && aifs != 15)
		aifs++;
	blk[0] = (u16)(e->txop << 5);
	if (aifs >= 1 && aifs <= 15) {
		blk[1] = e->cwmin;
		blk[2] = e->cwmax;
		blk[3] = e->cwmin;
		blk[4] = (u16)aifs;
		blk[5] = d11_read16(c->hw, D11_RANDOM) & e->cwmin;
		blk[6] = (u16)(blk[5] + aifs);
		blk[7] = shm_r(c, base + 0x0e) | 0x100;
		for (i = 0; i < 16; i++)
			shm_w(c, base + 2 * i, blk[i]);
	}
	if (suspend)
		mac_enable(c);
}

/* wlc_edcf_acp_apply (5.4) for the access categories that were set */
static void edcf_apply_all(struct bcm4360_core *c, bool suspend)
{
	u32 f;

	for (f = 0; f < 4; f++)
		if (c->w.edca[f].valid)
			edcf_apply_one(c, (u8)f, &c->w.edca[f], suspend);
}

/* wlc_clear_hw_association (5.7) */
static void clear_hw_association(struct bcm4360_core *c)
{
	struct core_wlchw *w = &c->w;
	static const u8 zero[6];

	w->associated = 0;
	hw_memset(w->bssid, 0, 6);
	core_ps_bss(c, false, true);			/* wlc_reset_pmstate (ps-keys.md 1.8) */
	if (!c->up)
		return;
	if (c->phy)
		bcm4360_phy_hold_upd(c->phy, 0x20, true);	/* wlc_sta_assoc_upd(.., 0) */
	CORE_MAC_CALL(c, bcm4360_bmac_write_amt(m, AMT_BSSID, zero, 0));
	mac_suspend(c);
	CORE_MAC_CALL(c, bcm4360_bmac_write_amt(m, AMT_BSSID, zero, 0));
	d11_write32(c->hw, D11_TSF_CFPSTART, 0x80000000);
	CORE_MAC_CALL(c, bcm4360_bmac_set_cwmin(m, 15));
	CORE_MAC_CALL(c, bcm4360_bmac_set_cwmax(m, 1023));
	if (ubs_unit(c) == 0)
		CORE_MAC_CALL(c, bcm4360_bmac_set_shortslot(m, w->shortslot));
	mac_enable(c);
	CORE_MAC_CALL(c, bcm4360_bmac_ifsctl_edcrs_set(m, true));
	{
		struct bcm4360_rateset none;

		hw_memset(&none, 0, sizeof(none));
		rate_lookup_init(c, &none);
	}
	set_ratetable(c);
}

/* G7: the BSS's rate set as the response-rate map wants it: the basic rates flagged;
 * with no basic rates known, the mandatory ones of the band (1, 2, 5.5, 11; 6, 12, 24) */
static void join_rates(struct bcm4360_core *c, u16 basic, struct bcm4360_rateset *rs)
{
	const struct bcm4360_rateset *hw = &c->w.hwrs[ubs_unit(c)];
	u32 i;

	hw_memset(rs, 0, sizeof(*rs));
	for (i = 0; i < hw->count && i < 16; i++) {
		u8 r = hw->rates[i] & 0x7f;
		bool basic_rate = basic ? (basic & (1u << i)) != 0 :
				  (r == 2 || r == 4 || r == 11 || r == 22 || r == 12 ||
				   r == 24 || r == 48);

		rs->rates[rs->count++] = basic_rate ? r | 0x80 : r;
	}
}

/*
 * The start of a join (docs/re/spec/coverage.md G7, wlc_join_BSS), called when mac80211
 * begins to authenticate (mgd_prepare_tx): the channel, the rate map, a full calibration
 * (reason 4) when channel or BSS are new, AMT[62] = BSSID with attribute 0x8002, the PHY
 * held (hold 0x1) and WAKE kept until bcm4360_core_join_end() or the association
 * (bcm4360_core_set_bss). Calling it again for the association request is harmless.
 */
int bcm4360_core_join_start(struct bcm4360_core *c, u16 chanspec, const u8 *bssid,
			    u16 basic_rates, bool reassoc)
{
	struct core_wlchw *w = &c->w;
	struct core_ctl *t = &c->ctl;
	struct bcm4360_rateset rs;
	bool fresh;

	if (!c->up)
		return -19;
	/* step 2: the channel */
	if (chanspec != FIELD(c->wlc, WLC_CHANSPEC, u16)) {
		int r = core_w_set_chanspec(c, chanspec);

		if (r)
			return r;
	} else if (core_cp_quiet(c, chanspec) && c->phy) {
		bcm4360_phy_mute_upd(c->phy, false, 0xffffffff);	/* wlc_mute(wlc, 0, 0) */
	}
	/* step 3 */
	join_rates(c, basic_rates, &rs);
	rate_lookup_init(c, &rs);
	/* step 4 */
	fresh = !t->cal_valid || t->join_cs != chanspec || hw_memcmp(t->join_bssid, bssid, 6);
	if (fresh && !reassoc && c->phy)
		bcm4360_phy_cal_perical(c->phy, 4);
	t->cal_valid = 1;
	t->join_cs = chanspec;
	hw_memcpy(t->join_bssid, bssid, 6);
	/* step 6: the AP's frames pass the BSSID filter */
	if (!w->associated)
		CORE_MAC_CALL(c, bcm4360_bmac_write_amt(m, AMT_BSSID, bssid, AMT_ATTR_JOIN));
	core_keys_hostflag(c);				/* wlc_bsscfg_up */
	/* wlc_assoc_change_state: leaving the idle states holds the PHY */
	if (!t->joining) {
		t->joining = 1;
		if (c->phy)
			bcm4360_phy_hold_upd(c->phy, 1, true);
	}
	t->join_ticks = 0;
	set_wake_ctrl(c);
	core_led_event(c);
	return 0;
}

/* The association state machine is idle again (wlc_assoc_change_state): release the PHY and
 * the wake bit; a join that failed also frees AMT[62] */
void bcm4360_core_join_end(struct bcm4360_core *c, bool success)
{
	struct core_ctl *t = &c->ctl;
	static const u8 zero[6];

	if (!t->joining)
		return;
	t->joining = 0;
	if (c->phy)
		bcm4360_phy_hold_upd(c->phy, 1, false);
	if (!success && !c->w.associated)
		CORE_MAC_CALL(c, bcm4360_bmac_write_amt(m, AMT_BSSID, zero, 0));
	set_wake_ctrl(c);
	core_led_event(c);
}

/* bcm4360_core_set_bss: wlc-hw.md 5.8 */
void bcm4360_core_set_bss(struct bcm4360_core *c, const struct bcm4360_bss *bss)
{
	struct core_wlchw *w = &c->w;
	struct bcm4360_rateset rs;
	u32 i, unit;

	if (!c->up) {
		w->associated = bss->associated;
		hw_memcpy(w->bssid, bss->bssid, 6);
		return;
	}
	if (!bss->associated) {
		if (w->associated)
			clear_hw_association(c);
		if (c->ctl.joining)
			bcm4360_core_join_end(c, false);	/* the join failed or was given up */
		core_btc_ps_protection(c);
		core_led_event(c);
		return;
	}
	if (!w->associated) {
		/* join: the MAC suspended (5.1 step 2.2, wlc_BSSinit) */
		hw_memcpy(w->bssid, bss->bssid, 6);
		w->associated = 1;
		unit = ubs_unit(c);
		hw_memset(&rs, 0, sizeof(rs));
		for (i = 0; i < w->hwrs[unit].count && i < 16; i++)
			if (bss->basic_rates & (1u << i))
				rs.rates[rs.count++] = w->hwrs[unit].rates[i] | 0x80;
		mac_suspend(c);
		rate_lookup_init(c, &rs);
		set_bssid(c);
		set_mac(c);
		set_ratetable(c);
		if (unit == 0) {
			w->shortslot = bss->short_slot;
			CORE_MAC_CALL(c, bcm4360_bmac_set_shortslot(m, bss->short_slot));
		}
		pretbtt_set(c);
		mac_enable(c);
		if (c->phy)
			bcm4360_phy_hold_upd(c->phy, 0x20, false);	/* wlc_sta_assoc_upd */
		edcf_apply_all(c, true);
		w->last_tsf = 0;
		core_ps_bss(c, true, false);		/* awake until the first beacon (ps-keys.md 1.8) */
		bcm4360_core_join_end(c, true);		/* G7: the association state machine is idle */
		core_btc_ps_protection(c);		/* G3: wlc_sta_assoc_upd */
		core_led_event(c);			/* G2: a join completed */
	} else if (unit = ubs_unit(c), unit == 0 && bss->short_slot != w->shortslot) {
		w->shortslot = bss->short_slot;
		CORE_MAC_CALL(c, bcm4360_bmac_set_shortslot(m, bss->short_slot));
	}
	/* the first beacon (5.1 step 3, 5.3) */
	if (bss->tsf && bss->tsf != w->last_tsf) {
		w->last_tsf = bss->tsf;
		w->beacon_int = bss->beacon_int;
		tsf_adopt(c, bss->tsf, bss->beacon_int);
	}
	if (bss->dtim_period && bss->dtim_period != w->dtim_period) {
		w->dtim_period = bss->dtim_period;
		shm_w(c, SHM_DTIM, bss->dtim_period);
	}
	core_ps_bss(c, false, false);		/* the DTIM period may allow power save now */
}

/* bcm4360_core_set_macaddr (1.4 step 13) */
void bcm4360_core_set_macaddr(struct bcm4360_core *c, const u8 *addr)
{
	hw_memcpy((u8 *)c->pub + PUB_ETHERADDR, addr, 6);
	if (c->up) {
		set_mac(c);
		set_bssid(c);
	}
}

/* bcm4360_core_set_edca (5.4): the contention parameters of a transmit FIFO */
void bcm4360_core_set_edca(struct bcm4360_core *c, u8 fifo, u16 cwmin, u16 cwmax, u8 aifs,
			   u16 txop)
{
	struct core_wlchw_edca *e;

	if (fifo >= 4)
		return;
	e = &c->w.edca[fifo];
	e->valid = 1;
	e->aifs = aifs;
	e->cwmin = cwmin;
	e->cwmax = cwmax;
	e->txop = txop;
	if (c->up && c->w.associated)
		edcf_apply_one(c, fifo, e, true);
}

/* ---------------------------------------------------------------------- */
/* filters and scanning: wlc-hw.md 6                                       */
/* ---------------------------------------------------------------------- */

/* bcm4360_core_set_filter (6.3) */
void bcm4360_core_set_filter(struct bcm4360_core *c, u32 filter)
{
	c->w.filter = filter;
	c->fr.rx_filter = filter;	/* the frame path's copy */
	if (!c->up)
		return;
	mac_bcn_promisc(c);
	mac_promisc(c);
	set_wake_ctrl(c);
}

/* bcm4360_core_set_scanning (6.2, 6.3) */
void bcm4360_core_set_scanning(struct bcm4360_core *c, bool on)
{
	struct core_wlchw *w = &c->w;

	if (!c->up || on == (w->scanning != 0))
		return;
	if (on) {
		if (c->phy)
			bcm4360_phy_hold_upd(c->phy, 2, true);
		w->scanning = 1;
		set_wake_ctrl(c);
		bcm4360_cca_stats_upd(c->cca, 0);
		mhf(c, 1, 0x0400, 0x0400, 3);
		mhf(c, 1, 0x0010, 0x0010, 3);
		w->bcn_promisc_scan = 1;
		mac_bcn_promisc(c);
		if (w->associated)
			CORE_MAC_CALL(c, bcm4360_bmac_tx_fifo_suspend(m, 1));
	} else {
		mac_suspend(c);
		mhf(c, 1, 0x0400, 0, 3);
		mhf(c, 1, 0x0010, 0, 3);
		w->bcn_promisc_scan = 0;
		mac_bcn_promisc(c);
		mac_enable(c);
		if (c->phy)
			bcm4360_phy_clear_tssi(c->phy);
		if (w->associated)
			CORE_MAC_CALL(c, bcm4360_bmac_tx_fifo_resume(m, 1));
		w->scanning = 0;
		if (c->phy)
			bcm4360_phy_hold_upd(c->phy, 2, false);
		set_wake_ctrl(c);
	}
}
