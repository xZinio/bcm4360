/*
 * Hybrid build: the PHY (Broadcom's wlc_phy_cmn.c and wlc_phy_ac.c) by the
 * open PHY of open/phy (docs/re/spec/acphy-api.md and the specifications it
 * names).
 *
 * The MAC layer of the object reaches the PHY through 79 functions and never
 * looks into the PHY's state (acphy-api.md). All 79 are taken over here
 * (hybrid/takeover.txt). With the switch open_phy on, the shared attach and
 * the attach make an open PHY and give the MAC a handle to it; every other
 * entry point then calls the open function. A handle the open side did not
 * make is Broadcom's, and the call goes to the object's own function
 * (bcm4360_blob_*): with open_phy off, for a chip the open PHY does not know,
 * and when the open attach refuses the card (it then falls back to Broadcom's
 * PHY for that card).
 *
 * The services the open PHY needs from the other layers (phy_env.h, hw.h)
 * are the object's: the PHY shim of the MAC (wlapi_*), the backplane (si_*),
 * the OTP, the board variables and Broadcom's tables.
 */
#include "hybrid.h"
#include <bcm4360/phy.h>
#include <bcm4360/phy_env.h>

#ifdef __KERNEL__
#include <linux/module.h>
#endif

bool bcm4360_open_phy = true;

#ifdef __KERNEL__
module_param_named(open_phy, bcm4360_open_phy, bool, 0444);
MODULE_PARM_DESC(open_phy, "PHY and radio: open code (1, default) or Broadcom's (0)");
#endif

/* ------------------------------------------------------------ the object's side */

/* the record wlc_bmac_attach passes to wlc_phy_shared_attach (acphy-attach.md) */
struct hyb_phy_params {
	void *osh;
	const void *sih;
	void *physhim;
	u32 unit;
	u32 corerev;
	u32 bustype;
	u32 buscorerev;
	char *vars;
	u16 vendor;
	u16 device;
	u32 chip;
	u32 chiprev;
	u32 chippkg;
	u32 sromrev;
	u32 boardtype;
	u32 boardrev;
	u32 boardvendor;
	u32 boardflags;
	u32 boardflags2;
};

/* services of the MAC's PHY shim (wlc_phy_shim.c) and of the backplane */
void wlapi_suspend_mac_and_wait(void *physhim);
void wlapi_enable_mac(void *physhim);
void wlapi_bmac_corereset(void *physhim, u32 flags);
void wlapi_bmac_bw_set(void *physhim, u16 bw);
void wlapi_bmac_phyclk_fgc(void *physhim, u8 clk);
void wlapi_bmac_mhf(void *physhim, u8 idx, u16 mask, u16 val, u32 bands);
void wlapi_bmac_mctrl(void *physhim, u32 mask, u32 val);
u32 wlapi_bmac_btc_mode_get(void *physhim);
u8 wlapi_is_eci_coex_enabled(void *physhim);	/* 0 or 1 (tools/re/blackbox.py eci) */
void wlapi_bmac_ucode_wake_override_phyreg_set(void *physhim);
void wlapi_bmac_ucode_wake_override_phyreg_clear(void *physhim);
void wlapi_update_bt_chanspec(void *physhim, u16 chanspec, u8 scan, u8 measure);
void wlapi_high_update_phy_mode(void *physhim, u32 mode);
void wlapi_high_update_txppr_offset(void *physhim, void *offsets);
void *wlapi_init_timer(void *physhim, void (*fn)(void *arg), void *arg, const char *name);
void wlapi_add_timer(void *physhim, void *t, u32 ms, int periodic);
u8 wlapi_del_timer(void *physhim, void *t);
void wlapi_free_timer(void *physhim, void *t);
u32 si_core_sflags(const void *sih, u32 mask, u32 val);
u32 si_core_cflags(const void *sih, u32 mask, u32 val);
u32 si_corereg(const void *sih, u32 coreidx, u32 regoff, u32 mask, u32 val);
u32 si_gpiocontrol(const void *sih, u32 mask, u32 val, u8 priority);
u32 si_gpioout(const void *sih, u32 mask, u32 val, u8 priority);
u32 si_gpioouten(const void *sih, u32 mask, u32 val, u8 priority);
u32 si_get_sromctl(const void *sih);
int si_set_sromctl(const void *sih, u32 value);
u32 si_alp_clock(const void *sih);
int otp_read_word(const void *sih, u32 wn, u16 *data);
char *getvar(char *vars, const char *name);

/* ------------------------------------------------------------ cards and handles */

/*
 * The open side's `sh`: what the MAC handed to wlc_phy_shared_attach, and
 * the platform of the open PHY. Broadcom's own `sh` is made too, so that a
 * card the open PHY refuses can still have Broadcom's.
 */
struct hyb_phy_sh {
	struct bcm4360_hw hw;		/* first: the services get &hw */
	struct hyb_phy_params p;
	void *blob_sh;			/* Broadcom's sh for the same parameters */
	struct bcm4360_phy_board board;
	struct bcm4360_phy *phy;	/* the open PHY of both bands, or NULL */
	bool fallback;			/* the open attach refused: Broadcom's PHY */
	bool used;
};

#define HYB_PHYS	4
static struct hyb_phy_sh hyb_phys[HYB_PHYS];

static struct hyb_phy_sh *hyb_sh(const void *sh)
{
	int i;

	for (i = 0; i < HYB_PHYS; i++) {
		if (hyb_phys[i].used && &hyb_phys[i] == sh)
			return &hyb_phys[i];
	}
	return NULL;
}

/* the open PHY behind a handle the MAC holds, or NULL: Broadcom's */
static struct bcm4360_phy *hyb_phy(const void *pi)
{
	int i;

	if (!pi)
		return NULL;
	for (i = 0; i < HYB_PHYS; i++) {
		if (hyb_phys[i].used && hyb_phys[i].phy == pi)
			return hyb_phys[i].phy;
	}
	return NULL;
}

static struct hyb_phy_sh *hyb_sh_of_hw(struct bcm4360_hw *hw)
{
	return (struct hyb_phy_sh *)hw;
}

/* ------------------------------------------------------------ services (phy_env.h, hw.h) */

void bcm4360_mac_suspend(struct bcm4360_hw *hw)
{
	wlapi_suspend_mac_and_wait(hyb_sh_of_hw(hw)->p.physhim);
}

void bcm4360_mac_enable(struct bcm4360_hw *hw)
{
	wlapi_enable_mac(hyb_sh_of_hw(hw)->p.physhim);
}

