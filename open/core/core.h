/*
 * The core's private state (docs/re/07-open-driver.md, milestone 1; task
 * docs/re/tasks/core-env.md).
 *
 * The open MAC layer (open/mac/bmac_attach.c and the others) still works on
 * Broadcom's layouts: `wlc_info_t`, `wlc_pub_t`, `wlc_hw_info_t`, the band
 * states and the backplane handle, addressed by the offsets of
 * docs/re/spec/bmac-attach.md and bmac-init.md ("Data"). The core therefore
 * owns blocks of those layouts; this file names the offsets it uses (the
 * upper layer's fields that only the core reads and writes), and keeps, next
 * to the blocks, the state that Broadcom's upper layer held in other places.
 *
 * Every handle an env function receives leads back to the core:
 *
 *   struct bcm4360_hw *  ->owner
 *   struct bcm4360_mac * ->hw->owner
 *   void *wlc            the word at WLC_CORE_PTR
 *   void *wlc_hw         wlc_hw+0x000 is wlc
 *   void *sih            the first member of struct bcm4360_core
 *   void *wl             the core itself (wlc+0x10)
 *   struct bcm4360_phy * ->hw->owner
 */
#ifndef BCM4360_CORE_PRIV_H
#define BCM4360_CORE_PRIV_H

#include <bcm4360/types.h>
#include <bcm4360/hw.h>
#include <bcm4360/core.h>
#include <bcm4360/si.h>
#include <bcm4360/pmu.h>
#include <bcm4360/mac.h>
#include <bcm4360/phy.h>
#include <bcm4360/clm.h>
#include <bcm4360/cca.h>

#include "frames.h"
#include "wlchw.h"
#include "pskeys.h"
#include "ctl.h"

#define FIELD(p, off, type)	(*(type *)((u8 *)(p) + (off)))

/* The public part of Broadcom's si_t (docs/re/spec/chip-attach.md, A1). */
struct core_sihpub {
	u32 socitype;		/* 0x00 */
	u32 bustype;		/* 0x04 */
	u32 buscoretype;	/* 0x08 */
	u32 buscorerev;		/* 0x0c */
	u32 buscoreidx;		/* 0x10 */
	u32 ccrev;		/* 0x14 */
	u32 cccaps;		/* 0x18 */
	u32 cccaps_ext;		/* 0x1c */
	u32 pmurev;		/* 0x20 */
	u32 pmucaps;		/* 0x24 */
	u32 boardtype;		/* 0x28 */
	u32 boardrev;		/* 0x2c */
	u32 boardvendor;	/* 0x30 */
	u32 boardflags;		/* 0x34 */
	u32 boardflags2;	/* 0x38 */
	u32 chip;		/* 0x3c */
	u32 chiprev;		/* 0x40 */
	u32 chippkg;		/* 0x44 */
	u32 chipst;		/* 0x48 */
	u8  pkg_14_15;		/* 0x4c */
	u8  pad[0x0b];
};

/* The PCIe bridge's state (chip.md, `pcicore_info`; only what this card uses) */
struct core_pcie {
	bool valid;
	u8 devctl;		/* +0x19: configuration offset of Device Control */
	u8 linkctl;		/* +0x18 */
	u8 devctl2;		/* +0x26 */
	u8 pm_cap;		/* +0x1d */
	u8 pme_ok;		/* +0x1e */
	u16 mrrs;		/* +0x22 */
	u16 pm868;		/* +0x20 */
};

#define CORE_MAXCORES		32
#define CORE_NFIFO		6

/* sizes of the blocks in Broadcom's layout that the core allocates */
#define WLC_SIZE		0x1000
#define PUB_SIZE		0x200
#define TUNABLES_N		32
#define UBAND_SIZE		0x120
#define WLC_CORESTATE_SIZE	0x80
#define STF_STATE_SIZE		0x400
#define COEX_SIZE		0x40
#define BSSCFG_SIZE		0x400
#define PM_SIZE			0x40
#define PUBCNT_SIZE		0x100

