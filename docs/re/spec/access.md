# Register access

How the driver reaches the registers of the card. All other specifications
build on the notation defined here.

## Scope

The access primitives of `wlc_phy_cmn.c` (`phy_reg_read` .text+0xb2f34,
`phy_reg_write` +0xb6650, `phy_reg_mod` +0xb2d55, `phy_reg_and`, `phy_reg_or`,
`read_radio_reg` +0xb305c, `write_radio_reg` +0xb6101, `mod_radio_reg`,
`and_radio_reg`, `or_radio_reg`, `xor_radio_reg`, `wlc_phy_write_table_ext`
+0xb6959, `wlc_phy_read_table_ext` +0xb6770), of `wlc_phy_ac.c`
(`wlc_phy_table_write_acphy` +0x98908, `wlc_phy_table_read_acphy` +0x97ecf)
and the object memory access of `wlc_bmac.c`; for MAC core revision 42 on a
PCIe card. All names are original.

## Address spaces and notation

| Notation | Space | Reached through |
|---|---|---|
| `PCICFG(o)` | PCI configuration space of the card | the host |
| `CC(o)` | ChipCommon core registers | BAR0 + 0x3000 + o (fixed window), or the movable window |
| `PCIE(o)` | PCIe Gen2 core registers | BAR0 + 0x2000 + o (fixed window) |
| `D11(o)` | 802.11 MAC core registers | BAR0 + o while the movable window points to the core |
| `WRAP(core, o)` | AXI wrapper of a core | BAR0 + 0x1000 + o while the second window points to the wrapper |
| `PHY(a)` | PHY registers, 16 bit | `D11(0x3fc)` address, `D11(0x3fe)` data |
| `RADIO(a)` | radio registers, 16 bit | `D11(0x3d8)` address, `D11(0x3da)` data |
| `TBL(id)[i]` | PHY tables | `PHY(0x0d)`, `PHY(0x0e)`, `PHY(0x0f)`, `PHY(0x10)`, `PHY(0x11)` |
| `SHM(o)` | shared memory of driver and microcode, 16 bit words, o in bytes | `D11(0x160)` address, `D11(0x164)`/`D11(0x166)` data |
| `PMU_CHIPCTL[n]`, `PMU_REGCTL[n]`, `PMU_PLLCTL[n]` | indirect registers of the PMU | `CC(0x650)`/`CC(0x654)`, `CC(0x658)`/`CC(0x65c)`, `CC(0x660)`/`CC(0x664)` |

`mod(reg, mask, value)` stands for: read `reg`, write `(old & ~mask) | (value & mask)`.

The movable windows are set by writing a backplane address to `PCICFG(0x80)`
(first window, BAR0 + 0) and to `PCICFG(0x70)` (second window, BAR0 + 0x1000;
before the driver has identified the PCIe Gen2 core it writes `PCICFG(0xac)`,
the register of older bridges).

## PHY registers

* **Read** `PHY(a)`: write `a` as 16 bit to `D11(0x3fc)`, read 16 bit from
  `D11(0x3fe)`.
* **Write** `PHY(a) = v`: one 32 bit write of `(v << 16) | a` to `D11(0x3fc)`
  (address and data register are adjacent; the write sets both).
* `phy_reg_mod`, `phy_reg_and`, `phy_reg_or`: a read as above, then a 16 bit
  write of the new value to `D11(0x3fe)` (the address is still set).

**Write pacing on PCI.** The driver counts consecutive writes made with
`phy_reg_write` and `write_radio_reg` (one counter of 16 bit for both). Before
the write the counter is incremented; when it is then greater than or equal to
the limit it is set to 0 and a dummy read is made first: 16 bit from
`D11(0x3e0)` (the PHY version register) in `phy_reg_write`, 32 bit from
`D11(0x120)` (the MAC control register) in `write_radio_reg`. The counter is set to 0 by
every `phy_reg_read`, `read_radio_reg`, `phy_reg_mod`, `phy_reg_and` and
`phy_reg_or` (they contain a read). `mod_radio_reg` and its relatives are a
`read_radio_reg` followed by a `write_radio_reg`. The writes made directly to
the data register in wide table transfers (below) are not counted. In the
traces the limit is 24: a dummy read before every 24th write in a row. The
pacing only exists for bus type PCI; the values read are not used. It bounds
the number of writes in a row without a read.

