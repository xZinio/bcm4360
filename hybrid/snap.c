// SPDX-License-Identifier: ISC
/*
 * Hybrid build: a read-only snapshot of the card's PHY registers, radio
 * registers, PHY tables and shared memory, to compare Broadcom's PHY and the
 * open one on the card (scripts/phy-snap.py, docs/re/03-tools.md).
 *
 * Not a takeover: nothing of the object calls it. The glue's file
 * /proc/bcm4360_snap<unit> does, one item at a time, holding the driver's lock
 * (src/wl/sys/wl_linux.c). The same code runs in the emulator
 * (tools/re/hybrid.py snap).
 *
 * The reads go straight to the registers of the 802.11 core through the open
 * access layer (open/hw/access.c), whatever the switches: the hardware state is
 * what both PHYs share, their software state is not comparable (the open PHY
 * keeps its own). Every item first checks that the core can be touched - the
 * backplane window is on the 802.11 core, the core is up and has its clock
 * and the PHY's - and keeps the microcode awake while it reads, as the PHY's
 * own register accesses do (wake override 0x02, bmac-init.md A3/A4). Table
 * reads move the table pointer of the PHY, so they are made with the MAC
 * suspended, as the drivers make table writes.
 *
 * A request is text, one item per line:
 *
 *     phy   ADDR COUNT            PHY registers ADDR .. ADDR+COUNT-1
 *     radio ADDR COUNT            radio registers
 *     tbl   ID OFFSET COUNT WIDTH  COUNT entries of a PHY table (WIDTH 8, 16,
 *                                 32, 48, 60 or 64 bits)
 *     shm   OFFSET COUNT          COUNT 16-bit words of shared memory from
 *                                 byte OFFSET (even)
 *
 * Numbers are decimal or 0x-hex. The result is 16-bit words in the order of
 * the request: one per register or shared-memory word; per table entry one
 * (8, 16 bit), two (32 bit: low, high), three (48) or four (60, 64), lowest
 * first.
 */
#include "hybrid.h"
#include <bcm4360/access.h>

/* fields of the object's structures (bmac-init.md, bmac-attach.md) */
#define WLC_HW			0x020	/* wlc -> wlc_hw */
#define WLC_HW_SIH		0x0b8	/* si_t * */
#define WLC_HW_REGS		0x0d0	/* registers of the 802.11 core */
#define WLC_HW_UP		0x10c	/* u8: the interface is up */
#define WLC_HW_CLK		0x186	/* u8: the core is out of reset and has its clock */
#define WLC_HW_PHYCLK		0x190	/* u8: the PHY is out of reset and has its clock */

#define D11_CORE_ID		0x812
#define WAKE_PHYREG		0x02	/* bmac-init.md, wake_override: PHY register access */

/* limits of one item */
#define SNAP_MAX_COUNT		4096
#define SNAP_PHY_END		0x2000
#define SNAP_RADIO_END		0x2000
#define SNAP_SHM_END		0x8000
#define SNAP_TBL_ID_MAX		0xff
#define SNAP_TBL_END		0x10000

u32 si_coreid(void *sih);
u8 si_iscoreup(void *sih);
u8 si_deviceremoved(void *sih);
void wlc_ucode_wake_override_set(void *wlc_hw, u32 bit);
void wlc_ucode_wake_override_clear(void *wlc_hw, u32 bit);
void wlc_bmac_suspend_mac_and_wait(void *wlc_hw);
void wlc_bmac_enable_mac(void *wlc_hw);

#define FIELD(p, off, type)	(*(type *)((u8 *)(p) + (off)))

static bool snap_space(char c0, char c1, u8 *space)
{
	if (c0 == 'p' && c1 == 'h') {
		*space = HYB_SNAP_PHY;
	} else if (c0 == 'r' && c1 == 'a') {
		*space = HYB_SNAP_RADIO;
	} else if (c0 == 't' && c1 == 'b') {
		*space = HYB_SNAP_TBL;
	} else if (c0 == 's' && c1 == 'h') {
		*space = HYB_SNAP_SHM;
	} else {
		return false;
	}
	return true;
}