void bcm4360_mac_corereset(struct bcm4360_hw *hw, u32 flags)
{
	wlapi_bmac_corereset(hyb_sh_of_hw(hw)->p.physhim, flags);
}

void bcm4360_mac_bw_set(struct bcm4360_hw *hw, u16 bw)
{
	wlapi_bmac_bw_set(hyb_sh_of_hw(hw)->p.physhim, bw);
}

void bcm4360_mac_phyclk_fgc(struct bcm4360_hw *hw, bool on)
{
	wlapi_bmac_phyclk_fgc(hyb_sh_of_hw(hw)->p.physhim, on);
}

void bcm4360_mac_mhf(struct bcm4360_hw *hw, u8 idx, u16 mask, u16 val, u32 bands)
{
	wlapi_bmac_mhf(hyb_sh_of_hw(hw)->p.physhim, idx, mask, val, bands);
}

void bcm4360_mac_mctrl(struct bcm4360_hw *hw, u32 mask, u32 val)
{
	wlapi_bmac_mctrl(hyb_sh_of_hw(hw)->p.physhim, mask, val);
}

u32 bcm4360_mac_btc_mode(struct bcm4360_hw *hw)
{
	return wlapi_bmac_btc_mode_get(hyb_sh_of_hw(hw)->p.physhim);
}

bool bcm4360_mac_eci_coex_enabled(struct bcm4360_hw *hw)
{
	return wlapi_is_eci_coex_enabled(hyb_sh_of_hw(hw)->p.physhim) != 0;
}

void bcm4360_mac_wake_override_set(struct bcm4360_hw *hw)
{
	wlapi_bmac_ucode_wake_override_phyreg_set(hyb_sh_of_hw(hw)->p.physhim);
}

void bcm4360_mac_wake_override_clear(struct bcm4360_hw *hw)
{
	wlapi_bmac_ucode_wake_override_phyreg_clear(hyb_sh_of_hw(hw)->p.physhim);
}

void bcm4360_mac_update_bt_chanspec(struct bcm4360_hw *hw, u16 chanspec, bool scan,
				    bool measure)
{
	wlapi_update_bt_chanspec(hyb_sh_of_hw(hw)->p.physhim, chanspec, scan, measure);
}

void bcm4360_mac_update_phy_mode(struct bcm4360_hw *hw, u32 mode)
{
	wlapi_high_update_phy_mode(hyb_sh_of_hw(hw)->p.physhim, mode);
}

void *wlapi_get_wlc_info(void *physhim);
u32 wlc_ol_noise_avg_offload(void *wlc);

/* phy_env.h: the noise average of the on-chip ARM's offload (0: none answers) */
s8 bcm4360_mac_ol_noise_avg(struct bcm4360_hw *hw)
{
	return (s8)wlc_ol_noise_avg_offload(wlapi_get_wlc_info(hyb_sh_of_hw(hw)->p.physhim));
}

void wlapi_noise_cb(void *physhim, u8 chan, s8 v);

void bcm4360_mac_noise_cb(struct bcm4360_hw *hw, u8 chan, s8 v)
{
	wlapi_noise_cb(hyb_sh_of_hw(hw)->p.physhim, chan, v);
}

void wlapi_bmac_btc_period_get(void *physhim, u16 *period, u8 *state);

void bcm4360_mac_btc_period_get(struct bcm4360_hw *hw, u16 *period, u8 *state)
{
	wlapi_bmac_btc_period_get(hyb_sh_of_hw(hw)->p.physhim, period, state);
}

void bcm4360_mac_update_txppr_offset(struct bcm4360_hw *hw, const void *offsets)
{
	wlapi_high_update_txppr_offset(hyb_sh_of_hw(hw)->p.physhim, (void *)offsets);
}

void *bcm4360_timer_init(struct bcm4360_hw *hw, void (*fn)(void *arg), void *arg,
			 const char *name)
{
	return wlapi_init_timer(hyb_sh_of_hw(hw)->p.physhim, fn, arg, name);
}

void bcm4360_timer_add(struct bcm4360_hw *hw, void *timer, u32 ms, bool periodic)
{
	wlapi_add_timer(hyb_sh_of_hw(hw)->p.physhim, timer, ms, periodic);
}

bool bcm4360_timer_del(struct bcm4360_hw *hw, void *timer)
{
	return wlapi_del_timer(hyb_sh_of_hw(hw)->p.physhim, timer) != 0;
}

void bcm4360_timer_free(struct bcm4360_hw *hw, void *timer)
{
	wlapi_free_timer(hyb_sh_of_hw(hw)->p.physhim, timer);
}

u32 bcm4360_chip_core_sflags(struct bcm4360_hw *hw, u32 mask, u32 val)
{
	return si_core_sflags(hyb_sh_of_hw(hw)->p.sih, mask, val);
}

u32 bcm4360_chip_core_cflags(struct bcm4360_hw *hw, u32 mask, u32 val)
{
	return si_core_cflags(hyb_sh_of_hw(hw)->p.sih, mask, val);
}

u32 bcm4360_chip_corereg(struct bcm4360_hw *hw, u32 coreidx, u32 offset, u32 mask, u32 val)
{
	return si_corereg(hyb_sh_of_hw(hw)->p.sih, coreidx, offset, mask, val);
}

u32 bcm4360_chip_gpiocontrol(struct bcm4360_hw *hw, u32 mask, u32 val, u8 priority)
{
	return si_gpiocontrol(hyb_sh_of_hw(hw)->p.sih, mask, val, priority);
}

u32 bcm4360_chip_gpioout(struct bcm4360_hw *hw, u32 mask, u32 val, u8 priority)
{
	return si_gpioout(hyb_sh_of_hw(hw)->p.sih, mask, val, priority);
}

u32 bcm4360_chip_gpioouten(struct bcm4360_hw *hw, u32 mask, u32 val, u8 priority)
{
	return si_gpioouten(hyb_sh_of_hw(hw)->p.sih, mask, val, priority);
}

u32 bcm4360_chip_sromctl_get(struct bcm4360_hw *hw)
{
	return si_get_sromctl(hyb_sh_of_hw(hw)->p.sih);
}

void bcm4360_chip_sromctl_set(struct bcm4360_hw *hw, u32 val)
{
	si_set_sromctl(hyb_sh_of_hw(hw)->p.sih, val);
}

