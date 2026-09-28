# Task `pmu`: power management unit and clocks

Specification: [`../spec/pmu.md`](../spec/pmu.md), register access notation:
[`../spec/access.md`](../spec/access.md).
Scenarios: all whose names start with `pmu-` (`python ab.py list`).

Scope: chips 0x4360 and 0x4352, all chip revisions the specification
describes (the tests use revisions 3 and 4), PMU on a PCIe card. Leave out:
the variables that override registers (`rmin`, `rmax`, `r<n>t`, `r<n>d`,
`chipc<n>`, `reg<n>`, `pll<n>`; they only exist with a file `nvram.txt`), the
functions the specification lists as having no effect or no caller, and
`si_clkctl_xtal` / `si_clkctl_init` / `si_clkctl_cc` (not used on this card).

## Register access

The object reaches ChipCommon in two ways: through the fixed window, and by
making ChipCommon the current core (`[CC window]` in the specification), which
costs writes to the PCI configuration space. Your code always uses the fixed
window (`cc_read32()`, `cc_write32()` of `hw.h`; the PCIe core: `hw_read32(hw->pcie, ...)`).
The tests therefore compare the accesses to ChipCommon and to the PCIe core
and the delays, not the window switching.

The read-write-read pattern of `corereg` is part of the behaviour and is
compared.

## What to write

`open/include/bcm4360/pmu.h` and `open/chip/pmu.c`:

```c
/* identification of the chip, read at attach (docs/re/spec/pmu.md, "Overview", 1.2) */
struct bcm4360_chip_info {
	u32 chip;		/* chip id, 0x4360 */
	u32 chiprev;
	u32 chippkg;
	u32 chipst;		/* chip status, CC(0x2c) */
	u32 ccrev;		/* revision of ChipCommon */
	u32 cccaps;		/* CC(0x04) */
	u32 cccaps_ext;		/* CC(0xac) */
	u32 pmurev;		/* bits 0..7 of CC(0x604) */
	u32 pmucaps;		/* CC(0x604) */
};

struct bcm4360_pmu {
	struct bcm4360_hw *hw;
	struct bcm4360_chip_info info;
	u32 ilp_hz;		/* measured ILP clock, 0 = not measured yet */
	/* you may add fields below this line */
};

/* the accessors of the indirect registers; result: the value read last */
u32 bcm4360_pmu_chipcontrol(struct bcm4360_pmu *p, u32 reg, u32 mask, u32 val);
u32 bcm4360_pmu_regcontrol(struct bcm4360_pmu *p, u32 reg, u32 mask, u32 val);
u32 bcm4360_pmu_pllcontrol(struct bcm4360_pmu *p, u32 reg, u32 mask, u32 val);
void bcm4360_pmu_pllupd(struct bcm4360_pmu *p);

void bcm4360_pmu_init(struct bcm4360_pmu *p);			/* si_pmu_init */
u32 bcm4360_pmu_measure_alpclk(struct bcm4360_pmu *p);		/* kHz */
void bcm4360_pmu_pll_init(struct bcm4360_pmu *p, u32 xtalfreq);	/* si_pmu_pll_init */
void bcm4360_pmu_res_init(struct bcm4360_pmu *p);		/* si_pmu_res_init */

u32 bcm4360_pmu_alp_clock(struct bcm4360_pmu *p);		/* Hz */
u32 bcm4360_pmu_ilp_clock(struct bcm4360_pmu *p);		/* Hz */
u32 bcm4360_pmu_si_clock(struct bcm4360_pmu *p);		/* Hz */
u32 bcm4360_pmu_get_bb_vcofreq(struct bcm4360_pmu *p, u32 xtal_mhz);
u32 bcm4360_pmu_fast_pwrup_delay(struct bcm4360_pmu *p);	/* microseconds */

void bcm4360_pmu_otp_power(struct bcm4360_pmu *p, bool on);	/* si_pmu_otp_power */
bool bcm4360_pmu_is_otp_powered(struct bcm4360_pmu *p);
void bcm4360_pmu_rfldo(struct bcm4360_pmu *p, bool on);
```

The names, argument orders and the fields above the line are the contract
with the tests. `struct bcm4360_pmu` must not be larger than 256 bytes. The
tests fill the fields above the line (the rest is zero) and call your
functions where the object calls its own, with the card in the same state;
results of functions that return a value are compared too.
