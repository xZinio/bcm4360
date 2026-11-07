/*
 * The LEDs of the hardware MAC layer: polarity and blink times per GPIO pin,
 * the GPIO writes, the blink engine (docs/re/spec/bmac-run.md R4). The state
 * is the specification's 0x328-byte block at wlc_hw+0x198.
 */
#ifndef BCM4360_BMAC_LED_H
#define BCM4360_BMAC_LED_H

#include <bcm4360/types.h>

void *bcm4360_bmac_led_attach(void *hw);				/* wlc_bmac_led_attach */
bool bcm4360_bmac_led_detach(void *hw);					/* wlc_bmac_led_detach */
void bcm4360_bmac_led_hw_mask_init(void *hw, u32 mask);		/* wlc_bmac_led_hw_mask_init */
void bcm4360_bmac_led(void *hw, u32 mask, u32 val, u8 activehi);	/* wlc_bmac_led */
void bcm4360_bmac_led_hw_deinit(void *hw, u32 mask);			/* wlc_bmac_led_hw_deinit */
void bcm4360_bmac_led_set(void *hw, s32 i, u8 activehi);		/* wlc_bmac_led_set */
void bcm4360_bmac_blink_sync(void *hw, u32 pins);			/* wlc_bmac_blink_sync */
s32 bcm4360_bmac_led_blink_event(void *hw, u8 on);			/* wlc_bmac_led_blink_event */
void bcm4360_bmac_led_blink(void *hw, s32 i, u16 on_ms, u16 off_ms);	/* wlc_bmac_led_blink */
/* sub_061660 (name assigned wlc_bmac_led_timer): the timer function, arg wlc */
void bcm4360_bmac_led_timer(void *wlc);

/* services of the platform */
const char *bcm4360_led_env_getvar(const char *vars, const char *name);	/* getvar */
void *bcm4360_led_env_init_timer(void *wl, void (*fn)(void *arg), void *arg,
				 const char *name);			/* wl_init_timer */
void bcm4360_led_env_add_timer(void *wl, void *timer, u32 ms, bool periodic);	/* wl_add_timer */
bool bcm4360_led_env_del_timer(void *wl, void *timer);		/* wl_del_timer */
void bcm4360_led_env_free_timer(void *wl, void *timer);		/* wl_free_timer */
void bcm4360_led_env_wl_down(void *wl);					/* wl_down */
u32 bcm4360_led_env_gpioout(void *sih, u32 mask, u32 val);		/* si_gpioout(.., 0) */
u32 bcm4360_led_env_gpioouten(void *sih, u32 mask, u32 val);		/* si_gpioouten(.., 0) */
u32 bcm4360_led_env_gpioled(void *sih, u32 mask, u32 val);		/* si_gpioled */
void bcm4360_led_env_xtal(void *hw, bool want);				/* wlc_bmac_xtal */
bool bcm4360_led_env_deviceremoved(void *hw);				/* wlc_hw_deviceremoved */

#endif