static bool is_space(char c)
{
	return c == ' ' || c == '\t' || c == '\r';
}

/* a decimal or 0x-hex number at *p, up to 2^32 - 1; advances *p */
static bool snap_number(const char **p, const char *end, u32 *v)
{
	const char *s = *p;
	u32 base = 10, n = 0, d;
	bool any = false;

	while (s < end && is_space(*s))
		s++;
	if (end - s > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		base = 16;
		s += 2;
	}
	for (; s < end; s++) {
		char c = *s;

		if (c >= '0' && c <= '9')
			d = c - '0';
		else if (base == 16 && c >= 'a' && c <= 'f')
			d = c - 'a' + 10;
		else if (base == 16 && c >= 'A' && c <= 'F')
			d = c - 'A' + 10;
		else
			break;
		if (n > (0xffffffffU - d) / base)
			return false;
		n = n * base + d;
		any = true;
	}
	*p = s;
	*v = n;
	return any;
}

u32 hyb_snap_words(const struct hyb_snap_item *it)
{
	u32 per = 1;

	if (it->space == HYB_SNAP_TBL) {
		switch (it->width) {
		case 8:
		case 16:
			per = 1;
			break;
		case 32:
			per = 2;
			break;
		case 48:
			per = 3;
			break;
		default:
			per = 4;
			break;
		}
	}
	return per * it->count;
}

static bool snap_valid(const struct hyb_snap_item *it)
{
	if (it->count == 0 || it->count > SNAP_MAX_COUNT)
		return false;
	switch (it->space) {
	case HYB_SNAP_PHY:
		return it->start + it->count <= SNAP_PHY_END;
	case HYB_SNAP_RADIO:
		return it->start + it->count <= SNAP_RADIO_END;
	case HYB_SNAP_SHM:
		return !(it->start & 1) && it->start + 2 * it->count <= SNAP_SHM_END;
	case HYB_SNAP_TBL:
		return it->id <= SNAP_TBL_ID_MAX && it->start + it->count <= SNAP_TBL_END &&
		       (it->width == 8 || it->width == 16 || it->width == 32 ||
			it->width == 48 || it->width == 60 || it->width == 64);
	}
	return false;
}

int hyb_snap_parse(const char *req, u32 len, struct hyb_snap_item *items, u32 max)
{
	const char *p = req, *end = req + len;
	u32 n = 0;

	while (p < end) {
		const char *eol = p;
		struct hyb_snap_item it = { 0 };
		u32 v[4] = { 0 };
		u32 i, need;

		while (eol < end && *eol != '\n')
			eol++;
		while (p < eol && is_space(*p))
			p++;
		if (p == eol || *p == '#') {
			p = eol + 1;
			continue;
		}
		if (eol - p < 2 || !snap_space(p[0], p[1], &it.space))
			return -HYB_SNAP_EINVAL;
		while (p < eol && !is_space(*p))
			p++;
		need = it.space == HYB_SNAP_TBL ? 4 : 2;
		for (i = 0; i < need; i++) {
			if (!snap_number(&p, eol, &v[i]))
				return -HYB_SNAP_EINVAL;
		}
		while (p < eol && is_space(*p))
			p++;
		if (p != eol)
			return -HYB_SNAP_EINVAL;
		if (it.space == HYB_SNAP_TBL) {
			if (v[0] > SNAP_TBL_ID_MAX || v[3] > 64)
				return -HYB_SNAP_EINVAL;
			it.id = v[0];
			it.start = v[1];
			it.count = v[2];
			it.width = v[3];
		} else {
			it.start = v[0];
			it.count = v[1];
		}
		if (!snap_valid(&it))
			return -HYB_SNAP_EINVAL;
		if (n == max)
			return -HYB_SNAP_ENOSPC;
		items[n++] = it;
		p = eol + 1;
	}
	return n;
}

