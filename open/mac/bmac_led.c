/*
 * The LEDs of the hardware MAC layer (docs/re/spec/bmac-run.md R4): per GPIO
 * pin the polarity and the blink times, the GPIO writes, and the blink engine
 * on a timer of the platform. Which LED shows what is the upper layer's
 * business (wlc_led.c); this layer only drives the pins.
 *
 * The LED state is the 0x328-byte block of the specification (its address in
 * wlc_hw+0x198), in that layout, because Broadcom's upper layer reaches it
 * through these functions only but the attach and detach of either
 * implementation may be the other's.
 */
#include <bcm4360/bmac_led.h>
#include <bcm4360/utils.h>
#include <bcm4360/hw.h>

#define P(b, off)	(*(void **)((u8 *)(b) + (off)))
#define U8(b, off)	(*(u8 *)((u8 *)(b) + (off)))
#define U16(b, off)	(*(u16 *)((u8 *)(b) + (off)))
#define U32(b, off)	(*(u32 *)((u8 *)(b) + (off)))

/* wlc_hw */
#define HW_WLC			0x000
#define HW_OSH			0x010
#define HW_BOARDFLAGS2		0x090
#define HW_SIH			0x0b8
#define HW_VARS			0x0c0
#define HW_LED_GPIO_MASK	0x180
#define HW_SBCLK		0x187
#define HW_LED			0x198
#define WLC_WL			0x010
#define WLC_HW			0x020

/* the LED state */
#define L_SIZE			0x328
#define L_HW			0x000
#define L_ENTRY(i)		(0x008 + 24 * (i))
#define E_PIN			0x00	/* u32 */
#define E_ACTIVEHI		0x04	/* u8 */
#define E_ON_MS			0x08	/* u32 */
#define E_OFF_MS		0x0c	/* u32 */
#define E_STAMP			0x10	/* u32 */
#define E_PHASE			0x14	/* u8: 1 = off phase or not started */
#define E_RESTART		0x15	/* u8 */
#define L_GPIO_VAL		0x308
#define L_GPIO_MASK		0x30c
#define L_PERIOD		0x310
#define L_TIMER			0x318
#define L_ARMED			0x320
#define L_ONESHOT		0x321

#define NPINS			32
#define BFL2_LED_OUTEN		0x800	/* boardflags2: LEDs driven by the output enable */
#define LED_BEHAVIOUR_MAX	0x18
#define LED_ACTIVE_LOW		0x80
#define PERIOD_MIN		10
#define PERIOD_IDLE		1000

static void *led_of(void *hw)
{
	return P(hw, HW_LED);
}

static void *entry(void *l, u32 i)
{
	return (u8 *)l + L_ENTRY(i);
}

static bool blinking(void *e)
{
	return U32(e, E_ON_MS) || U32(e, E_OFF_MS);
}

static void *wl_of(void *hw)
{
	return P(P(hw, HW_WLC), WLC_WL);
}

/* "ledbh<i>", "wl0gpio<i>" */
static void pin_name(char *buf, const char *prefix, u32 i)
{
	int n = 0;

	while (*prefix)
		buf[n++] = *prefix++;
	if (i >= 10)
		buf[n++] = (char)('0' + i / 10);
	buf[n++] = (char)('0' + i % 10);
	buf[n] = 0;
}

void *bcm4360_bmac_led_attach(void *hw)
{
	void *l = osl_malloc(P(hw, HW_OSH), L_SIZE);
	char name[16];
	const char *v;
	u32 i, b;

	if (!l)
		return NULL;
	osl_memset(l, 0, L_SIZE);
	for (i = 0; i < NPINS; i++) {
		void *e = entry(l, i);

		U32(e, E_PIN) = i;
		U8(e, E_ACTIVEHI) = 1;
		U32(e, E_STAMP) = osl_sysuptime();
		U8(e, E_PHASE) = 1;
	}
	for (i = 0; i < NPINS; i++) {
		pin_name(name, "ledbh", i);
		v = bcm4360_led_env_getvar(P(hw, HW_VARS), name);
		if (!v) {
			pin_name(name, "wl0gpio", i);
			v = bcm4360_led_env_getvar(P(hw, HW_VARS), name);
		}
		if (!v)
			continue;
		b = (u32)bcm4360_strtoul(v, NULL, 0);
		if ((b & 0x7f) <= LED_BEHAVIOUR_MAX) {
			U32(entry(l, i), E_PIN) = i;
			U8(entry(l, i), E_ACTIVEHI) = (b & LED_ACTIVE_LOW) ? 0 : 1;
		}
	}
	P(l, L_HW) = hw;
	P(l, L_TIMER) = bcm4360_led_env_init_timer(wl_of(hw), bcm4360_bmac_led_timer,
						   P(hw, HW_WLC), "led_blink");
	if (!P(l, L_TIMER)) {
		osl_mfree(P(hw, HW_OSH), l, L_SIZE);
		return NULL;
	}
	return l;
}

