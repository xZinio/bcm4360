# Task `radio`: the 2069 radio

Specification: [`../spec/acphy-radio.md`](../spec/acphy-radio.md) (sections in
parentheses below), register access: [`../spec/access.md`](../spec/access.md)
and your predecessor's code in `open/hw/access.c` (use it, do not change it).
Scenarios: all whose names start with `radio-` (`python ab.py list`).

Scope: radio major revision 0, radio revisions 3 and 4, PHY revisions 0 and 1.
Branches for other revisions are left out (where the specification mentions
them, a comment is enough).

## What to write

`open/include/bcm4360/radio2069.h` and `open/phy/radio2069.c`:

```c
/*
 * Tables of the radio. They are Broadcom's data and are not part of the open
 * code: the caller provides them (in the tests they are handed over from the
 * object's memory, a driver loads them as firmware).
 */
struct bcm4360_radio_tables {
	const u16 *prefregs;	/* pairs {address, value}, see section 6; may be NULL */
	const u16 *chan_tuning;	/* entries of 58 words, see "Tables" */
	u32 chan_entries;	/* number of entries */
};

struct bcm4360_radio {
	struct bcm4360_phy_io *io;
	struct bcm4360_radio_tables tbl;
	u8 cores;		/* number of cores (chains) of the PHY */
	u8 rev;			/* radio revision (3 or 4) */
	u8 phy_rev;		/* PHY revision (0 or 1) */
	u32 boardflags;
	bool skip_rcal;		/* boardflags3 bit 3 or bit 13 */
	bool bf_bit29;		/* boardflags bit 29 */
	/* results of the RC calibration, section 7 */
	u8 rccal_gmult;
	u8 rccal_gmult_rc;
	u8 rccal_dacbuf;
	/* you may add fields below this line */
};

/* set the defaults of the calibration results; the other fields are the caller's */
void bcm4360_radio_init(struct bcm4360_radio *r);

void bcm4360_radio_off(struct bcm4360_radio *r);		/* section 4, "Off", steps 2..4 */
void bcm4360_radio_pwron_seq(struct bcm4360_radio *r);		/* section 5 */
void bcm4360_radio_rcal(struct bcm4360_radio *r);		/* section 4, "On", steps 3 and 4 */
void bcm4360_radio_rccal(struct bcm4360_radio *r);		/* section 7 */

/* section 10: entry of a channel, NULL if there is none; *freq = MHz */
const u16 *bcm4360_radio_chan_entry(struct bcm4360_radio *r, u8 channel, u16 *freq);

/*
 * section 11, steps 2 to 8: write the entry and the fixed patches, the loop
 * filter on 5 GHz, start the VCO calibration. `channel` is the low byte of
 * the chanspec.
 */
void bcm4360_radio_tune(struct bcm4360_radio *r, const u16 *entry, u8 channel,
			bool is_5g);
void bcm4360_radio_rfpll_150khz(struct bcm4360_radio *r);	/* section 12 */
void bcm4360_radio_vcocal(struct bcm4360_radio *r);		/* section 13, start */
void bcm4360_radio_vcocal_wait(struct bcm4360_radio *r, bool settle);	/* section 13, wait */
void bcm4360_radio_afecal(struct bcm4360_radio *r);		/* section 14 */
/* section 15: the radio part of the band change */
void bcm4360_radio_band_change(struct bcm4360_radio *r, bool is_5g);
/* section 16 */
void bcm4360_radio_tssi_setup(struct bcm4360_radio *r, u8 coremask, u8 mode,
			      bool is_5g);
```

The names, argument orders and the fields above the line are the contract
with the tests. `struct bcm4360_radio` must not be larger than 512 bytes.

The tests fill a `struct bcm4360_radio` (fields above the line), call
`bcm4360_radio_init()`, and then call your functions where the object calls
its own, with the card in the same state. Compared are the accesses to PHY and
radio registers and the delays.
