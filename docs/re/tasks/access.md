# Task `access`: register access layer

Specification: [`../spec/access.md`](../spec/access.md).
Scenario: `access` (`python ab.py run access`).

## What to write

`open/include/bcm4360/access.h` and `open/hw/access.c` with:

```c
/* state of the register access of one PHY */
struct bcm4360_phy_io {
	struct bcm4360_hw *hw;
	/* write pacing, see the specification */
	...
};

void bcm4360_phy_io_init(struct bcm4360_phy_io *io, struct bcm4360_hw *hw,
			 bool pci, u16 write_limit);

u16  bcm4360_phy_read(struct bcm4360_phy_io *io, u16 addr);
void bcm4360_phy_write(struct bcm4360_phy_io *io, u16 addr, u16 val);
void bcm4360_phy_mod(struct bcm4360_phy_io *io, u16 addr, u16 mask, u16 val);
void bcm4360_phy_and(struct bcm4360_phy_io *io, u16 addr, u16 val);
void bcm4360_phy_or(struct bcm4360_phy_io *io, u16 addr, u16 val);

u16  bcm4360_radio_read(struct bcm4360_phy_io *io, u16 addr);
void bcm4360_radio_write(struct bcm4360_phy_io *io, u16 addr, u16 val);
void bcm4360_radio_mod(struct bcm4360_phy_io *io, u16 addr, u16 mask, u16 val);
void bcm4360_radio_and(struct bcm4360_phy_io *io, u16 addr, u16 val);
void bcm4360_radio_or(struct bcm4360_phy_io *io, u16 addr, u16 val);
void bcm4360_radio_xor(struct bcm4360_phy_io *io, u16 addr, u16 val);

/*
 * n entries of `width` bits (8, 16, 32, 48, 60 or 64) starting at `offset` of
 * table `id`. Layout of `data`: width 8: bytes; 16: u16; 32: u32; 48: three
 * u16 per entry, lowest first; 60 and 64: two u32 per entry, lower first.
 */
void bcm4360_tbl_write(struct bcm4360_phy_io *io, u32 id, u32 n, u32 offset,
		       u32 width, const void *data);
void bcm4360_tbl_read(struct bcm4360_phy_io *io, u32 id, u32 n, u32 offset,
		      u32 width, void *data);

/* shared memory of driver and microcode, offset in bytes */
u16  bcm4360_shm_read(struct bcm4360_hw *hw, u16 offset);
void bcm4360_shm_write(struct bcm4360_hw *hw, u16 offset, u16 val);
```

`struct bcm4360_phy_io` must not be larger than 256 bytes (the test allocates
that much and calls `bcm4360_phy_io_init()` on it).

The names and argument orders above are the contract with the test; do not
change them. `struct bcm4360_hw` is defined in `open/include/bcm4360/hw.h`;
the register access functions to use are `d11_read16()`, `d11_write16()`,
`d11_read32()`, `d11_write32()` from there.