int bcm4360_otp_read_word(struct bcm4360_hw *hw, u32 word, u16 *data)
{
	return otp_read_word(hyb_sh_of_hw(hw)->p.sih, word, data);
}

const char *hw_getvar(struct bcm4360_hw *hw, const char *name)
{
	return getvar(hyb_sh_of_hw(hw)->p.vars, name);
}

/*
 * The MAC's per-rate power container (wlc_ppr.c), a service of the open PHY
 * (phy_env.h; docs/re/spec/acphy-api-txpower.md). The object's functions take
 * the same arguments in the same order, except that create and delete take
 * the OS handle. Narrow results of the object are taken as u32 and truncated
 * (the object, built with GCC, does not extend them).
 */
void *ppr_create(void *osh, u32 bw);
void ppr_delete(void *osh, void *ppr);
void ppr_clear(void *ppr);
void ppr_copy_struct(const void *src, void *dst);
u32 ppr_get_ch_bw(const void *ppr);
void ppr_set_cmn_val(void *ppr, s32 v);
void ppr_plus_cmn_val(void *ppr, s32 d);
void ppr_minus_cmn_val(void *ppr, s32 d);
void ppr_cmn_val_minus(void *ppr, s32 v);
void ppr_apply_min(void *ppr, s32 v);
void ppr_apply_max(void *ppr, s32 v);
void ppr_compare_min(void *a, void *b);
void ppr_compare_max(void *a, void *b);
void ppr_multiply_percentage(void *ppr, u32 pct);
void ppr_force_disabled(void *ppr, s32 min);
u32 ppr_get_max(const void *ppr);
u32 ppr_get_min(const void *ppr, s32 floor);
void ppr_get_ofdm(const void *ppr, u32 m, u32 rateset, u32 bw, s8 *out);
void ppr_set_ofdm(void *ppr, u32 m, u32 rateset, u32 bw, const s8 *in);
void ppr_get_ht_mcs(const void *ppr, u32 nss, u32 m, u32 rateset, u32 bw, s8 *out);
void ppr_set_ht_mcs(void *ppr, u32 nss, u32 m, u32 rateset, u32 bw, const s8 *in);
void ppr_get_vht_mcs(const void *ppr, u32 nss, u32 m, u32 rateset, u32 bw, s8 *out);
void ppr_set_vht_mcs(void *ppr, u32 nss, u32 m, u32 rateset, u32 bw, const s8 *in);
void ppr_get_dsss(const void *ppr, u32 m, u32 rateset, s8 *out);
void ppr_set_dsss(void *ppr, u32 m, u32 rateset, const s8 *in);

void *bcm4360_ppr_create(struct bcm4360_hw *hw, u8 bw_sel)
{
	return ppr_create(hyb_sh_of_hw(hw)->p.osh, bw_sel);
}

void bcm4360_ppr_delete(struct bcm4360_hw *hw, void *ppr)
{
	ppr_delete(hyb_sh_of_hw(hw)->p.osh, ppr);
}

void bcm4360_ppr_clear(void *ppr)
{
	ppr_clear(ppr);
}

void bcm4360_ppr_copy(const void *src, void *dst)
{
	ppr_copy_struct(src, dst);
}

u8 bcm4360_ppr_get_ch_bw(const void *ppr)
{
	return (u8)ppr_get_ch_bw(ppr);
}

void bcm4360_ppr_set_cmn_val(void *ppr, s8 v)
{
	ppr_set_cmn_val(ppr, v);
}

void bcm4360_ppr_plus_cmn_val(void *ppr, s8 d)
{
	ppr_plus_cmn_val(ppr, d);
}

void bcm4360_ppr_minus_cmn_val(void *ppr, s8 d)
{
	ppr_minus_cmn_val(ppr, d);
}

void bcm4360_ppr_cmn_val_minus(void *ppr, s8 v)
{
	ppr_cmn_val_minus(ppr, v);
}

void bcm4360_ppr_apply_min(void *ppr, s8 v)
{
	ppr_apply_min(ppr, v);
}

void bcm4360_ppr_apply_max(void *ppr, s8 v)
{
	ppr_apply_max(ppr, v);
}

void bcm4360_ppr_compare_min(void *a, void *b)
{
	ppr_compare_min(a, b);
}

void bcm4360_ppr_compare_max(void *a, void *b)
{
	ppr_compare_max(a, b);
}

void bcm4360_ppr_multiply_percentage(void *ppr, u8 pct)
{
	ppr_multiply_percentage(ppr, pct);
}

void bcm4360_ppr_force_disabled(void *ppr, s8 min)
{
	ppr_force_disabled(ppr, min);
}

s8 bcm4360_ppr_get_max(const void *ppr)
{
	return (s8)ppr_get_max(ppr);
}

s8 bcm4360_ppr_get_min(const void *ppr, s8 floor)
{
	return (s8)ppr_get_min(ppr, floor);
}

void bcm4360_ppr_get_ofdm(const void *ppr, u8 m, u8 rateset, u8 bw, s8 *out)
{
	ppr_get_ofdm(ppr, m, rateset, bw, out);
}

void bcm4360_ppr_set_ofdm(void *ppr, u8 m, u8 rateset, u8 bw, const s8 *in)
{
	ppr_set_ofdm(ppr, m, rateset, bw, in);
}

void bcm4360_ppr_get_ht_mcs(const void *ppr, u8 nss, u8 m, u8 rateset, u8 bw, s8 *out)
{
	ppr_get_ht_mcs(ppr, nss, m, rateset, bw, out);
}

void bcm4360_ppr_set_ht_mcs(void *ppr, u8 nss, u8 m, u8 rateset, u8 bw, const s8 *in)
{
	ppr_set_ht_mcs(ppr, nss, m, rateset, bw, in);
}

void bcm4360_ppr_get_vht_mcs(const void *ppr, u8 nss, u8 m, u8 rateset, u8 bw, s8 *out)
{
	ppr_get_vht_mcs(ppr, nss, m, rateset, bw, out);
}

void bcm4360_ppr_set_vht_mcs(void *ppr, u8 nss, u8 m, u8 rateset, u8 bw, const s8 *in)
{
	ppr_set_vht_mcs(ppr, nss, m, rateset, bw, in);
}

void bcm4360_ppr_get_dsss(const void *ppr, u8 m, u8 rateset, s8 *out)
{
	ppr_get_dsss(ppr, m, rateset, out);
}

