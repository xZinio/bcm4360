// SPDX-License-Identifier: ISC
/*
 * Hybrid build: Broadcom's per-rate transmit power container (wlc_ppr.c, the
 * 50 named ppr_* functions) by the open code of open/lib/ratepwr.c
 * (docs/re/spec/ppr.md). Switch open_ppr.
 *
 * Pure pass-through: the container is private to wlc_ppr.c (ppr.md, "Who
 * looks inside a ppr_t"), the open code uses the same bytes, so a container
 * is handed over as it is. The open PHY's ppr services (hybrid/phy.c) call
 * these names too and reach the same code. With open_ppr off Broadcom's own
 * functions run (bcm4360_blob_*).
 *
 * Integer arguments narrower than 32 bit (int8, uint8) arrive as u32 and are
 * truncated here.
 */
#include "hybrid.h"
#include <bcm4360/ratepwr.h>

#ifdef __KERNEL__
#include <linux/module.h>
#endif

bool bcm4360_open_ppr = true;

#ifdef __KERNEL__
module_param_named(open_ppr, bcm4360_open_ppr, bool, 0444);
MODULE_PARM_DESC(open_ppr, "per-rate transmit power container: open code (1, default) or Broadcom's (0)");
#endif

typedef struct bcm4360_ratepwr P;
typedef bcm4360_ratepwr_mapfn F;

HYB_TAKE(u32, ppr_size, (u32 bw));
HYB_TAKE(u32, ppr_ser_size, (const P *p));
HYB_TAKE(u32, ppr_ser_size_by_bw, (u32 bw));
HYB_TAKE(u32, ppr_get_ch_bw, (const P *p));
HYB_TAKE(u32, ppr_get_max_bw, (void));
HYB_TAKE(P *, ppr_create, (void *osh, u32 bw));
HYB_TAKE(void, ppr_init, (P *p, u32 bw));
HYB_TAKE(void, ppr_delete, (void *osh, P *p));
HYB_TAKE(void, ppr_clear, (P *p));
HYB_TAKE(void, ppr_set_cmn_val, (P *p, u32 v));
HYB_TAKE(void, ppr_copy_struct, (const P *src, P *dst));
HYB_TAKE(s32, ppr_get_dsss, (const P *p, u32 bw, u32 chains, s8 *out));
HYB_TAKE(s32, ppr_get_ofdm, (const P *p, u32 bw, u32 mode, u32 chains, s8 *out));
HYB_TAKE(s32, ppr_get_ht_mcs, (const P *p, u32 bw, u32 nss, u32 mode, u32 chains, s8 *out));
HYB_TAKE(s32, ppr_get_vht_mcs, (const P *p, u32 bw, u32 nss, u32 mode, u32 chains, s8 *out));
HYB_TAKE(s32, ppr_set_dsss, (P *p, u32 bw, u32 chains, const s8 *in));
HYB_TAKE(s32, ppr_set_ofdm, (P *p, u32 bw, u32 mode, u32 chains, const s8 *in));
HYB_TAKE(s32, ppr_set_ht_mcs, (P *p, u32 bw, u32 nss, u32 mode, u32 chains, const s8 *in));
HYB_TAKE(s32, ppr_set_vht_mcs, (P *p, u32 bw, u32 nss, u32 mode, u32 chains, const s8 *in));
HYB_TAKE(s32, ppr_set_same_dsss, (P *p, u32 bw, u32 chains, u32 v));
HYB_TAKE(s32, ppr_set_same_ofdm, (P *p, u32 bw, u32 mode, u32 chains, u32 v));
HYB_TAKE(s32, ppr_set_same_ht_mcs, (P *p, u32 bw, u32 nss, u32 mode, u32 chains, u32 v));
HYB_TAKE(s32, ppr_set_same_vht_mcs, (P *p, u32 bw, u32 nss, u32 mode, u32 chains, u32 v));
HYB_TAKE(s32, ppr_get_vht_mcs_min, (const P *p, u32 bw, u32 nss, u32 mode, u32 chains,
				    s8 *min));
