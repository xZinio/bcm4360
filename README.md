# bcm4360: Wi‑Fi driver for the Broadcom BCM4360

A Linux driver for the Broadcom **BCM4360** 802.11ac Wi‑Fi chip, found in the
2013–2017 MacBook Air. It started as Broadcom's `wl` driver, repaired for
current kernels, and is being replaced piece by piece with open code. It
installs through DKMS, so it is rebuilt automatically for every new kernel.

## What's new

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
Replacing the upper layer is the next step: `bcm4360_open`, a mac80211
driver without any of Broadcom's code, is in `drv/` and `open/core/`. It is
**experimental and not installed**. Its last test on the card froze the
whole machine during bring‑up, so don't load it.

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

**4. Reboot.** Or switch without rebooting (Wi‑Fi drops for about 10
seconds):

```sh
sudo rmmod bcm4360 2>/dev/null; sudo modprobe bcm4360; sudo systemctl restart wpa_supplicant
```

**5. Check it:**

```sh
cat /sys/module/bcm4360/version      # the version you installed
iw dev wlan0 link                    # access point, band, signal
journalctl -k -b | grep bcm4360      # no WARNING or Call Trace
```

### Update

```sh
cd bcm4360 && git pull && sudo sh scripts/install.sh
```

Then reboot, or switch with the command from step 4.

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

## Settings

Module parameters go in a file in `/etc/modprobe.d/` (as in step 2 above)
and take effect after a reboot.

| Parameter | Default | What it does |
|---|---|---|
| `prefer_5ghz` | 15 | Bonus in dB for 5 GHz access points when joining; 0 picks by signal only. Can also be changed at runtime in `/sys/module/bcm4360/parameters/`. |
| `open_phy`, `open_mac`, `open_dma`, `open_pmu`, `open_si`, `open_srom`, `open_nvram`, `open_clm`, `open_chan`, `open_ppr`, `open_rate`, `open_stf`, `open_stf_policy`, `open_cca`, `open_iovar`, `open_access`, `open_utils` | 1 | One switch per open layer; 0 runs Broadcom's code for that layer. `modinfo bcm4360` describes each. |

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
  wpa_supplicant picks the access point to join; with a BSSID fixed in the
  profile, the core does not roam.
- **2.4 GHz on busy enterprise networks** (seen on Cisco and Meraki) can stall
  while staying associated. Broadcom's complete `wl` driver does the same on
  those channels. The watchdog reconnects after 30–60 s. To avoid it, keep the
  profile on 5 GHz: `nmcli con modify NAME 802-11-wireless.band a`.
- The blocklist also blocks `bcma` and `ssb`. Don't install on a machine that
  needs them for another Broadcom device.

## How it is made

```
src/        Linux side (cfg80211): Broadcom's wl source with this project's fixes
open/       open hardware layer: PHY and radio, MAC, DMA, backplane, power, SROM, ...
hybrid/     adapters that let open/ take over functions of Broadcom's code at link time
drv/        bcm4360_open, the open mac80211 driver (experimental, with open/core/)
scripts/    install, uninstall, download of Broadcom's core, firmware data extractor
systemd/    watchdog and fallback service
modprobe.d/ blocklist of the other Broadcom drivers
```

Analysts (AI agents) studied Broadcom's code and wrote specifications that
describe its behaviour, never its code. The open code was written from those
specifications alone, by agents that never saw Broadcom's code. Every open
layer then had to do exactly what Broadcom's does in an emulator, compared
register access by register access, before it was tested on the card. The
specifications, the emulator and the test scripts are in this repository's
history (up to 1.1.0 and commit `6a58d4e`).

`src/` can be rebuilt from public sources: Broadcom's
`hybrid-v35_64-nodebug-pcoem-6_30_223_271.tar.gz` (SHA‑256 `5f79774d…144be`),
Arch Linux's `broadcom-wl-dkms` patches 001–023 (commit `281691a`), and
this project's changes (`patches/0001-bcm4360-fixes.patch` in the history up
to 1.1.0).

## License and credits

Everything in this repository is under the ISC license (`LICENSE`).
Broadcom's code is not included. It is downloaded at install time, stays under
Broadcom's own license, and that license is installed to
`/usr/share/licenses/bcm4360/`.

Thanks to Broadcom (the original driver), the broadcom‑wl and Arch Linux
maintainers (kernel compatibility patches), and the Linux wireless developers
(cfg80211, and the `brcmfmac` driver used as a reference).