void bcm4360_ppr_set_dsss(void *ppr, u8 m, u8 rateset, const s8 *in)
{
	ppr_set_dsss(ppr, m, rateset, in);
}

/* Broadcom's tables, where they are in the linked object (hybrid/fwdata.h) */
#define HYB_FW(name, size)	extern const u8 name[];
#include "fwdata.h"
#undef HYB_FW

struct hyb_fw {
	const char *name;
	const void *data;
	u32 size;
};

static const struct hyb_fw hyb_fw[] = {
#define HYB_FW(name, size)	{ #name, name, size },
#include "fwdata.h"
#undef HYB_FW
};

const void *hw_fw_data(struct bcm4360_hw *hw, const char *name, u32 *size)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(hyb_fw); i++) {
		if (!hw_strcmp(hyb_fw[i].name, name)) {
			if (size)
				*size = hyb_fw[i].size;
			return hyb_fw[i].data;
		}
	}
	return NULL;
}

/* ------------------------------------------------------------ attach, detach */

HYB_TAKE(void *, wlc_phy_shared_attach, (void *params));
HYB_TAKE(void, wlc_phy_shared_detach, (void *sh));
HYB_TAKE(void *, wlc_phy_attach, (void *sh, void *regs, int bandtype, char *vars));
HYB_TAKE(void, wlc_phy_detach, (void *pi));

/* acphy-api.md, A1 */
void *wlc_phy_shared_attach(void *params)
{
	const struct hyb_phy_params *p = params;
	struct hyb_phy_sh *s = NULL;
	void *blob_sh;
	int i;

	blob_sh = bcm4360_blob_wlc_phy_shared_attach(params);
	if (!blob_sh || !bcm4360_open_phy || p->chip != BCM4360_CHIP_ID_4360)
		return blob_sh;
	for (i = 0; i < HYB_PHYS && !s; i++) {
		if (!hyb_phys[i].used)
			s = &hyb_phys[i];
	}
	if (!s)
		return blob_sh;
	hw_memset(s, 0, sizeof(*s));
	s->used = true;
	hw_memcpy(&s->p, p, sizeof(s->p));
	s->blob_sh = blob_sh;
	return s;
}

void wlc_phy_shared_detach(void *sh)
{
	struct hyb_phy_sh *s = hyb_sh(sh);

	if (!s) {
		bcm4360_blob_wlc_phy_shared_detach(sh);
		return;
	}
	bcm4360_blob_wlc_phy_shared_detach(s->blob_sh);
	s->used = false;
}

/* what the open PHY needs to know before it attaches (struct bcm4360_phy_board) */
static void hyb_phy_board(struct hyb_phy_sh *s, void *regs)
{
	struct bcm4360_phy_board *b = &s->board;
	const struct hyb_phy_params *p = &s->p;
	struct hyb_card *c = hyb_card_of(p->sih);

	if (c)
		hw_memcpy(&s->hw, &c->hw, sizeof(s->hw));
	s->hw.d11 = regs;
	s->hw.pdev = p->osh;

	b->chip = p->chip;
	b->chiprev = p->chiprev;
	b->chippkg = p->chippkg;
	b->corerev = p->corerev;
	b->sromrev = p->sromrev;
	b->boardtype = p->boardtype;
	b->boardrev = p->boardrev;
	b->boardvendor = p->boardvendor;
	b->boardflags = p->boardflags;
	b->boardflags2 = p->boardflags2;
	b->xtal_hz = si_alp_clock(p->sih);	/* pi+0xc24 of the object */
	b->pci_vendor = p->vendor;
	b->pci_device = p->device;
	b->pmu = hyb_pmu_of(p->sih);
}

/* acphy-api.md, A2: 2 = 2.4 GHz, 1 = 5 GHz; the 2.4 GHz band attaches first */
#define HYB_BAND_2G	2

void *wlc_phy_attach(void *sh, void *regs, int bandtype, char *vars)
{
	struct hyb_phy_sh *s = hyb_sh(sh);

	if (!s)
		return bcm4360_blob_wlc_phy_attach(sh, regs, bandtype, vars);
	if (s->fallback)
		return bcm4360_blob_wlc_phy_attach(s->blob_sh, regs, bandtype, vars);
	if (s->phy)
		return bcm4360_phy_attach_band(s->phy, bandtype) ? s->phy : NULL;
	if (bandtype == HYB_BAND_2G) {
		s->p.vars = vars;
		hyb_phy_board(s, regs);
		s->phy = bcm4360_phy_attach(&s->hw, &s->board);
		if (s->phy)
			return s->phy;
	}
	/* the open PHY does not take this card: Broadcom's PHY for it */
	s->fallback = true;
	return bcm4360_blob_wlc_phy_attach(s->blob_sh, regs, bandtype, vars);
}

void wlc_phy_detach(void *pi)
{
	struct bcm4360_phy *phy = hyb_phy(pi);
	int i;

	if (!phy) {
		bcm4360_blob_wlc_phy_detach(pi);
		return;
	}
	/* the last detach frees the PHY: forget the handle first */
	if (phy->refcnt == 1) {
		for (i = 0; i < HYB_PHYS; i++) {
			if (hyb_phys[i].used && hyb_phys[i].phy == phy)
				hyb_phys[i].phy = NULL;
		}
	}
	bcm4360_phy_detach(phy);
}

/* ------------------------------------------------------------ the other entry points */

/*
 * Each takes Broadcom's PHY handle `pi`: an open PHY goes to the open
 * function, any other handle to the object's own function. The object's
 * bool arguments and results are bytes (u8).
 */
#define HYB_PHY_OR_BLOB(name, ...)					\
	struct bcm4360_phy *phy = hyb_phy(pi);				\
	if (!phy)							\
		return bcm4360_blob_##name(pi, ##__VA_ARGS__)

HYB_TAKE(u8, wlc_phy_get_phyversion, (void *pi, u16 *type, u16 *rev, u16 *radioid,
				      u16 *radiover));
u8 wlc_phy_get_phyversion(void *pi, u16 *type, u16 *rev, u16 *radioid, u16 *radiover)
{
	HYB_PHY_OR_BLOB(wlc_phy_get_phyversion, type, rev, radioid, radiover);
	bcm4360_phy_get_phyversion(phy, type, rev, radioid, radiover);
	return 1;
}

HYB_TAKE(u32, wlc_phy_get_coreflags, (void *pi));
u32 wlc_phy_get_coreflags(void *pi)
{
	HYB_PHY_OR_BLOB(wlc_phy_get_coreflags);
	return bcm4360_phy_get_coreflags(phy);
}

