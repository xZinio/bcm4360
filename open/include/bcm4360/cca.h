/*
 * Channel statistics (Broadcom's wlc_cca.c): per channel and second, how much
 * of the air time the own and other networks and interference took, from the
 * microcode's counters; a driver module with a watchdog, a down function and
 * the iovar "cca_get_stats".
 *
 * Specification: docs/re/spec/cca-stf.md, "Procedures, part 1" and the Data
 * it refers to. The state is this code's own (struct bcm4360_cca, defined by
 * the implementation; it must be allocated with the size of the
 * specification, 0x4390 bytes). Everything of the driver around it is reached
 * through the services at the end, with the caller's handle `wlc`.
 */
#ifndef BCM4360_CCA_H
#define BCM4360_CCA_H

#include <bcm4360/types.h>
#include <bcm4360/iovar.h>

struct bcm4360_cca;

/* wlc_cca_attach: osh, unit and corerev (pub+0x14) of the caller */
struct bcm4360_cca *bcm4360_cca_attach(void *wlc, void *osh, u32 unit, u32 corerev);
/* wlc_cca_detach (cca not NULL) */
void bcm4360_cca_detach(struct bcm4360_cca *cca);
/* cca_stats_upd(wlc, calc); cca may be NULL (wlc without the module) */
void bcm4360_cca_stats_upd(struct bcm4360_cca *cca, s32 calc);
/* cca_stats_tsf_upd; cca may be NULL */
void bcm4360_cca_stats_tsf_upd(struct bcm4360_cca *cca);
/* cca_query_stats; cca may be NULL */
s32 bcm4360_cca_query_stats(struct bcm4360_cca *cca, u16 cs, s32 n, void *out, u32 size);
/* wlc_cca_get_chanspec; cca may be NULL */
u16 bcm4360_cca_get_chanspec(struct bcm4360_cca *cca, s32 i);

/*
 * The module's functions that attach registers (cca-stf.md: sub_06dd9c down,
 * sub_06e17b watchdog, sub_06e3fe iovar handler). The driver calls them with
 * the handle given at registration (the state).
 */
s32 bcm4360_cca_down(void *hdl);
s32 bcm4360_cca_watchdog(void *hdl);
s32 bcm4360_cca_doiovar(void *hdl, const struct bcm4360_iovar *vi, u32 actionid,
			const char *name, void *params, u32 p_len, void *arg, s32 len, s32 val_size,
			void *wlcif);

typedef s32 (*bcm4360_cca_fn)(void *hdl);
typedef s32 (*bcm4360_cca_iovar_fn)(void *hdl, const struct bcm4360_iovar *vi, u32 actionid,
				    const char *name, void *params, u32 p_len, void *arg,
				    s32 len, s32 val_size, void *wlcif);

/* services of the caller (cca-stf.md, Data: "Module service" and "Fields of wlc") */

/* the OS handle (wlc+0x08), for osl_malloc/osl_mfree */
void *bcm4360_cca_env_osh(void *wlc);
/* the upper layer's current chanspec (the low 16 bits of wlc+0x518) */
u16 bcm4360_cca_env_chanspec(void *wlc);
/* wlc_bmac_cca_stats_read(wlc_hw, v): the nine counter words; its result */
s32 bcm4360_cca_env_stats_read(void *wlc, u32 v[9]);
/* the power-save flag of the primary BSS configuration (Data, "Power save enabled") */
bool bcm4360_cca_env_pm_enabled(void *wlc);
/* wlc_read_tsf(wlc, &lo, &hi) */
void bcm4360_cca_env_read_tsf(void *wlc, u32 *lo, u32 *hi);
/* wlc_module_register(pub, ...) / wlc_module_unregister(pub, ...) */
s32 bcm4360_cca_env_module_register(void *wlc, const struct bcm4360_iovar *iovars,
				    const char *name, void *hdl, bcm4360_cca_iovar_fn doiovar,
				    bcm4360_cca_fn watchdog, bcm4360_cca_fn up, bcm4360_cca_fn down);
s32 bcm4360_cca_env_module_unregister(void *wlc, const char *name, void *hdl);

#endif