## Radio registers

For MAC core revisions >= 24 (except 27):

* **Read** `RADIO(a)`: write `a` as 16 bit to `D11(0x3d8)`, read 16 bit from
  `D11(0x3da)`.
* **Write** `RADIO(a) = v`: write `a` as 16 bit to `D11(0x3d8)`, write `v` as
  16 bit to `D11(0x3da)`.

For the AC-PHY the register address is used as it is (older PHYs set a "read"
bit in the address). The identification of the radio is in `RADIO(0)` and
`RADIO(1)`, read at attach with plain 16 bit accesses to `D11(0x3d8)`/
`D11(0x3da)`: register 1 holds the id (0x2069); of register 0, bits 0..7 are
the revision, bits 4..11 the major revision and bits 0..3 the minor revision.

## PHY tables

A table is addressed by an id and an offset (index of the entry); entries are
8, 16, 32, 48, 60 or 64 bits wide depending on the table. A transfer of `n`
entries starting at offset `o` of table `id`:

1. `PHY(0x0d) = id`, `PHY(0x0e) = o`.
2. Per entry, depending on the width:
   * 8 or 16 bit: write (or read) `PHY(0x0f)`.
   * 32 bit: write the upper half to `PHY(0x10)`, then the lower half to
     `PHY(0x0f)`; for reading: read `PHY(0x0f)` (lower), then `PHY(0x10)` (upper).
   * 48 bit (three 16 bit words, lowest first): the first word is written to
     `PHY(0x11)` with a normal register write, the two others are written as
     16 bit directly to the data register `D11(0x3fe)`. Reading: the first
     word with a normal read of `PHY(0x11)`, the others by reading `D11(0x3fe)`.
   * 60 or 64 bit (two 32 bit values per entry in the driver's memory, lower
     first): the low half of the first value is written to `PHY(0x11)`, then
     directly to `D11(0x3fe)`: its high half, the low half of the second
     value, its high half. Reading: the first word with a normal read of
     `PHY(0x11)`, three more by reading `D11(0x3fe)`, in the same order. The
     driver does not mask anything for 60 bit: all four words are
     transferred as they are.
   * Entries of 8 bit are written with the upper byte 0; of what is read the
     lower byte is kept.
   * Any other width: nothing is transferred (the id and offset registers of
     step 1 are written all the same).
3. The offset advances by itself after each entry.

In the code a transfer is described by a record of 24 bytes: pointer to the
data, number of entries, table id, offset, width in bits. The static tables
loaded at initialisation are arrays of such records (`acphytbl_info_rev0` and
so on, see [01-anatomy.md](../01-anatomy.md)).

## Object memories of the MAC

`D11(0x160)` selects memory and address: bits 16..19 the memory (0 microcode,
1 shared memory, 2 scratch registers of the microcode, 3 internal hardware
registers, 4 address match table), the low bits the address. `D11(0x164)` and
`D11(0x166)` are the lower and upper 16 bit of the 32 bit data register.

* Shared memory word at byte offset `o`: `D11(0x160) = 0x00010000 | (o >> 2)`
  as 32 bit, then a 16 bit access to `D11(0x164)` if `o & 2` is 0, to
  `D11(0x166)` otherwise. After writing the address the driver reads
  `D11(0x160)` back once as 32 bit (a flush; the value is not used).
* The other memories, including the microcode download: see the MAC
  specification.

## Verification

The model of the card in `tools/re/bcm4360.py` implements exactly these rules
to decode the accesses, and the object runs through bring-up and channel
changes on it. The table rules are checked by `tools/re/selftest.py`: for
every table write of the bring-up (655 transfers, 4,211 entries of 8, 16, 32
and 48 bit) the entries taken from the transfer record at the entry of
`wlc_phy_write_table_ext` are compared with the register writes that follow.
Transfers of 60 or 64 bit entries do not occur during bring-up and channel
changes; their description is from the code only. The write pacing was read
from the disassembly of the primitives and matches the dummy reads in the
traces (24 writes between two of them).

## Open questions

* The write pacing limit (24 in the traces) is a field of the PHY state; where
  it is set and whether it depends on the board is covered by the PHY attach
  specification.