HYB_TAKE(u8, wlc_phy_get_encore, (void *pi));
u8 wlc_phy_get_encore(void *pi)
{
	HYB_PHY_OR_BLOB(wlc_phy_get_encore);
	return bcm4360_phy_get_encore(phy);
}

HYB_TAKE(u32, wlc_phy_cap_get, (void *pi));
u32 wlc_phy_cap_get(void *pi)
{
	HYB_PHY_OR_BLOB(wlc_phy_cap_get);
	return bcm4360_phy_cap_get(phy);
}

HYB_TAKE(void, wlc_phy_machwcap_set, (void *pi, u32 caps));
void wlc_phy_machwcap_set(void *pi, u32 caps)
{
	HYB_PHY_OR_BLOB(wlc_phy_machwcap_set, caps);
	bcm4360_phy_machwcap_set(phy, caps);
}

HYB_TAKE(void, wlc_phy_hw_clk_state_upd, (void *pi, u8 on));
void wlc_phy_hw_clk_state_upd(void *pi, u8 on)
{
	HYB_PHY_OR_BLOB(wlc_phy_hw_clk_state_upd, on);
	bcm4360_phy_hw_clk_state_upd(phy, on != 0);
}

HYB_TAKE(void, wlc_phy_hw_state_upd, (void *pi, u8 up));
void wlc_phy_hw_state_upd(void *pi, u8 up)
{
	HYB_PHY_OR_BLOB(wlc_phy_hw_state_upd, up);
	bcm4360_phy_hw_state_upd(phy, up != 0);
}

HYB_TAKE(void, wlc_phy_por_inform, (void *pi));
void wlc_phy_por_inform(void *pi)
{
	HYB_PHY_OR_BLOB(wlc_phy_por_inform);
	bcm4360_phy_por_inform(phy);
}

HYB_TAKE(void, wlc_phy_anacore, (void *pi, u8 on));
void wlc_phy_anacore(void *pi, u8 on)
{
	HYB_PHY_OR_BLOB(wlc_phy_anacore, on);
	bcm4360_phy_anacore(phy, on != 0);
}

HYB_TAKE(void, wlc_phy_switch_radio, (void *pi, u8 on));
void wlc_phy_switch_radio(void *pi, u8 on)
{
	HYB_PHY_OR_BLOB(wlc_phy_switch_radio, on);
	bcm4360_phy_switch_radio(phy, on != 0);
}

HYB_TAKE(void, wlc_phy_cal_init, (void *pi));
void wlc_phy_cal_init(void *pi)
{
	HYB_PHY_OR_BLOB(wlc_phy_cal_init);
	bcm4360_phy_cal_init(phy);
}

HYB_TAKE(void, wlc_phy_init, (void *pi, u16 chanspec));
void wlc_phy_init(void *pi, u16 chanspec)
{
	HYB_PHY_OR_BLOB(wlc_phy_init, chanspec);
	bcm4360_phy_init(phy, chanspec);
}

HYB_TAKE(u32, wlc_phy_down, (void *pi));
u32 wlc_phy_down(void *pi)
{
	HYB_PHY_OR_BLOB(wlc_phy_down);
	return bcm4360_phy_down(phy);
}

HYB_TAKE(void, wlc_phy_chanspec_radio_set, (void *pi, u16 chanspec));
void wlc_phy_chanspec_radio_set(void *pi, u16 chanspec)
{
	HYB_PHY_OR_BLOB(wlc_phy_chanspec_radio_set, chanspec);
	bcm4360_phy_chanspec_radio_set(phy, chanspec);
}

HYB_TAKE(u16, wlc_phy_chanspec_get, (void *pi));
u16 wlc_phy_chanspec_get(void *pi)
{
	HYB_PHY_OR_BLOB(wlc_phy_chanspec_get);
	return bcm4360_phy_chanspec_get(phy);
}

HYB_TAKE(void, wlc_phy_chanspec_set, (void *pi, u16 chanspec));
void wlc_phy_chanspec_set(void *pi, u16 chanspec)
{
	HYB_PHY_OR_BLOB(wlc_phy_chanspec_set, chanspec);
	bcm4360_phy_chanspec_set(phy, chanspec);
}

HYB_TAKE(void, wlc_phy_bw_state_set, (void *pi, u16 bw));
void wlc_phy_bw_state_set(void *pi, u16 bw)
{
	HYB_PHY_OR_BLOB(wlc_phy_bw_state_set, bw);
	bcm4360_phy_bw_state_set(phy, bw);
}

HYB_TAKE(u32, wlc_phy_clk_bwbits, (void *pi));
u32 wlc_phy_clk_bwbits(void *pi)
{
	HYB_PHY_OR_BLOB(wlc_phy_clk_bwbits);
	return bcm4360_phy_clk_bwbits(phy);
}

HYB_TAKE(u32, wlc_phy_chanspec_band_firstch, (void *pi, u32 band));
u32 wlc_phy_chanspec_band_firstch(void *pi, u32 band)
{
	HYB_PHY_OR_BLOB(wlc_phy_chanspec_band_firstch, band);
	return bcm4360_phy_chanspec_band_firstch(phy, band);
}

HYB_TAKE(void, wlc_phy_chanspec_band_validch, (void *pi, u32 band, u8 *chanvec));
void wlc_phy_chanspec_band_validch(void *pi, u32 band, u8 *chanvec)
{
	HYB_PHY_OR_BLOB(wlc_phy_chanspec_band_validch, band, chanvec);
	bcm4360_phy_chanspec_band_validch(phy, band, chanvec);
}

HYB_TAKE(void, wlc_phy_chanspec_ch14_widefilter_set, (void *pi, u8 on));
void wlc_phy_chanspec_ch14_widefilter_set(void *pi, u8 on)
{
	HYB_PHY_OR_BLOB(wlc_phy_chanspec_ch14_widefilter_set, on);
	bcm4360_phy_chanspec_ch14_widefilter_set(phy, on != 0);
}

HYB_TAKE(void, wlc_phy_block_bbpll_change, (void *pi, u8 block, u8 going_down));
void wlc_phy_block_bbpll_change(void *pi, u8 block, u8 going_down)
{
	HYB_PHY_OR_BLOB(wlc_phy_block_bbpll_change, block, going_down);
	bcm4360_phy_block_bbpll_change(phy, block != 0, going_down != 0);
}

