# Task `bmac-rename`: separate the MAC's functions from the PHY's MAC services

`open/include/bcm4360/phy_env.h` declares the services the PHY needs from the
MAC layer, with a `struct bcm4360_hw *` first argument:
`bcm4360_mac_suspend`, `bcm4360_mac_enable`, `bcm4360_mac_mctrl`,
`bcm4360_mac_mhf`, `bcm4360_mac_wake_override_set`,
`bcm4360_mac_wake_override_clear` (and others). The MAC layer's own
implementation (`open/include/bcm4360/mac.h`, `open/mac/bmac_ctrl.c`,
`open/mac/bmac_mhf.c`) defines functions with **the same six names** but a
`struct bcm4360_mac *` first argument. The two cannot be linked into one
driver.

## What to do

Rename the MAC layer's six functions (declarations in `mac.h`, definitions
and every call in `bmac_ctrl.c` and `bmac_mhf.c`):

| Now | New |
|---|---|
| `bcm4360_mac_mctrl` | `bcm4360_bmac_mctrl` |
| `bcm4360_mac_wake_override_set` | `bcm4360_bmac_wake_override_set` |
| `bcm4360_mac_wake_override_clear` | `bcm4360_bmac_wake_override_clear` |
| `bcm4360_mac_suspend` | `bcm4360_bmac_suspend` |
| `bcm4360_mac_enable` | `bcm4360_bmac_enable` |
| `bcm4360_mac_mhf` | `bcm4360_bmac_mhf` |

Nothing else changes: no behaviour, no other identifiers, no structure
fields. Do not touch `phy_env.h` or the PHY files.

## Test

The coordinator changes the test harness to the new names at the same time
(`tools/re/mac_scenarios.py`, `hybrid/mac.c`); after your rename,
`python ab.py build` must be warning-free and `python ab.py run mac-ctrl` and
`python ab.py run mac-mhf` must report `0 differences`. If they cannot find
the new names, report it: the harness change may not be in yet.
