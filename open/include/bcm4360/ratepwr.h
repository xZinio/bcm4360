/* SPDX-License-Identifier: ISC */
/*
 * The per-rate transmit power container: power values (signed bytes, quarter
 * dBm, -128 = rate disabled) for every rate of one channel bandwidth, with
 * whole-container arithmetic, queries, element-wise mapping and a serialised
 * form. Pure computation: no hardware access, no global state.
 *
 * Specification: docs/re/spec/ppr.md (Broadcom's wlc_ppr.c, `ppr_t`). The
 * functions here are those of the specification with the prefix
 * bcm4360_ratepwr_ instead of ppr_, the same arguments in the same order,
 * and the same results. (The prefix bcm4360_ppr_ is taken: it names the
 * services through which the open PHY reaches a container, phy_env.h.)
 *
 * The container's bytes are those of the specification's "Data" section
 * (a 32-bit bandwidth word followed by 1, 2 or 3 sets of 182 bytes); the
 * tests compare them byte for byte. Containers are allocated with
 * osl_malloc(osh, ...) and freed with osl_mfree(osh, ..., size) of the
 * platform (hw.h), the sizes the specification gives.
 *
 * Enumerations (ppr.md, "Enumerations"): bw = wl_tx_bw_t (0 = 20, 1 = 40,
 * 2 = 80 MHz, 3 = 20 in 40, 4 = 20 in 80, 5 = 40 in 80), mode = wl_tx_mode_t
 * (0 none, 1 STBC, 2 CDD, 3 TXBF), nss 1..3, chains 1..3; all 32-bit.
 */
#ifndef BCM4360_RATEPWR_H
#define BCM4360_RATEPWR_H

#include <bcm4360/types.h>

/* a container: ppr.md "ppr_t" (opaque outside open/lib/ratepwr.c) */
struct bcm4360_ratepwr;

/* ppr.md "Callback type of the ppr_map_vec_* functions" */
typedef void (*bcm4360_ratepwr_mapfn)(void *ctx, s8 *a_elem, s8 *b_elem);

/* sizes and header */
u32 bcm4360_ratepwr_size(u32 bw);
u32 bcm4360_ratepwr_ser_size(const struct bcm4360_ratepwr *p);
u32 bcm4360_ratepwr_ser_size_by_bw(u32 bw);
u32 bcm4360_ratepwr_get_ch_bw(const struct bcm4360_ratepwr *p);
u32 bcm4360_ratepwr_get_max_bw(void);

/* life cycle */
struct bcm4360_ratepwr *bcm4360_ratepwr_create(void *osh, u32 bw);
void bcm4360_ratepwr_init(struct bcm4360_ratepwr *p, u32 bw);
void bcm4360_ratepwr_delete(void *osh, struct bcm4360_ratepwr *p);
void bcm4360_ratepwr_clear(struct bcm4360_ratepwr *p);
void bcm4360_ratepwr_set_cmn_val(struct bcm4360_ratepwr *p, s8 v);
void bcm4360_ratepwr_copy_struct(const struct bcm4360_ratepwr *src,
				 struct bcm4360_ratepwr *dst);

/* group get / set: bytes transferred, 0 if the group does not exist */
s32 bcm4360_ratepwr_get_dsss(const struct bcm4360_ratepwr *p, u32 bw, u32 chains, s8 *out);
s32 bcm4360_ratepwr_get_ofdm(const struct bcm4360_ratepwr *p, u32 bw, u32 mode, u32 chains,
			     s8 *out);
s32 bcm4360_ratepwr_get_ht_mcs(const struct bcm4360_ratepwr *p, u32 bw, u32 nss, u32 mode,
			       u32 chains, s8 *out);
s32 bcm4360_ratepwr_get_vht_mcs(const struct bcm4360_ratepwr *p, u32 bw, u32 nss, u32 mode,
				u32 chains, s8 *out);
s32 bcm4360_ratepwr_set_dsss(struct bcm4360_ratepwr *p, u32 bw, u32 chains, const s8 *in);
s32 bcm4360_ratepwr_set_ofdm(struct bcm4360_ratepwr *p, u32 bw, u32 mode, u32 chains,
			     const s8 *in);
s32 bcm4360_ratepwr_set_ht_mcs(struct bcm4360_ratepwr *p, u32 bw, u32 nss, u32 mode,
			       u32 chains, const s8 *in);
s32 bcm4360_ratepwr_set_vht_mcs(struct bcm4360_ratepwr *p, u32 bw, u32 nss, u32 mode,
				u32 chains, const s8 *in);