HYB_TAKE(void, wlc_acphy_set_scramb_dyn_bw_en, (void *pi, u8 on));
void wlc_acphy_set_scramb_dyn_bw_en(void *pi, u8 on)
{
	HYB_PHY_OR_BLOB(wlc_acphy_set_scramb_dyn_bw_en, on);
	bcm4360_phy_set_scramb_dyn_bw_en(phy, on != 0);
}

HYB_TAKE(void, wlc_phy_ldpc_override_set, (void *pi, u8 on));
void wlc_phy_ldpc_override_set(void *pi, u8 on)
{
	HYB_PHY_OR_BLOB(wlc_phy_ldpc_override_set, on);
	bcm4360_phy_ldpc_override_set(phy, on != 0);
}

HYB_TAKE(void, wlc_phy_watchdog, (void *pi));
void wlc_phy_watchdog(void *pi)
{
	HYB_PHY_OR_BLOB(wlc_phy_watchdog);
	bcm4360_phy_watchdog(phy);
}

HYB_TAKE(void, wlc_phy_hold_upd, (void *pi, u32 id, u8 on));
void wlc_phy_hold_upd(void *pi, u32 id, u8 on)
{
	HYB_PHY_OR_BLOB(wlc_phy_hold_upd, id, on);
	bcm4360_phy_hold_upd(phy, id, on != 0);
}

HYB_TAKE(void, wlc_phy_mute_upd, (void *pi, u8 mute, u32 flags));
void wlc_phy_mute_upd(void *pi, u8 mute, u32 flags)
{
	HYB_PHY_OR_BLOB(wlc_phy_mute_upd, mute, flags);
	bcm4360_phy_mute_upd(phy, mute != 0, flags);
}

HYB_TAKE(void, wlc_phy_stf_chain_init, (void *pi, u8 txchain, u8 rxchain));
void wlc_phy_stf_chain_init(void *pi, u8 txchain, u8 rxchain)
{
	HYB_PHY_OR_BLOB(wlc_phy_stf_chain_init, txchain, rxchain);
	bcm4360_phy_stf_chain_init(phy, txchain, rxchain);
}

HYB_TAKE(void, wlc_phy_antsel_type_set, (void *pi, u8 type));
void wlc_phy_antsel_type_set(void *pi, u8 type)
{
	HYB_PHY_OR_BLOB(wlc_phy_antsel_type_set, type);
	bcm4360_phy_antsel_type_set(phy, type);
}

HYB_TAKE(s8, wlc_phy_preamble_override_get, (void *pi));
s8 wlc_phy_preamble_override_get(void *pi)
{
	HYB_PHY_OR_BLOB(wlc_phy_preamble_override_get);
	return bcm4360_phy_preamble_override_get(phy);
}

HYB_TAKE(void, wlc_phy_preamble_override_set, (void *pi, s8 val));
void wlc_phy_preamble_override_set(void *pi, s8 val)
{
	HYB_PHY_OR_BLOB(wlc_phy_preamble_override_set, val);
	bcm4360_phy_preamble_override_set(phy, val);
}

HYB_TAKE(void, wlc_phy_btclock_war, (void *pi, u8 on));
void wlc_phy_btclock_war(void *pi, u8 on)
{
	HYB_PHY_OR_BLOB(wlc_phy_btclock_war, on);
	bcm4360_phy_btclock_war(phy, on != 0);
}

HYB_TAKE(void, wlc_phy_ofdm_rateset_war, (void *pi, u8 on));
void wlc_phy_ofdm_rateset_war(void *pi, u8 on)
{
	HYB_PHY_OR_BLOB(wlc_phy_ofdm_rateset_war, on);
	bcm4360_phy_ofdm_rateset_war(phy, on != 0);
}

HYB_TAKE(void, wlc_phy_bf_preempt_enable, (void *pi, u8 on));
void wlc_phy_bf_preempt_enable(void *pi, u8 on)
{
	HYB_PHY_OR_BLOB(wlc_phy_bf_preempt_enable, on);
	bcm4360_phy_bf_preempt_enable(phy, on != 0);
}

HYB_TAKE(void, wlc_phy_initcal_enable, (void *pi, u8 on));
void wlc_phy_initcal_enable(void *pi, u8 on)
{
	HYB_PHY_OR_BLOB(wlc_phy_initcal_enable, on);
	bcm4360_phy_initcal_enable(phy, on != 0);
}

HYB_TAKE(void, wlc_phy_set_filt_war, (void *pi, u8 on));
void wlc_phy_set_filt_war(void *pi, u8 on)
{
	HYB_PHY_OR_BLOB(wlc_phy_set_filt_war, on);
	bcm4360_phy_set_filt_war(phy, on != 0);
}

HYB_TAKE(void, wlc_phy_tkip_rifs_war, (void *pi, u8 val));
void wlc_phy_tkip_rifs_war(void *pi, u8 val)
{
	HYB_PHY_OR_BLOB(wlc_phy_tkip_rifs_war, val);
	bcm4360_phy_tkip_rifs_war(phy, val);
}

HYB_TAKE(void, wlc_phy_antsel_init, (void *pi, u8 lut_init));
void wlc_phy_antsel_init(void *pi, u8 lut_init)
{
	HYB_PHY_OR_BLOB(wlc_phy_antsel_init, lut_init);
	bcm4360_phy_antsel_init(phy, lut_init != 0);
}

HYB_TAKE(void, wlc_phy_freqtrack_start, (void *pi));
void wlc_phy_freqtrack_start(void *pi)
{
	HYB_PHY_OR_BLOB(wlc_phy_freqtrack_start);
	bcm4360_phy_freqtrack_start(phy);
}

HYB_TAKE(void, wlc_phy_freqtrack_end, (void *pi));
void wlc_phy_freqtrack_end(void *pi)
{
	HYB_PHY_OR_BLOB(wlc_phy_freqtrack_end);
	bcm4360_phy_freqtrack_end(phy);
}

HYB_TAKE(void, wlc_phy_interference_set, (void *pi, u8 init));
void wlc_phy_interference_set(void *pi, u8 init)
{
	HYB_PHY_OR_BLOB(wlc_phy_interference_set, init);
	bcm4360_phy_interference_set(phy, init != 0);
}

HYB_TAKE(void, wlc_phy_acimode_noisemode_reset, (void *pi, u32 channel, u8 clear_aci,
						 u8 clear_noise, u8 disassoc));