HYB_TAKE(void, ppr_apply_max, (P *p, u32 v));
HYB_TAKE(void, ppr_apply_min, (P *p, u32 v));
HYB_TAKE(void, ppr_plus_cmn_val, (P *p, u32 v));
HYB_TAKE(void, ppr_minus_cmn_val, (P *p, u32 v));
HYB_TAKE(void, ppr_cmn_val_minus, (P *p, u32 v));
HYB_TAKE(void, ppr_multiply_percentage, (P *p, u32 pct));
HYB_TAKE(void, ppr_compare_min, (P *a, const P *b));
HYB_TAKE(void, ppr_compare_max, (P *a, const P *b));
HYB_TAKE(u32, ppr_apply_vector_ceiling, (P *a, const P *b));
HYB_TAKE(u32, ppr_apply_vector_floor, (P *a, const P *b));
HYB_TAKE(s32, ppr_force_disabled, (P *p, u32 min));
HYB_TAKE(u32, ppr_apply_constraint_total_tx, (P *p, u32 v));
HYB_TAKE(s8, ppr_get_max, (const P *p));
HYB_TAKE(s8, ppr_get_min, (const P *p, u32 skip));
HYB_TAKE(s8, ppr_get_max_for_bw, (const P *p, u32 bw));
HYB_TAKE(s8, ppr_get_min_for_bw, (const P *p, u32 bw));
HYB_TAKE(void, ppr_map_vec_dsss, (F fn, void *ctx, P *a, P *b, u32 bw, u32 chains));
HYB_TAKE(void, ppr_map_vec_ofdm, (F fn, void *ctx, P *a, P *b, u32 bw, u32 mode, u32 chains));
HYB_TAKE(void, ppr_map_vec_ht_mcs, (F fn, void *ctx, P *a, P *b, u32 bw, u32 nss, u32 mode,
				   u32 chains));
HYB_TAKE(void, ppr_map_vec_vht_mcs, (F fn, void *ctx, P *a, P *b, u32 bw, u32 nss, u32 mode,
				    u32 chains));
HYB_TAKE(void, ppr_map_vec_all, (F fn, void *ctx, P *a, P *b));
HYB_TAKE(s32, ppr_init_ser_mem_by_bw, (u8 *buf, u32 bw, u32 len));
HYB_TAKE(s32, ppr_init_ser_mem, (u8 *buf, const P *p, u32 len));
HYB_TAKE(s32, ppr_serialize, (const P *p, u8 *buf, u32 len, u32 *out_len));
HYB_TAKE(s32, ppr_deserialize, (P *p, const u8 *buf, u32 len));
HYB_TAKE(s32, ppr_deserialize_create, (void *osh, const u8 *buf, u32 len, P **out));