bool bcm4360_bmac_led_detach(void *hw)
{
	void *l = led_of(hw);
	bool r = false;

	if (!l)
		return false;
	if (P(l, L_TIMER)) {
		r = !bcm4360_led_env_del_timer(wl_of(hw), P(l, L_TIMER));
		bcm4360_led_env_free_timer(wl_of(hw), P(l, L_TIMER));
		P(l, L_TIMER) = NULL;
	}
	osl_mfree(P(hw, HW_OSH), l, L_SIZE);
	return r;
}

void bcm4360_bmac_led_hw_mask_init(void *hw, u32 mask)
{
	U32(hw, HW_LED_GPIO_MASK) = mask;
}

void bcm4360_bmac_led(void *hw, u32 mask, u32 val, u8 activehi)
{
	void *l = led_of(hw);
	void *sih = P(hw, HW_SIH);
	u32 orig = val, r;

	if (!U8(hw, HW_SBCLK))
		return;
	if (!activehi)
		val = ~val & mask;
	if (!(U32(hw, HW_BOARDFLAGS2) & BFL2_LED_OUTEN)) {
		if (!((U32(l, L_GPIO_VAL) ^ val) & mask))
			return;
		r = bcm4360_led_env_gpioout(sih, mask, val);
	} else {
		bool changes = ((val ^ U32(l, L_GPIO_VAL)) & mask) != 0;

		if (!activehi && changes)
			return;
		if (activehi && !changes)
			return;
		r = bcm4360_led_env_gpioouten(sih, mask, orig == mask ? mask : 0);
	}
	U32(l, L_GPIO_VAL) = r;
}

void bcm4360_bmac_led_hw_deinit(void *hw, u32 mask)
{
	bool xtal = !U8(hw, HW_SBCLK);
	void *sih;

	if (xtal)
		bcm4360_led_env_xtal(hw, true);
	sih = P(hw, HW_SIH);
	if (sih) {
		bcm4360_led_env_gpioout(sih, mask, 0);
		bcm4360_led_env_gpioouten(sih, mask, 0);
		bcm4360_led_env_gpioled(sih, mask, 0);
	}
	if (xtal)
		bcm4360_led_env_xtal(hw, false);
}

void bcm4360_bmac_led_set(void *hw, s32 i, u8 activehi)
{
	U8(entry(led_of(hw), (u32)i), E_ACTIVEHI) = activehi;
}

void bcm4360_bmac_blink_sync(void *hw, u32 pins)
{
	void *l = led_of(hw);
	u32 i;

	for (i = 0; i < NPINS; i++)
		if (pins & (1u << i))
			U8(entry(l, i), E_RESTART) = 1;
}

static u32 gcd(u32 a, u32 b)
{
	while (b) {
		u32 t = a % b;

		a = b;
		b = t;
	}
	return a;
}

s32 bcm4360_bmac_led_blink_event(void *hw, u8 on)
{
	void *l = led_of(hw), *wl = wl_of(hw);
	u32 i;

	if (on) {
		bcm4360_led_env_del_timer(wl, P(l, L_TIMER));
		bcm4360_led_env_add_timer(wl, P(l, L_TIMER), U32(l, L_PERIOD), true);
		U8(l, L_ARMED) = 1;
		return 0;
	}
	if (!bcm4360_led_env_del_timer(wl, P(l, L_TIMER)))
		return 1;
	U8(l, L_ARMED) = 0;
	for (i = 0; i < NPINS; i++) {
		void *e = entry(l, i);

		if (!blinking(e))
			continue;
		bcm4360_bmac_led(hw, 1u << U32(e, E_PIN), 0, U8(e, E_ACTIVEHI));
		U8(e, E_RESTART) = 1;
	}
	return 0;
}

