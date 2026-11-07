/*
 * The microcode host flags of the 802.11 core (wlc_bmac.c): five 16-bit words
 * that tell the microcode how to behave. The driver keeps a copy per band and
 * writes the copy of the current band to the shared memory.
 *
 * Written from docs/re/spec/bmac-init.md, section A9 (wlc_bmac_mhf), for MAC
 * core revision 42 (chip 0x4360, PCIe).
 */
#include <bcm4360/mac.h>
#include <bcm4360/access.h>

/*
 * The `bands` argument (docs/re/spec/bmac-init.md, A9 step 1): which band's
 * host-flag copy the call selects.
 */
#define MHF_BANDS_AUTO		0	/* the current band */
#define MHF_BANDS_5G		1	/* the 5 GHz band state */
#define MHF_BANDS_2G		2	/* the 2.4 GHz band state */
#define MHF_BANDS_ALL		3	/* both band states (acts on the current one) */

/*
 * Index into mac->mhf[][] and bit position in mac->band_present: the 2.4 GHz
 * row is first (docs/re/tasks/bmac-mhf.md).
 */
#define MHF_BAND_2G		0
#define MHF_BAND_5G		1

/* Number of host-flag words per band (docs/re/spec/bmac-init.md, A9). */
#define MHF_COUNT		5

/*
 * idx -> shared-memory word of that host flag
 * (docs/re/spec/bmac-init.md, A9 host-flag table).
 */
static const u16 mhf_shm_offset[MHF_COUNT] = {
	0x5e,	/* host flags 1 */
	0x60,	/* host flags 2 */
	0x62,	/* host flags 3 */
	0x78,	/* host flags 4 */
	0xd4,	/* host flags 5 */
};

/*
 * docs/re/spec/bmac-init.md, A9 (wlc_bmac_mhf): change the masked bits of
 * host-flag word `idx` of the band(s) that `bands` selects, and write the new
 * value to the shared memory only when the clock is on, the value actually
 * changes, and the selected band is the current band.
 *
 * `bands` == MHF_BANDS_ALL selects the current band (so the conditional write
 * can happen for it) and then also sets both bands' copies, with no further
 * access. A band whose band_present bit is clear is skipped. Any `bands` value
 * other than 0..3 does nothing at all. `idx` is not checked (as in the object).
 */
void bcm4360_bmac_mhf(struct bcm4360_mac *mac, u8 idx, u16 mask, u16 val,
		     u32 bands)
{
	u8 sel;		/* selected band: MHF_BAND_2G or MHF_BAND_5G */

	/* step 1: which band's copy the call selects */
	switch (bands) {
	case MHF_BANDS_5G:
		sel = MHF_BAND_5G;
		break;
	case MHF_BANDS_2G:
		sel = MHF_BAND_2G;
		break;
	case MHF_BANDS_AUTO:
	case MHF_BANDS_ALL:
		sel = mac->current_band;
		break;
	default:
		return;		/* any other value does nothing at all */
	}

	/* step 2: update the selected band's copy; write it to the shared memory
	 * only when the clock is on, the value changed, and the selected band is
	 * the current band (for MHF_BANDS_ALL it is, so that write happens too) */
	if (mac->band_present & (1u << sel)) {
		u16 old = mac->mhf[sel][idx];
		u16 new = (old & ~mask) | val;

		mac->mhf[sel][idx] = new;
		if (mac->clk && new != old && sel == mac->current_band)
			bcm4360_shm_write(mac->hw, mhf_shm_offset[idx], new);
	}

	/* step 3: MHF_BANDS_ALL also sets both bands' copies; no further access */
	if (bands == MHF_BANDS_ALL) {
		if (mac->band_present & (1u << MHF_BAND_2G))
			mac->mhf[MHF_BAND_2G][idx] =
				(mac->mhf[MHF_BAND_2G][idx] & ~mask) | val;
		if (mac->band_present & (1u << MHF_BAND_5G))
			mac->mhf[MHF_BAND_5G][idx] =
				(mac->mhf[MHF_BAND_5G][idx] & ~mask) | val;
	}
}
