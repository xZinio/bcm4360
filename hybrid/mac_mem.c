// SPDX-License-Identifier: ISC
/*
 * Hybrid build: the object memories of the MAC core in Broadcom's core
 * (wlc_bmac.c: shared memory fill, copies to and from the object memories,
 * the address match table, the initialisation lists, template RAM, internal
 * registers) and the state setters that write through them, by the open code
 * of open/mac/bmac_mem.c (docs/re/spec/bmac-init.md A8, B4-B6, B11, E1, E3,
 * E7, F1, F2; task docs/re/tasks/bmac-mem.md).
 *
 * Part of the MAC layer: switch open_mac. The state goes in and out as for
 * the control primitives (hyb_mac_in/hyb_mac_out, hybrid/mac.c); the open
 * functions call no other layer of the object except the offload semaphore,
 * which does not touch `wlc_hw`.
 *
 * Integer arguments narrower than 32 bit arrive as u32 and are truncated.
 */
#include "hybrid.h"
#include <bcm4360/mac.h>

HYB_TAKE(void, wlc_bmac_set_shm, (void *wlc_hw, u32 offset, u32 v, s32 len));
HYB_TAKE(void, wlc_bmac_copyto_objmem, (void *wlc_hw, u32 offset, const void *buf, s32 len,
					u32 sel));
HYB_TAKE(void, wlc_bmac_copyfrom_objmem, (void *wlc_hw, u32 offset, void *buf, s32 len,
					  u32 sel));
HYB_TAKE(void, wlc_bmac_write_amt, (void *wlc_hw, s32 idx, const u8 *addr, u32 attr));
HYB_TAKE(void, wlc_bmac_write_inits, (void *wlc_hw, const void *inits));
HYB_TAKE(void, wlc_bmac_set_cwmin, (void *wlc_hw, u32 v));
HYB_TAKE(void, wlc_bmac_set_cwmax, (void *wlc_hw, u32 v));
HYB_TAKE(void, wlc_bmac_set_hw_etheraddr, (void *wlc_hw, const u8 *addr));
HYB_TAKE(void, wlc_bmac_hw_etheraddr, (void *wlc_hw, u8 *out));
HYB_TAKE(void, wlc_bmac_set_rcmta, (void *wlc_hw, s32 idx, const u8 *addr));
HYB_TAKE(void, wlc_bmac_retrylimit_upd, (void *wlc_hw, u32 srl, u32 lrl));
HYB_TAKE(void, wlc_bmac_write_ihr, (void *wlc_hw, u32 reg, u32 v));
HYB_TAKE(void, wlc_bmac_write_template_ram, (void *wlc_hw, s32 offset, s32 len,
					     const void *buf));

/*
 * ADAPT(name, (args of the object), (arguments of the open call after mac),
 * (arguments of Broadcom's body)): the open function bcm4360_bmac_<name>
 * on the state of `wlc_hw`, or Broadcom's wlc_bmac_<name> with open_mac off.
 */
#define ADAPT(name, params, open_args, blob_args)			\
void wlc_bmac_##name params						\
{									\
	struct bcm4360_hw hw;						\
	struct bcm4360_mac mac, mac0;						\
									\
	if (!hyb_mac_on(HYB_MAC_MEM)) {					\
		bcm4360_blob_wlc_bmac_##name blob_args;			\
		return;							\
	}								\
	hyb_mac_in(wlc_hw, &hw, &mac, &mac0);					\
	bcm4360_bmac_##name open_args;					\
	hyb_mac_out(wlc_hw, &mac, &mac0);					\
}

/* A8 */
ADAPT(set_shm, (void *wlc_hw, u32 offset, u32 v, s32 len),
      (&mac, offset, (u16)v, len), (wlc_hw, offset, v, len))
/* B4 */
ADAPT(copyto_objmem, (void *wlc_hw, u32 offset, const void *buf, s32 len, u32 sel),
      (&mac, offset, buf, len, sel), (wlc_hw, offset, buf, len, sel))
ADAPT(copyfrom_objmem, (void *wlc_hw, u32 offset, void *buf, s32 len, u32 sel),
      (&mac, offset, buf, len, sel), (wlc_hw, offset, buf, len, sel))
/* B5 */
ADAPT(write_amt, (void *wlc_hw, s32 idx, const u8 *addr, u32 attr),
      (&mac, idx, addr, (u16)attr), (wlc_hw, idx, addr, attr))
/* B6: the list is Broadcom's data (an initial-values table), passed through */
ADAPT(write_inits, (void *wlc_hw, const void *inits),
      (&mac, inits), (wlc_hw, inits))
/* B11 */
ADAPT(set_cwmin, (void *wlc_hw, u32 v), (&mac, (u16)v), (wlc_hw, v))
ADAPT(set_cwmax, (void *wlc_hw, u32 v), (&mac, (u16)v), (wlc_hw, v))
/* E1 */
ADAPT(set_hw_etheraddr, (void *wlc_hw, const u8 *addr), (&mac, addr), (wlc_hw, addr))
ADAPT(hw_etheraddr, (void *wlc_hw, u8 *out), (&mac, out), (wlc_hw, out))
/* E3 */
ADAPT(set_rcmta, (void *wlc_hw, s32 idx, const u8 *addr),
      (&mac, idx, addr), (wlc_hw, idx, addr))
/* E7 */
ADAPT(retrylimit_upd, (void *wlc_hw, u32 srl, u32 lrl),
      (&mac, (u16)srl, (u16)lrl), (wlc_hw, srl, lrl))
/* F1 */
ADAPT(write_ihr, (void *wlc_hw, u32 reg, u32 v), (&mac, reg, (u16)v), (wlc_hw, reg, v))
/* F2 */
ADAPT(write_template_ram, (void *wlc_hw, s32 offset, s32 len, const void *buf),
      (&mac, offset, len, buf), (wlc_hw, offset, len, buf))