/* wlc_info_t (spec bmac-attach.md, bmac-init.md "Data", cca-stf.md "Data") */
#define WLC_PUB			0x000
#define WLC_OSH			0x008
#define WLC_WL			0x010
#define WLC_REGS		0x018
#define WLC_HW			0x020
#define WLC_CORESTATE		0x028
#define WLC_CLK			0x031
#define WLC_CORE		0x038	/* pointer to the upper layer's "core" state */
#define WLC_BAND		0x040
#define WLC_BANDSTATE(u)	(0x050 + 8 * (u))
#define WLC_FLAG_64		0x064
#define WLC_WORD_68		0x068
#define WLC_PRIMARY_BSSCFG	0x2f8
#define WLC_FLAGS_304		0x304
#define WLC_TXC_GEN		0x2a8
#define WLC_CHANSPEC		0x518
#define WLC_STF			0x550
#define WLC_BCN_RATE		0x584
#define WLC_CCA			0x628
#define WLC_COEX		0x690
#define WLC_FLAG_718		0x718
#define WLC_OFFLOAD		0x750
#define WLC_CORE_PTR		0xff8	/* the core's own back pointer (not Broadcom's) */

/* the upper layer's "core" state (first word: the core index) */
#define CORE_COREIDX		0x000
#define CORE_TXPKTPEND		0x038

/* wlc_pub_t */
#define PUB_ETHERADDR		0x008
#define PUB_UNIT		0x010
#define PUB_COREREV		0x014
#define PUB_UP			0x034
#define PUB_TUNABLES		0x038
#define PUB_HW_UP		0x040
#define PUB_NBANDS		0x044
#define PUB_SHM_CORE_BASE	0x0e4
#define PUB_BOARDFLAGS2_HI	0x09d
#define PUB_CNT			0x0a0
#define PUB_SIH			0x100
#define PUB_VARS		0x108
#define PUB_VARS_SIZE		0x110
#define CNT_RESET		0x0b4

/* wlc_hw_info_t */
#define HW_WLC			0x000
#define HW_OSH			0x010
#define HW_UNIT			0x018
#define HW_DI(f)		(0x020 + 8 * (f))
#define HW_COREREV		0x084
#define HW_INTSTATUS		0x094
#define HW_INTMASK		0x098
#define HW_SIH			0x0b8
#define HW_VARS			0x0c0
#define HW_BAND			0x0e8
#define HW_BANDSTATE(u)		(0x0f0 + 8 * (u))
#define HW_LED			0x198
#define BAND_PI			0x028

/* interrupt registers of the 802.11 core, revision 42 */
#define D11_MACINTSTATUS	0x128
#define D11_MACINTMASK		0x12c

/* ChipCommon */
#define CC_CHIPID		0x000
#define CC_CHIPCTL		0x028
#define CC_EROMPTR		0x0fc
#define CC_SROM_CTL		0x190
#define CC_GPIOTIMERVAL		0x088
#define CC_OTP_CTL		0x010
#define CC_OTP_PROG		0x014
#define CC_OTP_LAYOUT		0x01c
#define CC_OTP_CMD		0x018

struct bcm4360_phy;

struct bcm4360_core {
	/* first: the backplane handle `sih` that the open MAC layer reads by offset */
	struct core_sihpub sihp;

	struct bcm4360_hw *hw;
	void *osh;
	void *plat;
	u16 vendor, device;
	u32 msg_level;

	/* the backplane (chip.md, chip-attach.md) */
	struct bcm4360_si si;
	u32 curidx;
	u32 coreid[CORE_MAXCORES];
	u32 coreregbase[CORE_MAXCORES];
	u32 wrapbase[CORE_MAXCORES];
	u32 cia[CORE_MAXCORES];
	u32 cib[CORE_MAXCORES];
	u32 oob_router;
	struct core_pcie pcie;
	struct bcm4360_pmu pmu;
	char *vars;
	u32 varsz;
	bool sprom_checked;

	/* Broadcom's layouts */
	void *wlc;
	void *pub;
	u32 *tunables;
	void *wlc_core;
	void *uband[2];
	void *stf_state;
	void *coex;
	void *bsscfg;
	void *pm;
	void *pubcnt;

	/* the PHY (acphy-api.md) */
	struct bcm4360_phy_board board;
	struct bcm4360_phy *phy;
	bool phy_shared;

	/* the upper layers' small modules */
	struct bcm4360_cca *cca;
	struct bcm4360_clm clm;
	bool clm_ok;

