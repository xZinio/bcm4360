/*
 * Hybrid build: the LEDs of the MAC layer in Broadcom's core (wlc_bmac.c:
 * wlc_bmac_led*, the blink engine and its timer function sub_061660) by the
 * open code of open/mac/bmac_led.c (docs/re/spec/bmac-run.md R4). Part of the
 * MAC layer: switch open_mac, group 0x400 of open_mac_mask.
 *
 * The LED state is a block of its own (wlc_hw+0x198), not part of the view of
 * hybrid/mac.c: the adapters pass wlc_hw through. With the group on, the
 * timer function is the open one (registered by the open wlc_bmac_led_attach).
 */
#include "hybrid.h"
#include <bcm4360/bmac_led.h>

HYB_TAKE(void *, wlc_bmac_led_attach, (void *hw));
HYB_TAKE(u8, wlc_bmac_led_detach, (void *hw));
HYB_TAKE(void, wlc_bmac_led_hw_mask_init, (void *hw, u32 mask));
HYB_TAKE(void, wlc_bmac_led, (void *hw, u32 mask, u32 val, u32 activehi));
HYB_TAKE(void, wlc_bmac_led_hw_deinit, (void *hw, u32 mask));
HYB_TAKE(void, wlc_bmac_led_set, (void *hw, u32 i, u32 activehi));
HYB_TAKE(void, wlc_bmac_blink_sync, (void *hw, u32 pins));
HYB_TAKE(s32, wlc_bmac_led_blink_event, (void *hw, u32 on));
HYB_TAKE(void, wlc_bmac_led_blink, (void *hw, u32 i, u32 on_ms, u32 off_ms));

char *getvar(char *vars, const char *name);
u32 si_gpioout(void *sih, u32 mask, u32 val, u32 priority);
u32 si_gpioouten(void *sih, u32 mask, u32 val, u32 priority);
u32 si_gpioled(void *sih, u32 mask, u32 val);
void wlc_bmac_xtal(void *wlc_hw, u32 want);
u8 wlc_hw_deviceremoved(void *wlc_hw);
/* the glue (src/wl/sys/wl_linux.c) */
void *wl_init_timer(void *wl, void (*fn)(void *arg), void *arg, const char *name);
void wl_add_timer(void *wl, void *timer, u32 ms, int periodic);
bool wl_del_timer(void *wl, void *timer);
void wl_free_timer(void *wl, void *timer);
void wl_down(void *wl);

#define ON()	hyb_mac_on(HYB_MAC_LED)

void *wlc_bmac_led_attach(void *hw)
{
	return ON() ? bcm4360_bmac_led_attach(hw) : bcm4360_blob_wlc_bmac_led_attach(hw);
}

u8 wlc_bmac_led_detach(void *hw)
{
	return ON() ? bcm4360_bmac_led_detach(hw) : bcm4360_blob_wlc_bmac_led_detach(hw);
}

void wlc_bmac_led_hw_mask_init(void *hw, u32 mask)
{
	if (ON())
		bcm4360_bmac_led_hw_mask_init(hw, mask);
	else
		bcm4360_blob_wlc_bmac_led_hw_mask_init(hw, mask);
}

void wlc_bmac_led(void *hw, u32 mask, u32 val, u32 activehi)
{
	if (ON())
		bcm4360_bmac_led(hw, mask, val, (u8)activehi);
	else
		bcm4360_blob_wlc_bmac_led(hw, mask, val, activehi);
}

void wlc_bmac_led_hw_deinit(void *hw, u32 mask)
{
	if (ON())
		bcm4360_bmac_led_hw_deinit(hw, mask);
	else
		bcm4360_blob_wlc_bmac_led_hw_deinit(hw, mask);
}

void wlc_bmac_led_set(void *hw, u32 i, u32 activehi)
{
	if (ON())
		bcm4360_bmac_led_set(hw, (s32)i, (u8)activehi);
	else
		bcm4360_blob_wlc_bmac_led_set(hw, i, activehi);
}

void wlc_bmac_blink_sync(void *hw, u32 pins)
{
	if (ON())
		bcm4360_bmac_blink_sync(hw, pins);
	else
		bcm4360_blob_wlc_bmac_blink_sync(hw, pins);
}

s32 wlc_bmac_led_blink_event(void *hw, u32 on)
{
	return ON() ? bcm4360_bmac_led_blink_event(hw, (u8)on) :
		      bcm4360_blob_wlc_bmac_led_blink_event(hw, on);
}

void wlc_bmac_led_blink(void *hw, u32 i, u32 on_ms, u32 off_ms)
{
	if (ON())
		bcm4360_bmac_led_blink(hw, (s32)i, (u16)on_ms, (u16)off_ms);
	else
		bcm4360_blob_wlc_bmac_led_blink(hw, i, on_ms, off_ms);
}

/* --- services of open/include/bcm4360/bmac_led.h --- */

const char *bcm4360_led_env_getvar(const char *vars, const char *name)
{
	return getvar((char *)vars, name);
}

void *bcm4360_led_env_init_timer(void *wl, void (*fn)(void *arg), void *arg, const char *name)
{
	return wl_init_timer(wl, fn, arg, name);
}

void bcm4360_led_env_add_timer(void *wl, void *timer, u32 ms, bool periodic)
{
	wl_add_timer(wl, timer, ms, periodic);
}

bool bcm4360_led_env_del_timer(void *wl, void *timer) { return wl_del_timer(wl, timer); }
void bcm4360_led_env_free_timer(void *wl, void *timer) { wl_free_timer(wl, timer); }
void bcm4360_led_env_wl_down(void *wl) { wl_down(wl); }
u32 bcm4360_led_env_gpioout(void *sih, u32 mask, u32 val) { return si_gpioout(sih, mask, val, 0); }
u32 bcm4360_led_env_gpioouten(void *sih, u32 mask, u32 val)
{
	return si_gpioouten(sih, mask, val, 0);
}
u32 bcm4360_led_env_gpioled(void *sih, u32 mask, u32 val) { return si_gpioled(sih, mask, val); }
void bcm4360_led_env_xtal(void *hw, bool want) { wlc_bmac_xtal(hw, want); }
bool bcm4360_led_env_deviceremoved(void *hw) { return wlc_hw_deviceremoved(hw) != 0; }