void wlc_phy_acimode_noisemode_reset(void *pi, u32 channel, u8 clear_aci, u8 clear_noise,
				     u8 disassoc)
{
	HYB_PHY_OR_BLOB(wlc_phy_acimode_noisemode_reset, channel, clear_aci, clear_noise,
			disassoc);
	bcm4360_phy_acimode_noisemode_reset(phy, channel, clear_aci != 0, clear_noise != 0,
					    disassoc != 0);
}

HYB_TAKE(s8, wlc_phy_get_femctrl_bt_wlan_ovrd, (void *pi));
s8 wlc_phy_get_femctrl_bt_wlan_ovrd(void *pi)
{
	HYB_PHY_OR_BLOB(wlc_phy_get_femctrl_bt_wlan_ovrd);
	return bcm4360_phy_get_femctrl_bt_wlan_ovrd(phy);
}

HYB_TAKE(void, wlc_phy_set_femctrl_bt_wlan_ovrd, (void *pi, s8 state));
void wlc_phy_set_femctrl_bt_wlan_ovrd(void *pi, s8 state)
{
	HYB_PHY_OR_BLOB(wlc_phy_set_femctrl_bt_wlan_ovrd, state);
	bcm4360_phy_set_femctrl_bt_wlan_ovrd(phy, state);
}

HYB_TAKE(const u8 *, wlc_phy_get_ofdm_rate_lookup, (void));
const u8 *wlc_phy_get_ofdm_rate_lookup(void)
{
	if (!bcm4360_open_phy)
		return bcm4360_blob_wlc_phy_get_ofdm_rate_lookup();
	return bcm4360_phy_get_ofdm_rate_lookup();
}

HYB_TAKE(void, wlc_phy_BSSinit, (void *pi, u8 bonlyap, int noise));
void wlc_phy_BSSinit(void *pi, u8 bonlyap, int noise)
{
	HYB_PHY_OR_BLOB(wlc_phy_BSSinit, bonlyap, noise);
	bcm4360_phy_bssinit(phy, bonlyap != 0, (s8)noise);
}

HYB_TAKE(void, wlc_phy_cal_perical, (void *pi, u8 reason));
void wlc_phy_cal_perical(void *pi, u8 reason)
{
	HYB_PHY_OR_BLOB(wlc_phy_cal_perical, reason);
	bcm4360_phy_cal_perical(phy, reason);
}

HYB_TAKE(int, wlc_phy_ioctl, (void *pi, int cmd, u32 len, void *arg));
int wlc_phy_ioctl(void *pi, int cmd, u32 len, void *arg)
{
	HYB_PHY_OR_BLOB(wlc_phy_ioctl, cmd, len, arg);
	return bcm4360_phy_ioctl(phy, cmd, len, arg);
}

HYB_TAKE(int, wlc_phy_iovar_dispatch, (void *pi, u32 actionid, u16 type, void *p, u32 plen,
				       void *a, int alen, int vsize));
int wlc_phy_iovar_dispatch(void *pi, u32 actionid, u16 type, void *p, u32 plen, void *a,
			   int alen, int vsize)
{
	HYB_PHY_OR_BLOB(wlc_phy_iovar_dispatch, actionid, type, p, plen, a, alen, vsize);
	return bcm4360_phy_iovar_dispatch(phy, actionid, type, p, plen, a, alen, vsize);
}

/* ---- transmit power (docs/re/spec/acphy-api-txpower.md) */

HYB_TAKE(void, wlc_phy_txpower_limit_set, (void *pi, void *txpwr, u16 chanspec));
void wlc_phy_txpower_limit_set(void *pi, void *txpwr, u16 chanspec)
{
	HYB_PHY_OR_BLOB(wlc_phy_txpower_limit_set, txpwr, chanspec);
	bcm4360_phy_txpower_limit_set(phy, txpwr);
}

HYB_TAKE(int, wlc_phy_txpower_set, (void *pi, u32 qdbm, u8 override, void *reg_pwr));
int wlc_phy_txpower_set(void *pi, u32 qdbm, u8 override, void *reg_pwr)
{
	HYB_PHY_OR_BLOB(wlc_phy_txpower_set, qdbm, override, reg_pwr);
	return bcm4360_phy_txpower_set(phy, qdbm, override != 0, reg_pwr);
}

HYB_TAKE(int, wlc_phy_txpower_get, (void *pi, u32 *qdbm, u8 *override));
int wlc_phy_txpower_get(void *pi, u32 *qdbm, u8 *override)
{
	HYB_PHY_OR_BLOB(wlc_phy_txpower_get, qdbm, override);
	return bcm4360_phy_txpower_get(phy, qdbm, override);
}

HYB_TAKE(void, wlc_phy_txpower_sromlimit, (void *pi, u16 chanspec, s8 *min_pwr, void *max_pwr,
					   u8 core));
void wlc_phy_txpower_sromlimit(void *pi, u16 chanspec, s8 *min_pwr, void *max_pwr, u8 core)
{
	HYB_PHY_OR_BLOB(wlc_phy_txpower_sromlimit, chanspec, min_pwr, max_pwr, core);
	bcm4360_phy_txpower_sromlimit(phy, chanspec, min_pwr, max_pwr, core);
}

HYB_TAKE(u8, wlc_phy_txpower_get_target_max, (void *pi));
u8 wlc_phy_txpower_get_target_max(void *pi)
{
	HYB_PHY_OR_BLOB(wlc_phy_txpower_get_target_max);
	return bcm4360_phy_txpower_get_target_max(phy);
}

HYB_TAKE(u8, wlc_phy_txpower_get_target_min, (void *pi));
u8 wlc_phy_txpower_get_target_min(void *pi)
{
	HYB_PHY_OR_BLOB(wlc_phy_txpower_get_target_min);
	return bcm4360_phy_txpower_get_target_min(phy);
}

HYB_TAKE(u8, wlc_phy_txpower_hw_ctrl_get, (void *pi));
u8 wlc_phy_txpower_hw_ctrl_get(void *pi)
{
	HYB_PHY_OR_BLOB(wlc_phy_txpower_hw_ctrl_get);
	return bcm4360_phy_txpower_hw_ctrl_get(phy);
}

HYB_TAKE(u8, wlc_phy_txpower_ipa_ison, (void *pi));
u8 wlc_phy_txpower_ipa_ison(void *pi)
{
	HYB_PHY_OR_BLOB(wlc_phy_txpower_ipa_ison);
	return bcm4360_phy_txpower_ipa_ison(phy);
}