	/* the core's own state */
	bool attached;
	bool up;
	u16 chanspec;
	u32 txpower_qdbm;
	char ccode[4];
	void *wd_timer;		/* the periodic timer, 1 s */
	struct bcm4360_core_info info;

	/* what the upper layer adds (wlchw.c, wlchw.h; docs/re/spec/wlc-hw.md) */
	struct core_wlchw w;

	/* the frame path (frames.c, frames.h; docs/re/spec/frames.md) */
	struct core_frames fr;

	/* power save and hardware keys (keys.c, ps.c; docs/re/spec/ps-keys.md) */
	struct core_pskeys pk;

	/* LEDs, radio switch and MPC, join, coexistence (ctl.c; docs/re/spec/coverage.md G2-G7) */
	struct core_ctl ctl;
};

/* the MAC layer's block, made by its attach */
#define CORE_WLC_HW(c)	FIELD((c)->wlc, WLC_HW, void *)

static inline struct bcm4360_core *core_of_hw(struct bcm4360_hw *hw)
{
	return hw->owner;
}

static inline struct bcm4360_core *core_of_mac(struct bcm4360_mac *mac)
{
	return mac->hw->owner;
}

/* the core of a raw wlc_hw, of a wlc, of an sih, of a wl handle */
static inline struct bcm4360_core *core_of_wlc(void *wlc)
{
	return FIELD(wlc, WLC_CORE_PTR, struct bcm4360_core *);
}

static inline struct bcm4360_core *core_of_rawhw(void *wlc_hw)
{
	return core_of_wlc(FIELD(wlc_hw, HW_WLC, void *));
}

static inline struct bcm4360_core *core_of_sih(void *sih)
{
	return (struct bcm4360_core *)sih;
}

static inline void *core_sih(struct bcm4360_core *c)
{
	return &c->sihp;
}

/* mac_view.c: struct bcm4360_mac as a view of the raw wlc_hw (hybrid/mac.c) */
void core_mac_in(struct bcm4360_core *c, struct bcm4360_mac *mac, struct bcm4360_mac *mac0);
void core_mac_out(struct bcm4360_core *c, const struct bcm4360_mac *mac,
		  struct bcm4360_mac *mac0);
void core_mac_flush(struct bcm4360_mac *mac);
void core_mac_reload(struct bcm4360_mac *mac);

/* a call into the MAC layer, as a nested service or from the core itself */
#define CORE_MAC_CALL(c, stmt)						\
	do {								\
		struct bcm4360_mac mac_view_, mac0_view_;		\
		struct bcm4360_mac *m = &mac_view_;			\
									\
		core_mac_in((c), m, &mac0_view_);			\
		stmt;							\
		core_mac_out((c), m, &mac0_view_);			\
	} while (0)

/* the same from inside a service (the state is written back first, read after) */
#define CORE_MAC_NESTED(mac, stmt)					\
	do {								\
		struct bcm4360_core *c_ = core_of_mac(mac);		\
									\
		core_mac_flush(mac);					\
		CORE_MAC_CALL(c_, stmt);				\
		core_mac_reload(mac);					\
	} while (0)

/* the PHY of the current band of the MAC state (wlc_hw+0xe8 -> +0x28) */
static inline struct bcm4360_phy *core_pi_of_mac(struct bcm4360_mac *mac)
{
	void *band = FIELD(mac->env, HW_BAND, void *);

	return band ? FIELD(band, BAND_PI, struct bcm4360_phy *) : NULL;
}

/* chip_attach.c */
void *core_si_attach(struct bcm4360_core *c, u32 device, void *osh, void *regs, u32 bustype,
		     char **vars, u32 *varsz);