s32 bcm4360_ratepwr_set_same_dsss(struct bcm4360_ratepwr *p, u32 bw, u32 chains, s8 v);
s32 bcm4360_ratepwr_set_same_ofdm(struct bcm4360_ratepwr *p, u32 bw, u32 mode, u32 chains,
				  s8 v);
s32 bcm4360_ratepwr_set_same_ht_mcs(struct bcm4360_ratepwr *p, u32 bw, u32 nss, u32 mode,
				    u32 chains, s8 v);
s32 bcm4360_ratepwr_set_same_vht_mcs(struct bcm4360_ratepwr *p, u32 bw, u32 nss, u32 mode,
				     u32 chains, s8 v);
s32 bcm4360_ratepwr_get_vht_mcs_min(const struct bcm4360_ratepwr *p, u32 bw, u32 nss,
				    u32 mode, u32 chains, s8 *min);

/* whole-container arithmetic */
void bcm4360_ratepwr_apply_max(struct bcm4360_ratepwr *p, s8 v);
void bcm4360_ratepwr_apply_min(struct bcm4360_ratepwr *p, s8 v);
void bcm4360_ratepwr_plus_cmn_val(struct bcm4360_ratepwr *p, s8 v);
void bcm4360_ratepwr_minus_cmn_val(struct bcm4360_ratepwr *p, s8 v);
void bcm4360_ratepwr_cmn_val_minus(struct bcm4360_ratepwr *p, s8 v);
void bcm4360_ratepwr_multiply_percentage(struct bcm4360_ratepwr *p, u8 pct);
void bcm4360_ratepwr_compare_min(struct bcm4360_ratepwr *a, const struct bcm4360_ratepwr *b);
void bcm4360_ratepwr_compare_max(struct bcm4360_ratepwr *a, const struct bcm4360_ratepwr *b);
u32 bcm4360_ratepwr_apply_vector_ceiling(struct bcm4360_ratepwr *a,
					 const struct bcm4360_ratepwr *b);
u32 bcm4360_ratepwr_apply_vector_floor(struct bcm4360_ratepwr *a,
				       const struct bcm4360_ratepwr *b);
s32 bcm4360_ratepwr_force_disabled(struct bcm4360_ratepwr *p, s8 min);
u32 bcm4360_ratepwr_apply_constraint_total_tx(struct bcm4360_ratepwr *p, s8 v);

/* queries */
s8 bcm4360_ratepwr_get_max(const struct bcm4360_ratepwr *p);
s8 bcm4360_ratepwr_get_min(const struct bcm4360_ratepwr *p, s8 skip);
s8 bcm4360_ratepwr_get_max_for_bw(const struct bcm4360_ratepwr *p, u32 bw);
s8 bcm4360_ratepwr_get_min_for_bw(const struct bcm4360_ratepwr *p, u32 bw);

/* mapping */
void bcm4360_ratepwr_map_vec_dsss(bcm4360_ratepwr_mapfn fn, void *ctx,
				  struct bcm4360_ratepwr *a, struct bcm4360_ratepwr *b,
				  u32 bw, u32 chains);
void bcm4360_ratepwr_map_vec_ofdm(bcm4360_ratepwr_mapfn fn, void *ctx,
				  struct bcm4360_ratepwr *a, struct bcm4360_ratepwr *b,
				  u32 bw, u32 mode, u32 chains);
void bcm4360_ratepwr_map_vec_ht_mcs(bcm4360_ratepwr_mapfn fn, void *ctx,
				    struct bcm4360_ratepwr *a, struct bcm4360_ratepwr *b,
				    u32 bw, u32 nss, u32 mode, u32 chains);
void bcm4360_ratepwr_map_vec_vht_mcs(bcm4360_ratepwr_mapfn fn, void *ctx,
				     struct bcm4360_ratepwr *a, struct bcm4360_ratepwr *b,
				     u32 bw, u32 nss, u32 mode, u32 chains);
void bcm4360_ratepwr_map_vec_all(bcm4360_ratepwr_mapfn fn, void *ctx,
				 struct bcm4360_ratepwr *a, struct bcm4360_ratepwr *b);

/* serialisation */
s32 bcm4360_ratepwr_init_ser_mem_by_bw(u8 *buf, u32 bw, u32 len);
s32 bcm4360_ratepwr_init_ser_mem(u8 *buf, const struct bcm4360_ratepwr *p, u32 len);
s32 bcm4360_ratepwr_serialize(const struct bcm4360_ratepwr *p, u8 *buf, u32 len,
			      u32 *out_len);
s32 bcm4360_ratepwr_deserialize(struct bcm4360_ratepwr *p, const u8 *buf, u32 len);
s32 bcm4360_ratepwr_deserialize_create(void *osh, const u8 *buf, u32 len,
				       struct bcm4360_ratepwr **out);

#endif