void bcm4360_bmac_led_blink(void *hw, s32 i, u16 on_ms, u16 off_ms)
{
	void *l = led_of(hw);
	u32 k, p = 0;
	bool any = false;

	U32(entry(l, (u32)i), E_ON_MS) = on_ms;
	U32(entry(l, (u32)i), E_OFF_MS) = off_ms;
	for (k = 0; k < NPINS; k++) {
		void *e = entry(l, k);

		if (!blinking(e)) {
			U8(e, E_RESTART) = 1;
			continue;
		}
		p = gcd(p, gcd(U32(e, E_ON_MS), U32(e, E_OFF_MS)));
		any = true;
	}
	if (!any)
		p = PERIOD_IDLE;
	U32(l, L_PERIOD) = p > PERIOD_MIN ? p : PERIOD_MIN;
	if (any && U8(l, L_ARMED) && !U8(l, L_ONESHOT)) {
		bcm4360_bmac_led_blink_event(hw, 0);
		bcm4360_bmac_led_blink_event(hw, 1);
	}
}

static void turn(void *hw, void *e, bool on, u32 now)
{
	u32 bit = 1u << U32(e, E_PIN);

	bcm4360_bmac_led(hw, bit, on ? bit : 0, U8(e, E_ACTIVEHI));
	U8(e, E_PHASE) = on ? 0 : 1;
	U32(e, E_STAMP) = now;
	if (on)
		U8(e, E_RESTART) = 0;
}

/* sub_061660 (name assigned wlc_bmac_led_timer): the blink timer */
void bcm4360_bmac_led_timer(void *wlc)
{
	void *hw = P(wlc, WLC_HW), *l, *wl;
	u32 now, next = 0, i, period;

	if (bcm4360_led_env_deviceremoved(hw)) {
		bcm4360_led_env_wl_down(P(wlc, WLC_WL));
		return;
	}
	l = led_of(hw);
	wl = wl_of(hw);
	now = osl_sysuptime();
	period = U32(l, L_PERIOD);
	for (i = 0; i < NPINS; i++) {
		void *e = entry(l, i);
		u32 el, t, div, rem;
		bool on_now = false;

		if (!blinking(e))
			continue;
		el = now - U32(e, E_STAMP);
		if (U16(e, E_PHASE)) {
			/* off phase, or restart: the off time */
			t = U32(e, E_OFF_MS);
			div = t <= 1000 ? 10 : 20;
			if (el > t) {
				on_now = true;
			} else {
				rem = t - el;
				if (rem < period) {
					if (rem < t / div || rem <= 9) {
						if (period - rem > period / 10)
							on_now = true;
					} else if (!next || rem < next) {
						next = rem;
					}
				}
			}
			if (!on_now && U8(e, E_RESTART))
				on_now = true;
			if (on_now)
				turn(hw, e, true, now);
		} else {
			/* on phase: the on time */
			t = U32(e, E_ON_MS);
			div = t <= 1000 ? 10 : 20;
			if (el > t) {
				turn(hw, e, false, now);
			} else {
				rem = t - el;
				if (rem < period) {
					if (rem < t / div || rem <= 9) {
						if (period - rem > period / 10)
							turn(hw, e, false, now);
					} else if (!next || rem < next) {
						next = rem;
					}
				}
			}
		}
	}
	if (next) {
		bcm4360_led_env_del_timer(wl, P(l, L_TIMER));
		bcm4360_led_env_add_timer(wl, P(l, L_TIMER), next, false);
		U8(l, L_ARMED) = 1;
		U8(l, L_ONESHOT) = 1;
	} else if (U8(l, L_ONESHOT)) {
		bcm4360_bmac_led_blink_event(hw, 1);
		U8(l, L_ARMED) = 1;
		U8(l, L_ONESHOT) = 0;
	}
}