/* R(name, (args)): result of the open function or of Broadcom's, by the switch */
#define R(name, args)							\
	(bcm4360_open_ppr ? bcm4360_ratepwr_##name args : bcm4360_blob_ppr_##name args)
/* V(name, (args)): the same for a function without result */
#define V(name, args)							\
	do {								\
		if (bcm4360_open_ppr)					\
			bcm4360_ratepwr_##name args;			\
		else							\
			bcm4360_blob_ppr_##name args;			\
	} while (0)

/* sizes and header */
u32 ppr_size(u32 bw) { return R(size, (bw)); }
u32 ppr_ser_size(const P *p) { return R(ser_size, (p)); }
u32 ppr_ser_size_by_bw(u32 bw) { return R(ser_size_by_bw, (bw)); }
u32 ppr_get_ch_bw(const P *p) { return R(get_ch_bw, (p)); }
u32 ppr_get_max_bw(void) { return R(get_max_bw, ()); }

/* life cycle */
P *ppr_create(void *osh, u32 bw) { return R(create, (osh, bw)); }
void ppr_init(P *p, u32 bw) { V(init, (p, bw)); }
void ppr_delete(void *osh, P *p) { V(delete, (osh, p)); }
void ppr_clear(P *p) { V(clear, (p)); }
void ppr_copy_struct(const P *src, P *dst) { V(copy_struct, (src, dst)); }

void ppr_set_cmn_val(P *p, u32 v)
{
	if (bcm4360_open_ppr)
		bcm4360_ratepwr_set_cmn_val(p, (s8)v);
	else
		bcm4360_blob_ppr_set_cmn_val(p, v);
}

/* group get / set */
s32 ppr_get_dsss(const P *p, u32 bw, u32 chains, s8 *out)
{
	return R(get_dsss, (p, bw, chains, out));
}

s32 ppr_get_ofdm(const P *p, u32 bw, u32 mode, u32 chains, s8 *out)
{
	return R(get_ofdm, (p, bw, mode, chains, out));
}

s32 ppr_get_ht_mcs(const P *p, u32 bw, u32 nss, u32 mode, u32 chains, s8 *out)
{
	return R(get_ht_mcs, (p, bw, nss, mode, chains, out));
}

s32 ppr_get_vht_mcs(const P *p, u32 bw, u32 nss, u32 mode, u32 chains, s8 *out)
{
	return R(get_vht_mcs, (p, bw, nss, mode, chains, out));
}

s32 ppr_set_dsss(P *p, u32 bw, u32 chains, const s8 *in)
{
	return R(set_dsss, (p, bw, chains, in));
}

s32 ppr_set_ofdm(P *p, u32 bw, u32 mode, u32 chains, const s8 *in)
{
	return R(set_ofdm, (p, bw, mode, chains, in));
}

s32 ppr_set_ht_mcs(P *p, u32 bw, u32 nss, u32 mode, u32 chains, const s8 *in)
{
	return R(set_ht_mcs, (p, bw, nss, mode, chains, in));
}

s32 ppr_set_vht_mcs(P *p, u32 bw, u32 nss, u32 mode, u32 chains, const s8 *in)
{
	return R(set_vht_mcs, (p, bw, nss, mode, chains, in));
}

s32 ppr_set_same_dsss(P *p, u32 bw, u32 chains, u32 v)
{
	if (bcm4360_open_ppr)
		return bcm4360_ratepwr_set_same_dsss(p, bw, chains, (s8)v);
	return bcm4360_blob_ppr_set_same_dsss(p, bw, chains, v);
}

s32 ppr_set_same_ofdm(P *p, u32 bw, u32 mode, u32 chains, u32 v)
{
	if (bcm4360_open_ppr)
		return bcm4360_ratepwr_set_same_ofdm(p, bw, mode, chains, (s8)v);
	return bcm4360_blob_ppr_set_same_ofdm(p, bw, mode, chains, v);
}

s32 ppr_set_same_ht_mcs(P *p, u32 bw, u32 nss, u32 mode, u32 chains, u32 v)
{
	if (bcm4360_open_ppr)
		return bcm4360_ratepwr_set_same_ht_mcs(p, bw, nss, mode, chains, (s8)v);
	return bcm4360_blob_ppr_set_same_ht_mcs(p, bw, nss, mode, chains, v);
}

s32 ppr_set_same_vht_mcs(P *p, u32 bw, u32 nss, u32 mode, u32 chains, u32 v)
{
	if (bcm4360_open_ppr)
		return bcm4360_ratepwr_set_same_vht_mcs(p, bw, nss, mode, chains, (s8)v);
	return bcm4360_blob_ppr_set_same_vht_mcs(p, bw, nss, mode, chains, v);
}

s32 ppr_get_vht_mcs_min(const P *p, u32 bw, u32 nss, u32 mode, u32 chains, s8 *min)
{
	return R(get_vht_mcs_min, (p, bw, nss, mode, chains, min));
}

/* whole-container arithmetic: the s8/u8 argument truncated */
#define ARITH(name, cast)						\
void ppr_##name(P *p, u32 v)						\
{									\
	if (bcm4360_open_ppr)						\
		bcm4360_ratepwr_##name(p, (cast)v);			\
	else								\
		bcm4360_blob_ppr_##name(p, v);				\
}
ARITH(apply_max, s8)
ARITH(apply_min, s8)
ARITH(plus_cmn_val, s8)
ARITH(minus_cmn_val, s8)
ARITH(cmn_val_minus, s8)
ARITH(multiply_percentage, u8)

void ppr_compare_min(P *a, const P *b) { V(compare_min, (a, b)); }
void ppr_compare_max(P *a, const P *b) { V(compare_max, (a, b)); }
u32 ppr_apply_vector_ceiling(P *a, const P *b) { return R(apply_vector_ceiling, (a, b)); }
u32 ppr_apply_vector_floor(P *a, const P *b) { return R(apply_vector_floor, (a, b)); }

s32 ppr_force_disabled(P *p, u32 min)
{
	if (bcm4360_open_ppr)
		return bcm4360_ratepwr_force_disabled(p, (s8)min);
	return bcm4360_blob_ppr_force_disabled(p, min);
}

u32 ppr_apply_constraint_total_tx(P *p, u32 v)
{
	if (bcm4360_open_ppr)
		return bcm4360_ratepwr_apply_constraint_total_tx(p, (s8)v);
	return bcm4360_blob_ppr_apply_constraint_total_tx(p, v);
}

/* queries */
s8 ppr_get_max(const P *p) { return R(get_max, (p)); }

s8 ppr_get_min(const P *p, u32 skip)
{
	if (bcm4360_open_ppr)
		return bcm4360_ratepwr_get_min(p, (s8)skip);
	return bcm4360_blob_ppr_get_min(p, skip);
}

s8 ppr_get_max_for_bw(const P *p, u32 bw) { return R(get_max_for_bw, (p, bw)); }
s8 ppr_get_min_for_bw(const P *p, u32 bw) { return R(get_min_for_bw, (p, bw)); }

/* mapping */
void ppr_map_vec_dsss(F fn, void *ctx, P *a, P *b, u32 bw, u32 chains)
{
	V(map_vec_dsss, (fn, ctx, a, b, bw, chains));
}

void ppr_map_vec_ofdm(F fn, void *ctx, P *a, P *b, u32 bw, u32 mode, u32 chains)
{
	V(map_vec_ofdm, (fn, ctx, a, b, bw, mode, chains));
}

void ppr_map_vec_ht_mcs(F fn, void *ctx, P *a, P *b, u32 bw, u32 nss, u32 mode, u32 chains)
{
	V(map_vec_ht_mcs, (fn, ctx, a, b, bw, nss, mode, chains));
}

void ppr_map_vec_vht_mcs(F fn, void *ctx, P *a, P *b, u32 bw, u32 nss, u32 mode, u32 chains)
{
	V(map_vec_vht_mcs, (fn, ctx, a, b, bw, nss, mode, chains));
}

void ppr_map_vec_all(F fn, void *ctx, P *a, P *b) { V(map_vec_all, (fn, ctx, a, b)); }

/* serialisation */
s32 ppr_init_ser_mem_by_bw(u8 *buf, u32 bw, u32 len)
{
	return R(init_ser_mem_by_bw, (buf, bw, len));
}

s32 ppr_init_ser_mem(u8 *buf, const P *p, u32 len) { return R(init_ser_mem, (buf, p, len)); }

s32 ppr_serialize(const P *p, u8 *buf, u32 len, u32 *out_len)
{
	return R(serialize, (p, buf, len, out_len));
}

s32 ppr_deserialize(P *p, const u8 *buf, u32 len) { return R(deserialize, (p, buf, len)); }

s32 ppr_deserialize_create(void *osh, const u8 *buf, u32 len, P **out)
{
	return R(deserialize_create, (osh, buf, len, out));
}