void core_si_detach(struct bcm4360_core *c);
void core_si_sync(struct bcm4360_core *c);
s32 core_si_pci_fixcfg(struct bcm4360_core *c);
u32 core_si_find_capability(struct bcm4360_core *c, u32 capid);
void core_si_pci_setup(struct bcm4360_core *c, u32 coremask);
void core_si_pci_up(struct bcm4360_core *c);
void core_si_pci_down(struct bcm4360_core *c);
void core_si_pci_sleep(struct bcm4360_core *c);
void core_si_pcie_ltr_war(struct bcm4360_core *c);
u32 core_si_pcieltrenable(struct bcm4360_core *c, u32 mask, u32 val);
u32 core_si_pcieobffenable(struct bcm4360_core *c, u32 mask, u32 val);
void core_si_clkctl_init(struct bcm4360_core *c);
u16 core_si_clkctl_fast_pwrup_delay(struct bcm4360_core *c);
void core_si_pmu_res_req_timer_clr(struct bcm4360_core *c);
bool core_si_is_sprom_available(struct bcm4360_core *c);
int core_otp_read_word(struct bcm4360_core *c, u32 word, u16 *data);
s32 core_getintvar(const char *vars, const char *name);
const char *core_getvar(const char *vars, const char *name);

/* env_mac.c */
void core_intrsoff_hw(struct bcm4360_core *c, u32 *old);
u32 core_intrsoff(struct bcm4360_core *c);
void core_intrsrestore(struct bcm4360_core *c, u32 mask);

/* core.c */
void *core_alloc(struct bcm4360_core *c, u32 size);

/* wlchw.c: the upper layer's steps (docs/re/spec/wlc-hw.md) */
void core_w_attach(struct bcm4360_core *c);
int core_w_up(struct bcm4360_core *c, u16 chanspec);
void core_w_down(struct bcm4360_core *c);
void core_w_detach(struct bcm4360_core *c);
int core_w_set_chanspec(struct bcm4360_core *c, u16 chanspec);
void core_w_watchdog(struct bcm4360_core *c);
void core_w_btc_period_get(struct bcm4360_core *c, u16 *period, u8 *state);
/* chanpow.c: channel-power.md */
void core_cp_attach(struct bcm4360_core *c);
int core_cp_set_country(struct bcm4360_core *c, const char *code);
void *core_cp_limits(struct bcm4360_core *c, u16 cs);	/* a ratepwr container, to delete */
void core_cp_free(struct bcm4360_core *c, void *p);
bool core_cp_quiet(struct bcm4360_core *c, u16 cs);
void core_w_stf_txchain_set(struct bcm4360_core *c, u8 mask);
void core_w_set_wake_ctrl(struct bcm4360_core *c);	/* wlc_set_wake_ctrl, after a request changed */

/* keys.c: docs/re/spec/ps-keys.md 2 */
void core_keys_reinit(struct bcm4360_core *c);		/* after the table was cleared at init */
int core_keys_tx(struct bcm4360_core *c, u32 entry, u8 *cipher, u8 *icvlen);
/* ps.c: docs/re/spec/ps-keys.md 1 */
bool core_ps_wake_req(struct bcm4360_core *c);		/* a wake request is held (wlc_stay_awake) */
void core_ps_down(struct bcm4360_core *c);
void core_ps_bss(struct bcm4360_core *c, bool joined, bool left);
void core_ps_chanspec(struct bcm4360_core *c);
void core_ps_tx_frame(struct bcm4360_core *c, const u8 *f, u32 len, u16 frame_id);
void core_ps_txstatus(struct bcm4360_core *c, u32 w0);
void core_ps_beacon(struct bcm4360_core *c, const u8 *f, u32 len, u8 rate, u8 enc, u8 flags);
void core_ps_watchdog(struct bcm4360_core *c);

/* txpwr.c: coverage.md G1 (the power field of PHY control word 1) */
u32 core_txrate_rspec(const struct bcm4360_txrate *r);
u32 core_stf_txchain_get(struct bcm4360_core *c, u32 rspec);
u32 core_stf_spatial_expansion_get(struct bcm4360_core *c, u32 rspec);
s8 core_stf_get_pwrperrate(struct bcm4360_core *c, u32 rspec, u32 e);
u16 core_txctl1_calc(struct bcm4360_core *c, u32 rspec);
/* chains.c: coverage.md G5 (changes of the transmit and receive chains) */
int core_stf_txchain_set(struct bcm4360_core *c, u8 mask, bool force, u32 id);
void core_stf_txchain_set_complete(struct bcm4360_core *c);
int core_stf_rxchain_set(struct bcm4360_core *c, u8 mask);
void core_txfifo_complete(struct bcm4360_core *c);	/* the FIFOs drained: pending changes complete */
void *core_test_block(struct bcm4360_core *c, u32 which);

#endif