/* whether the 802.11 core can be touched now (the caller holds the driver's lock) */
static int snap_ready(void *wlc_hw)
{
	void *sih = FIELD(wlc_hw, WLC_HW_SIH, void *);

	if (!sih || !FIELD(wlc_hw, WLC_HW_REGS, void *))
		return -HYB_SNAP_ENODEV;
	if (!FIELD(wlc_hw, WLC_HW_UP, u8) || !FIELD(wlc_hw, WLC_HW_CLK, u8) ||
	    !FIELD(wlc_hw, WLC_HW_PHYCLK, u8))
		return -HYB_SNAP_EAGAIN;
	if (si_coreid(sih) != D11_CORE_ID)
		return -HYB_SNAP_ENODEV;
	if (!si_iscoreup(sih))
		return -HYB_SNAP_EAGAIN;
	if (si_deviceremoved(sih))
		return -HYB_SNAP_ENODEV;
	return 0;
}

int hyb_snap_read(void *wlc, const struct hyb_snap_item *it, u16 *out, u32 max)
{
	void *wlc_hw = wlc ? FIELD(wlc, WLC_HW, void *) : NULL;
	struct bcm4360_hw hw = { 0 };
	struct bcm4360_phy_io io;
	u32 words = hyb_snap_words(it), i, k, per;
	int r;

	if (!wlc_hw)
		return -HYB_SNAP_ENODEV;
	if (!snap_valid(it))
		return -HYB_SNAP_EINVAL;
	if (words > max)
		return -HYB_SNAP_ENOSPC;
	r = snap_ready(wlc_hw);
	if (r)
		return r;
	hw.d11 = FIELD(wlc_hw, WLC_HW_REGS, void *);
	bcm4360_phy_io_init(&io, &hw, false, 0);

	wlc_ucode_wake_override_set(wlc_hw, WAKE_PHYREG);
	switch (it->space) {
	case HYB_SNAP_PHY:
		for (i = 0; i < it->count; i++)
			out[i] = bcm4360_phy_read(&io, it->start + i);
		break;
	case HYB_SNAP_RADIO:
		for (i = 0; i < it->count; i++)
			out[i] = bcm4360_radio_read(&io, it->start + i);
		break;
	case HYB_SNAP_SHM:
		for (i = 0; i < it->count; i++)
			out[i] = bcm4360_shm_read(&hw, it->start + 2 * i);
		break;
	case HYB_SNAP_TBL:
		per = words / it->count;
		wlc_bmac_suspend_mac_and_wait(wlc_hw);
		for (i = 0; i < it->count; i++) {
			u32 e[2] = { 0 };
			u16 w[4] = { 0 };

			/* one entry per transfer: the layout of tbl_read (access.h) */
			if (it->width == 8) {
				u8 b = 0;

				bcm4360_tbl_read(&io, it->id, 1, it->start + i, 8, &b);
				w[0] = b;
			} else if (it->width == 16) {
				bcm4360_tbl_read(&io, it->id, 1, it->start + i, 16, &w[0]);
			} else if (it->width == 32) {
				bcm4360_tbl_read(&io, it->id, 1, it->start + i, 32, &e[0]);
				w[0] = e[0] & 0xffff;
				w[1] = e[0] >> 16;
			} else if (it->width == 48) {
				bcm4360_tbl_read(&io, it->id, 1, it->start + i, 48, w);
			} else {
				bcm4360_tbl_read(&io, it->id, 1, it->start + i, it->width, e);
				w[0] = e[0] & 0xffff;
				w[1] = e[0] >> 16;
				w[2] = e[1] & 0xffff;
				w[3] = e[1] >> 16;
			}
			for (k = 0; k < per; k++)
				out[i * per + k] = w[k];
		}
		wlc_bmac_enable_mac(wlc_hw);
		break;
	}
	wlc_ucode_wake_override_clear(wlc_hw, WAKE_PHYREG);
	return words;
}
