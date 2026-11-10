# bcm4360: Wi‑Fi driver for the Broadcom BCM4360

A Linux driver for the Broadcom **BCM4360** 802.11ac Wi‑Fi chip, found in the
2013–2017 MacBook Air. It comes in two drivers, installed together through
DKMS and rebuilt automatically for every new kernel:

- **`bcm4360`** started as Broadcom's `wl` driver, repaired for current
  kernels; open code has replaced most of Broadcom's code inside it. It is
  the default.
- **`bcm4360_open`** (since 1.2.0) contains none of Broadcom's code. It loads
  only Broadcom's microcode and data tables, cut out of Broadcom's download at
  install time. It is an install option, and `bcm4360` is its fallback.

## What's new

**1.2.2**

- **`bcm4360` no longer depends on `bcm4360_open` building.** The two
  drivers are separate DKMS packages, and `bcm4360_open` is built only with
  `install.sh --open`. Before, a kernel whose mac80211 `bcm4360_open` does not
  support (anything before 6.17, e.g. Debian 13's 6.12) stopped the default
  driver from installing as well. If `bcm4360_open` does not build for a new
  kernel, `bcm4360` loads in its place at boot.
- **A fallback for `bcm4360_open` from the start.** If it does not connect
  within 5 minutes of loading (no interface because its probe failed, no
  access point seen, or a saved network in range but no connection), the
  watchdog switches to `bcm4360`. Before, it acted only after a connection.
- **The watchdog judges the card, not a USB dongle**, and runs as one service
  instead of a timer that logged three lines every 30 s.
- **`install.sh` changes nothing until everything is ready.** It checks the
  tools, verifies Broadcom's object in `lib/` again, cuts the firmware data
  and builds both drivers for the running kernel first. `uninstall.sh`
  removes every installed version, not only the checkout's.
- **Fixes in the drivers:** `bcm4360_open` reports an unknown signal as
  unknown instead of 0 dBm (the signal in `iw` and NetworkManager drifted
  towards 0 dBm under traffic), reports every frame to mac80211 with a
  status (also those it drops), survives a card that returns garbage DMA
  indices, and no longer races a hardware restart against unloading.
  `bcm4360`'s private ioctl checks for root before it allocates anything,
  and its buffer is capped.
- `make` and `make open` work on kernels built with clang. The blocklist is
  down to the two modules that can take the card (`bcma`, `wl`). Broadcom's
  download falls back to a copy at the Internet Archive (the checksums still
  decide).

**1.2.0**

- **A driver without any of Broadcom's code, as an option.** `bcm4360_open`
  is a mac80211 driver for this chip. It loads only Broadcom's microcode and
  data tables, cut out of Broadcom's download at install time, as
  `b43-fwcutter` does for `b43`. Install it with `install.sh --open` (see
  [Install](#install)). Tested on a MacBookAir7,2:
  - WPA2 with hardware or software encryption;
  - reconnects, radio off/on, link down/up, suspend to RAM;
  - 2.4 and 5 GHz, 80 MHz VHT;
  - a 20‑minute soak: 0.3 % pings lost, never disconnected, 63–99 Mbit/s
    down and 62–77 up (the Broadcom‑core driver on the same channel:
    73–83 / 39–45).

  Not tested yet: 802.1X (eduroam), power save, 40 MHz in 2.4 GHz.
  `bcm4360` is installed with it and stays the fallback: the watchdog
  switches to it if `bcm4360_open` cannot keep a connection.

**1.1.1**

- **The access point NetworkManager picks is the one it joins.** The driver
  passed only the network name to Broadcom's core, which then chose among
  all its access points by signal alone. A profile limited to 5 GHz still
  ended up on 2.4 GHz. Now the access point and channel wpa_supplicant
  picked (which honours the profile's band and BSSID) are the join target,
  as in `brcmfmac`. A profile with a fixed BSSID also turns the core's own
  roaming off, so it stays on that access point.
- **Stalled connections recover.** On busy networks the link can stay
  associated while no data gets through, which NetworkManager never notices.
  The watchdog now asks the router every 30 s. After 30–60 s without an
  answer it reconnects. If the link stalls again within 5 minutes, it reloads
  the driver.

**1.1.0**

- **Open code instead of Broadcom's.** Since 1.1.0 most of Broadcom's code
  for this chip runs as open code, written from specifications. This
  includes the radio and PHY (AC‑PHY), which the in‑kernel `b43` driver
  lacks (it marks its BCM4360 support as broken). The
  MAC hardware layer, DMA, clocks and power, board configuration and
  regulatory lookup are open too. In a bring‑up with channel changes,
  measured in an emulator, only 37 % of the Broadcom code that used to run
  still runs. Counting how often each piece runs, 99 % of what the driver
  executes is open code. Each open layer has a switch to fall back to
  Broadcom's code (see [Settings](#settings)).
- **Fewer problems than Broadcom's `wl`.** Connections and roams are
  reported to the kernel correctly. eduroam works through NetworkManager.
  Wi‑Fi no longer dies until a reboot after a failed roam, and scans no
  longer get stuck after a dropped link. The memory-safety warning at every
  boot is gone, `iw` shows correct values, and normal users can query the
  interface.
- **Recovers by itself.** A watchdog reloads the driver if Wi‑Fi stays dead.
  A fallback service loads `wl` if the driver failed to build for a new kernel.
  On networks with both bands, the driver prefers 5 GHz access points, which
  are more reliable with this chip.

Still Broadcom's: the upper 802.11 layer (joining networks, scanning,
aggregation, rate selection, keys) and the chip's microcode and data tables.
`bcm4360_open` (since 1.2.0, `install.sh --open`) replaces all of that
except the microcode and the data tables.

## Will it work on my machine?

Check for the chip:

```sh
lspci -nn | grep -i '14e4:43a'
```

The driver handles PCI IDs `14e4:43a0` (all MacBook Airs with this chip),
`14e4:43a1`, `14e4:43a2` and `14e4:4360`. It is tested on a MacBookAir7,2
with Arch Linux and kernel 7.2. Other distributions should work but are
untested. It needs a 64‑bit x86 CPU without Intel CET/IBT (10th generation
or older, as in every MacBook Air with this chip), because Broadcom's
code does not support CET.

## Install

**1. Have an internet connection.** The install downloads Broadcom's code
(2.9 MB, checksum‑verified) from broadcom.com. If Wi‑Fi doesn't work yet, use
USB tethering from a phone or an Ethernet adapter.

**2. Install the build tools** (kernel headers, compiler, DKMS, curl, and
Python 3 for `--open`):

```sh
sudo pacman -S --needed base-devel linux-headers dkms curl git python            # Arch
sudo apt install build-essential linux-headers-$(uname -r) dkms curl git python3 # Debian, Ubuntu
sudo dnf install kernel-devel make gcc dkms curl git python3                     # Fedora
```

Recommended: also install your distribution's `wl` driver (`broadcom-wl-dkms`
on Arch, `broadcom-sta-dkms` on Debian/Ubuntu). The fallback service loads it
if this driver is ever missing.

**3. Download and install:**

```sh
git clone https://github.com/xZinio/bcm4360.git
cd bcm4360
sudo sh scripts/install.sh
```

This builds the module (half a minute) for the running kernel, installs it,
and blocks the modules that would grab the card (`bcma`, and Broadcom's
`wl`). It also sets up the watchdog and the fallback service. Nothing is
installed if the build fails.

To use `bcm4360_open`, the driver without Broadcom's code, instead:

```sh
sudo sh scripts/install.sh --open
```

It also cuts the microcode and tables out of Broadcom's download into
`/lib/firmware/bcm4360/`. `bcm4360_open` needs Linux 6.17 or newer.
`bcm4360` is installed with it and stays the fallback. To go back, run
`install.sh` without `--open` and reboot.

**4. Reboot.** Or switch without rebooting (Wi‑Fi drops for about 10
seconds; with `--open`: `sudo rmmod bcm4360; sudo modprobe bcm4360_open`):

```sh
sudo rmmod bcm4360 2>/dev/null; sudo modprobe bcm4360; sudo systemctl restart wpa_supplicant
```

**5. Check it:**

```sh
cat /sys/module/bcm4360*/version    # the version you installed
iw dev wlan0 link                    # access point, band, signal
journalctl -k -b | grep bcm4360      # no WARNING or Call Trace
```

### Update

```sh
cd bcm4360 && git pull && sudo sh scripts/install.sh     # add --open for bcm4360_open
```

Then reboot, or switch with the command from step 4.

### Uninstall

```sh
sudo sh scripts/uninstall.sh && sudo reboot
```

This removes both drivers (every version DKMS has of them), the blocklists,
the firmware file, the watchdog and the fallback service. Your
distribution's `wl` driver, if installed, takes over again.

## If Wi‑Fi stops working

Try these in order. None of them needs a network.

```sh
# 1. Nothing connects:
sudo systemctl restart wpa_supplicant NetworkManager
# 2. Put one open layer back to Broadcom's code, e.g. the PHY, then reboot:
echo "options bcm4360 open_phy=0" | sudo tee /etc/modprobe.d/bcm4360-local.conf
# 3. Back to Broadcom's stock driver right now:
sudo rmmod bcm4360; sudo modprobe wl; sudo systemctl restart wpa_supplicant
```

With `bcm4360_open`, the watchdog switches to `bcm4360` by itself when
`bcm4360_open` does not connect within 5 minutes or loses its connection
(`journalctl -u bcm4360-watchdog` says why). Go back to `bcm4360` right away
with `sudo rmmod bcm4360_open; sudo modprobe bcm4360`, and for good with
`sudo rm /etc/modprobe.d/bcm4360-open.conf` and a reboot.

Don't use `modprobe -r` to reload the driver. It also unloads `cfg80211`,
and wpa_supplicant then can't use Wi‑Fi until it is restarted.

## Settings

Module parameters go in a file in `/etc/modprobe.d/` (as in step 2 above)
and take effect after a reboot.

| Parameter | Default | What it does |
|---|---|---|
| `prefer_5ghz` | 15 | Bonus in dB for 5 GHz access points when joining; 0 picks by signal only. Can also be changed at runtime in `/sys/module/bcm4360/parameters/`. |
| `open_phy`, `open_mac`, `open_dma`, `open_pmu`, `open_si`, `open_srom`, `open_nvram`, `open_clm`, `open_chan`, `open_ppr`, `open_rate`, `open_stf`, `open_stf_policy`, `open_cca`, `open_iovar`, `open_access`, `open_utils` | 1 | One switch per open layer; 0 runs Broadcom's code for that layer. `modinfo bcm4360` describes each. |

`bcm4360_open` (`options bcm4360_open ...`):

| Parameter | Default | What it does |
|---|---|---|
| `nohwcrypt` | 0 | 1: all encryption in software (mac80211) instead of the chip's engine. |
| `ampdu` | 1 | 0: no A-MPDU aggregation. |
| `powersave` | 0 | 1: offer 802.11 power save (not tested on the card yet). |
| `mpc` | 0 | 1: power the radio down while idle (not tested on the card yet). |
| `trace` | 0 | Log every bring-up step and wait this many ms after it, to find where a card hangs. |

## Known limitations

- **`bcm4360` links Broadcom's 2015 core.** The kernel logs one `Unpatched
  return thunk` warning per boot for it, as for the stock `wl`.
  `bcm4360_open` doesn't link it.
- **Client mode only** (and ad‑hoc/IBSS): no access point, monitor mode or
  Wi‑Fi Direct.
- **Security:** open networks, WPA/WPA2‑Personal and WPA/WPA2‑Enterprise
  (802.1X, e.g. eduroam). With `bcm4360`, networks that accept only WPA3,
  or that require management frame protection, don't work. With
  `bcm4360_open`, WPA2-Personal is tested; 802.1X and WPA3 are not yet.
- **Roaming:** with `bcm4360`, Broadcom's core roams; wpa_supplicant picks
  the access point to join, and with a BSSID fixed in the profile the core
  does not roam. With `bcm4360_open`, wpa_supplicant and mac80211 roam.
- **2.4 GHz on busy enterprise networks** (seen on Cisco and Meraki) can stall
  while staying associated. Broadcom's complete `wl` driver does the same on
  those channels. The watchdog reconnects after 30–60 s. To avoid it, keep the
  profile on 5 GHz: `nmcli con modify NAME 802-11-wireless.band a`.
- The blocklist also blocks `bcma`. Don't install on a machine that needs it
  for another Broadcom device.

## How it is made

```
src/        Linux side (cfg80211): Broadcom's wl source with this project's fixes
open/       open hardware layer: PHY and radio, MAC, DMA, backplane, power, SROM, ...
hybrid/     adapters that let open/ take over functions of Broadcom's code at link time
drv/        bcm4360_open, the open mac80211 driver (with open/core/)
scripts/    install, uninstall, download of Broadcom's core, firmware data extractor
systemd/    watchdog and fallback service
modprobe.d/ blocklists: the other Broadcom drivers; bcm4360 when bcm4360_open is the driver
tests/      tests that need neither root nor the card (make check; CI runs them too)
```

`dkms.conf` builds `bcm4360`, `dkms-open.conf` `bcm4360_open` (DKMS package
`bcm4360-open`). The version is set in `dkms.conf` only.

Analysts (AI agents) studied Broadcom's code and wrote specifications that
describe its behaviour, never its code. The open code was written from those
specifications alone, by agents that never saw Broadcom's code. Every open
layer then had to do exactly what Broadcom's does in an emulator, compared
register access by register access, before it was tested on the card. The
specifications, the emulator and the test scripts are in this repository's
history (up to 1.1.0 and commit `6a58d4e`). The comments cite the
specifications by path (`docs/re/spec/frames.md`, or just `frames.md 3.2`);
read them there, e.g. `git show 6a58d4e:docs/re/spec/frames.md`.

`src/` can be rebuilt from public sources: Broadcom's
`hybrid-v35_64-nodebug-pcoem-6_30_223_271.tar.gz` (SHA‑256 `5f79774d…144be`),
Arch Linux's `broadcom-wl-dkms` patches 001–023 (commit `281691a`), and
this project's changes (`patches/0001-bcm4360-fixes.patch` in the history up
to 1.1.0).