HYB_TAKE(void, wlc_phy_txpwr_percent_set, (void *pi, u8 percent));
void wlc_phy_txpwr_percent_set(void *pi, u8 percent)
{
	HYB_PHY_OR_BLOB(wlc_phy_txpwr_percent_set, percent);
	bcm4360_phy_txpwr_percent_set(phy, percent);
}

HYB_TAKE(int, wlc_phy_txpower_core_offset_set, (void *pi, void *offsets));
int wlc_phy_txpower_core_offset_set(void *pi, void *offsets)
{
	HYB_PHY_OR_BLOB(wlc_phy_txpower_core_offset_set, offsets);
	return bcm4360_phy_txpower_core_offset_set(phy, offsets);
}

HYB_TAKE(void, wlc_phy_get_est_pout, (void *pi, u8 *est_pout, u8 *est_pout_adj,
				      u16 *est_pout_cck));
void wlc_phy_get_est_pout(void *pi, u8 *est_pout, u8 *est_pout_adj, u16 *est_pout_cck)
{
	HYB_PHY_OR_BLOB(wlc_phy_get_est_pout, est_pout, est_pout_adj, est_pout_cck);
	bcm4360_phy_get_est_pout(phy, est_pout, est_pout_adj, est_pout_cck);
}

HYB_TAKE(s8, wlc_phy_get_olpc_pwroffset, (void *pi, u16 bw));
s8 wlc_phy_get_olpc_pwroffset(void *pi, u16 bw)
{
	HYB_PHY_OR_BLOB(wlc_phy_get_olpc_pwroffset, bw);
	return bcm4360_phy_get_olpc_pwroffset(phy, bw);
}

HYB_TAKE(int, wlc_phy_tssivisible_thresh, (void *pi));
int wlc_phy_tssivisible_thresh(void *pi)
{
	HYB_PHY_OR_BLOB(wlc_phy_tssivisible_thresh);
	return bcm4360_phy_tssivisible_thresh(phy);
}

HYB_TAKE(void, wlc_phy_clear_tssi, (void *pi));
void wlc_phy_clear_tssi(void *pi)
{
	HYB_PHY_OR_BLOB(wlc_phy_clear_tssi);
	bcm4360_phy_clear_tssi(phy);
}

/* ---- receive, measurements, chains (docs/re/spec/acphy-api-rx.md) */

HYB_TAKE(void, wlc_phy_rssi_compute, (void *pi, void *rxh));
void wlc_phy_rssi_compute(void *pi, void *rxh)
{
	HYB_PHY_OR_BLOB(wlc_phy_rssi_compute, rxh);
	bcm4360_phy_rssi_compute(phy, rxh);
}

HYB_TAKE(s8, wlc_phy_noise_avg, (void *pi));
s8 wlc_phy_noise_avg(void *pi)
{
	HYB_PHY_OR_BLOB(wlc_phy_noise_avg);
	return bcm4360_phy_noise_avg(phy);
}

HYB_TAKE(void, wlc_phy_noise_sample_intr, (void *pi));
void wlc_phy_noise_sample_intr(void *pi)
{
	HYB_PHY_OR_BLOB(wlc_phy_noise_sample_intr);
	bcm4360_phy_noise_sample_intr(phy);
}

HYB_TAKE(void, wlc_phy_noise_sample_request_external, (void *pi));
void wlc_phy_noise_sample_request_external(void *pi)
{
	HYB_PHY_OR_BLOB(wlc_phy_noise_sample_request_external);
	bcm4360_phy_noise_sample_request_external(phy);
}

HYB_TAKE(void, wlc_phy_interf_rssi_update, (void *pi, u16 chanspec, s8 rssi));
void wlc_phy_interf_rssi_update(void *pi, u16 chanspec, s8 rssi)
{
	HYB_PHY_OR_BLOB(wlc_phy_interf_rssi_update, chanspec, rssi);
	bcm4360_phy_interf_rssi_update(phy, chanspec, rssi);
}

HYB_TAKE(void, wlc_phy_set_deaf, (void *pi, u8 user));
void wlc_phy_set_deaf(void *pi, u8 user)
{
	HYB_PHY_OR_BLOB(wlc_phy_set_deaf, user);
	bcm4360_phy_set_deaf(phy, user != 0);
}

HYB_TAKE(void, wlc_phy_stf_chain_set, (void *pi, u8 txchain, u8 rxchain));
void wlc_phy_stf_chain_set(void *pi, u8 txchain, u8 rxchain)
{
	HYB_PHY_OR_BLOB(wlc_phy_stf_chain_set, txchain, rxchain);
	bcm4360_phy_stf_chain_set(phy, txchain, rxchain);
}

HYB_TAKE(u8, wlc_phy_stf_chain_active_get, (void *pi));
u8 wlc_phy_stf_chain_active_get(void *pi)
{
	HYB_PHY_OR_BLOB(wlc_phy_stf_chain_active_get);
	return bcm4360_phy_stf_chain_active_get(phy);
}

HYB_TAKE(void, wlc_phy_ant_rxdiv_set, (void *pi, u8 val));
void wlc_phy_ant_rxdiv_set(void *pi, u8 val)
{
	HYB_PHY_OR_BLOB(wlc_phy_ant_rxdiv_set, val);
	bcm4360_phy_ant_rxdiv_set(phy, val);
}

HYB_TAKE(u8, wlc_phy_ant_rxdiv_get, (void *pi, u8 *val));
u8 wlc_phy_ant_rxdiv_get(void *pi, u8 *val)
{
	HYB_PHY_OR_BLOB(wlc_phy_ant_rxdiv_get, val);
	return bcm4360_phy_ant_rxdiv_get(phy, val);
}

HYB_TAKE(int, wlc_phy_swdiv_ant_get, (void *pi, u8 bit, u8 *val));
int wlc_phy_swdiv_ant_get(void *pi, u8 bit, u8 *val)
{
	HYB_PHY_OR_BLOB(wlc_phy_swdiv_ant_get, bit, val);
	return bcm4360_phy_swdiv_ant_get(phy, bit, val);
}

HYB_TAKE(int, wlc_phy_swdiv_ant_set, (void *pi, u8 bit, u8 on));
int wlc_phy_swdiv_ant_set(void *pi, u8 bit, u8 on)
{
	HYB_PHY_OR_BLOB(wlc_phy_swdiv_ant_set, bit, on);
	return bcm4360_phy_swdiv_ant_set(phy, bit, on != 0);
}
