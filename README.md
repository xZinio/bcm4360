# bcm4360: Wi‑Fi driver for the Broadcom BCM4360

A Linux driver for the Broadcom **BCM4360** 802.11ac Wi‑Fi chip, found in the
2013–2017 MacBook Air. It started as Broadcom's `wl` driver, repaired for
current kernels, and is being replaced piece by piece with open code. It
installs through DKMS, so it is rebuilt automatically for every new kernel.

## What's new

- **Open code instead of Broadcom's.** Since 1.1.0 most of Broadcom's code
  for this chip runs as open code, written from specifications. This
  includes the radio and PHY (AC‑PHY): the part no open driver had working,
  since the in‑kernel `b43` driver marks its BCM4360 support as broken. The
  MAC hardware layer, DMA, clocks and power, board configuration and
  regulatory lookup are open too. In a bring‑up with channel changes,
  measured in an emulator, only 37 % of the Broadcom code that used to run
  still runs. Counting how often each piece runs, 99 % of what the driver
  executes is open code. Each open layer has a switch to fall back to
  Broadcom's code (see [hybrid/README.md](hybrid/README.md)).
- **Fewer problems than Broadcom's `wl`.** Connections and roams are
  reported to the kernel correctly. eduroam works through NetworkManager.
  Wi‑Fi no longer dies until a reboot after a failed roam, and scans no
  longer get stuck after a dropped link. The memory-safety warning at every
  boot is gone, `iw` shows correct values, and normal users can query the
  interface. Full list: [docs/fixes.md](docs/fixes.md).
- **Recovers by itself.** A watchdog reloads the driver if Wi‑Fi stays dead.
  A fallback service loads `wl` if the driver failed to build for a new kernel.
  On networks with both bands, the driver prefers 5 GHz access points, which
  are more reliable with this chip.

Still Broadcom's: the upper 802.11 layer (joining networks, scanning,
aggregation, rate selection, keys) and the chip's microcode and data tables.
Replacing the upper layer is the next step; see
[docs/re/07-open-driver.md](docs/re/07-open-driver.md).

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

**2. Install the build tools** (kernel headers, compiler, DKMS, curl):

```sh
sudo pacman -S --needed base-devel linux-headers dkms curl git           # Arch
sudo apt install build-essential linux-headers-$(uname -r) dkms curl git # Debian, Ubuntu
sudo dnf install kernel-devel make gcc dkms curl git                     # Fedora
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

This builds the module (half a minute), installs it, and blocks the
other Broadcom drivers that would grab the card (`b43`, `bcma`, `ssb`,
`brcm*`, `wl`). It also sets up the watchdog and the fallback service.

**4. Reboot.** Or switch without rebooting. Wi‑Fi drops for about 10
seconds, and if the new driver doesn't reconnect, the old one comes back:

```sh
sudo systemd-run --unit=bcm4360-swap --collect /bin/bash "$PWD/scripts/swap-test.sh" bcm4360
sudo cat /var/log/bcm4360-swap-test.log     # ends with RESULT: PASS
```

**5. Check it:**

```sh
sudo bash scripts/verify.sh     # link, DHCP, internet, DNS, scan, kernel log
```

### Update

```sh
cd bcm4360 && git pull && sudo sh scripts/install.sh
```

Then reboot, or switch with the `swap-test.sh` command from step 4.

### Uninstall

```sh
sudo sh scripts/uninstall.sh && sudo reboot
```

This removes the driver, the blocklist, the watchdog and the fallback
service. Your distribution's `wl` driver, if installed, takes over again.

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

Don't use `modprobe -r` to reload the driver. It also unloads `cfg80211`,
and wpa_supplicant then can't use Wi‑Fi until it is restarted.

To see how the driver is doing over days, `scripts/wifi-daily.py` logs the
connection as your normal user and reports outages per network and band:

```sh
python3 scripts/wifi-daily.py enable          # starts logging, also at every login
python3 scripts/wifi-daily.py report --days 7
```

## Settings

Module parameters go in a file in `/etc/modprobe.d/` (as in step 2 above)
and take effect after a reboot.

| Parameter | Default | What it does |
|---|---|---|
| `prefer_5ghz` | 15 | Bonus in dB for 5 GHz access points when joining; 0 picks by signal only. Can also be changed at runtime in `/sys/module/bcm4360/parameters/`. |
| `open_phy`, `open_mac`, `open_dma`, … | 1 | One switch per open layer; 0 runs Broadcom's code for that layer. The full list is in [hybrid/README.md](hybrid/README.md). |

## Known limitations

- **Broadcom's code is still linked in**, so the kernel is marked as tainted,
  and logs one `Unpatched return thunk` warning per boot. The stock `wl` does
  the same.
- **Client mode only** (and ad‑hoc/IBSS): no access point, monitor mode or
  Wi‑Fi Direct.
- **Security:** open networks, WPA/WPA2‑Personal and WPA/WPA2‑Enterprise
  (802.1X, e.g. eduroam). Networks that accept only WPA3, or that require
  management frame protection, don't work.
- **Roaming** between access points is done by Broadcom's core.
  wpa_supplicant only picks the network.
- **2.4 GHz on busy enterprise networks** (seen on Cisco and Meraki) can stall
  for 20–60 seconds. It recovers by itself, and the 5 GHz preference avoids
  most of it.
- The blocklist also blocks `bcma` and `ssb`. Don't install on a machine that
  needs them for another Broadcom device.

## How it is made

```
src/       Linux side (cfg80211): Broadcom's wl source with this project's fixes
open/      open hardware layer: PHY and radio, MAC, DMA, backplane, power, SROM, ...
hybrid/    adapters that let open/ take over functions of Broadcom's code at link time
docs/re/   specifications, method and status of the reverse engineering
tools/re/  emulator that runs Broadcom's code and the open code side by side
scripts/   install, uninstall, swap-test, verify, card tests, logging
```

Analysts (AI agents) studied Broadcom's code and wrote specifications that
describe its behaviour, never its code. The open code was written from those
specifications alone, by agents that never saw Broadcom's code. Every open
layer then had to do exactly what Broadcom's does in an emulator, compared
register access by register access, before it was tested on the card. The
method is in [docs/re/](docs/re/README.md).

`src/` can be rebuilt from public sources: Broadcom's
`hybrid-v35_64-nodebug-pcoem-6_30_223_271.tar.gz` (SHA‑256 `5f79774d…144be`),
Arch Linux's `broadcom-wl-dkms` patches 001–023 (commit `281691a`), and
`patches/0001-bcm4360-fixes.patch`.

## License and credits

Everything in this repository is under the ISC license (`LICENSE`).
Broadcom's code is not included. It is downloaded at install time, stays under
Broadcom's own license, and that license is installed to
`/usr/share/licenses/bcm4360/`.

Thanks to Broadcom (the original driver), the broadcom‑wl and Arch Linux
maintainers (kernel compatibility patches), and the Linux wireless developers
(cfg80211, and the `brcmfmac` driver used as a reference).
