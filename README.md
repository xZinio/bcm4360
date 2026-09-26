# bcm4360 — Broadcom BCM4360 Wi‑Fi for Linux on the MacBook Air

A Linux driver for the Broadcom **BCM4360** 802.11ac chip (PCI `14e4:43a0`) in the
2013–2017 MacBook Air, packaged for DKMS. It is Broadcom's hybrid `wl` driver
with its Linux side repaired for current kernels (tested on 7.2).

## What is open, what is not

| Part | License | Where |
|---|---|---|
| Linux/cfg80211 driver code, build system, scripts | ISC (open source) | this folder |
| Broadcom's 802.11 core `wlc_hybrid.o_shipped` (2015, binary) | Broadcom's proprietary license | **not included**; downloaded from Broadcom at install time, checksum-verified, linked in unmodified |

Why not 100 % open? The only open driver for this chip, the kernel's `b43`,
has its BCM4360 radio support (AC‑PHY) marked **BROKEN** – it crashes. No open
implementation of this radio exists, so Broadcom's core still does the actual
802.11 work. Every fix in this project is in the open part.

## Status

Tested on the target machine: MacBookAir7,2, BCM4360 `14e4:43a0` rev 03 (Apple
subsystem `106b:0117`), Arch Linux, kernel 7.2.7‑arch1‑1, GCC 16.2.1,
NetworkManager 1.58.1, wpa_supplicant 2.12, WPA2‑PSK on 5 GHz (channel 36).

| Test | Result |
|---|---|
| Build against 7.2.7 (plain `make` and DKMS) | builds; no new warnings (7 pre-existing ones in Broadcom's code) |
| Load next to the stock `wl` without binding the card | loads and unloads cleanly |
| Live driver swap (`wl` → bcm4360, then 3 upgrades in place) | 4/4, reconnected within 5 s each time |
| Disconnect/reconnect cycles | 8/8, no `bss_not_found` |
| `verify.sh` (link, DHCP, gateway/internet/DNS, scan, queries as non-root, throughput, kernel log) | 13/13 |
| Boot-time driver selection (udev replay) | udev picks bcm4360 over `bcma` and `wl` |
| Real reboot | bcm4360 loads by itself, Wi‑Fi up 10.8 s after kernel start, 13/13 |
| Suspend to RAM through logind (3×) | Wi‑Fi back 4 s after resume |
| Throughput vs stock `wl`, same minute, 3 × 30 MB each | `wl` 72.0–74.1 Mbit/s, bcm4360 75.7–80.7 Mbit/s (both at the internet line's limit) |
| Link rate | up to 866.5 Mbit/s (VHT80, 2 streams) |

## What is fixed

Compared with Broadcom's `wl` as packaged by Arch (`broadcom-wl-dkms 6.30.223.271-50`),
which was already running on the test machine:

| Symptom with the stock driver | Cause | Fix |
|---|---|---|
| `WARNING … net/wireless/sme.c:845 … bss_not_found`; cfg80211 then **discards the connection result** (roams: `sme.c:1201`, roam dropped) | The driver's own AP lookup passed capability bits where `cfg80211_get_bss()` has taken enums since Linux 4.1 (it searched for a 60 GHz PBSS) and never matched; connect/roam reports then depended on cfg80211 finding scan entries that expire after 30 s | Correct lookup, fresh entry from the firmware, BSSID-only fallback for hidden networks, and the referenced entry handed to `cfg80211_connect_bss()` / `cfg80211_roamed()` |
| `memcpy: detected field-spanning write … wl_cfg80211_hybrid.c:3139` at every boot | Scan results were copied into a fake beacon through a zero-length array | IEs passed straight to `cfg80211_inform_bss()`; this also removes a 1 KiB IE limit, and one bad scan entry no longer hides every network after it |
| `ERROR @wl_cfg80211_get_tx_power: error (-1)` whenever a normal user runs `iw dev` | Every firmware call re-checked `CAP_NET_ADMIN`, although nl80211 already allows read-only queries for everyone | cfg80211 ops rely on nl80211's permission checks; the raw firmware ioctl (`SIOCDEVPRIVATE`) stays root-only |
| `iw` showed milliwatts as dBm; `iw … set txpower` took mBm as milliwatts | Wrong unit conversions | Quarter-dBm conversion, as in the in-kernel `brcmfmac` driver |
| `WLC_SCAN error (-22)` while joining a network | Firmware "busy" came back as `-EINVAL` | `-EBUSY`, so NetworkManager simply retries; a scan whose results can't be fetched is reported as aborted (quietly when it was just cut short, e.g. before suspend) |
| Roams reported the previous AP's association IEs and leaked memory | `roam_info` was filled before the IEs were fetched; old IEs never freed | Filled after fetching; old IEs freed; firmware-reported lengths clamped |
| `iw dev wlan0 station dump` showed nothing | `dump_station` not implemented | Added; `get_station` no longer reports the AP's figures under other addresses |
| Every driver message split over two log lines; blank line after the banner | Two `printk()` calls per message | One record per message, proper log levels |
| Module loaded for any vendor's "network, other" PCI device | Catch-all PCI ID table | BCM4360 IDs only; module named `bcm4360`, so it coexists with the distro's `wl` |

## Install

Needs kernel headers, a compiler, `make`, `dkms` and `curl`. On Arch:

```sh
sudo pacman -S --needed base-devel linux-headers dkms curl
```

Copy this folder to the MacBook, then:

```sh
cd driver
sudo sh scripts/install.sh   # downloads + verifies Broadcom's core, builds via DKMS,
                             # installs, blacklists b43/bcma/ssb/brcm*/wl
sudo reboot                  # bcm4360 takes over at boot
```

To switch without rebooting (Wi‑Fi drops for about 10 s; rolls back to `wl`
automatically if the new driver doesn't reconnect):

```sh
sudo systemd-run --unit=bcm4360-swap --collect /bin/bash "$PWD/scripts/swap-test.sh" bcm4360
sudo cat /var/log/bcm4360-swap-test.log
```

Check the result at any time (as root it also runs a scan and the non-root queries):

```sh
sudo bash scripts/verify.sh
```

DKMS rebuilds the module automatically for new kernels.

### Uninstall

```sh
sudo sh scripts/uninstall.sh   # removes the DKMS module and the blacklist
sudo reboot                    # the distro's wl (if installed) takes over again
```

## Layout

```
Makefile                  Kbuild + `make`, `make fetch`, `make install`
dkms.conf                 DKMS package bcm4360 1.0.0
src/                      Linux/cfg80211 driver code (ISC)
modprobe.d/bcm4360.conf   blacklist of drivers that would grab the card first
patches/                  our changes as one patch against the provenance baseline
scripts/fetch-blob.sh     download + verify Broadcom's core into lib/
scripts/install.sh        DKMS build/install
scripts/uninstall.sh      DKMS removal
scripts/swap-test.sh      live driver swap with end-to-end checks and rollback
scripts/verify.sh         health check
```

## Provenance

`src/` is reproducible from public sources:

1. Broadcom's `hybrid-v35_64-nodebug-pcoem-6_30_223_271.tar.gz`
   (SHA-256 `5f79774d5beec8f7636b59c0fb07a03108eef1e3fd3245638b20858c714144be`)
2. plus Arch Linux's `broadcom-wl-dkms` patches 001–023
   (gitlab.archlinux.org/archlinux/packaging/packages/broadcom-wl-dkms, commit `281691a`)
   and its PKGBUILD's `eth%d` → `wlan%d` edit,
3. plus `patches/0001-bcm4360-fixes.patch` (this project).

Steps 1 and 2 reproduce Arch's installed driver byte for byte; step 3 then
yields exactly `src/`. Both were checked on the test machine.

## Known limitations

- **Proprietary core.** Bugs inside Broadcom's 2015 core can't be fixed here,
  and it was built without today's kernel hardening. The kernel is tainted
  (`P`, `O`, `E`) and logs one `Unpatched return thunk in use` warning per boot
  when the module loads. The stock `wl` does exactly the same: `objtool` can't
  process the core, so it is skipped for the final link, as distributions do.
- **CPUs with Intel CET/IBT** (11th gen and later) are not supported by the core,
  same as the stock `wl`. The MacBook Air's Haswell/Broadwell CPUs don't have it.
- **Modes:** client (managed) and IBSS only; no access point, monitor or P2P.
- **Security:** WPA/WPA2 (personal and enterprise). The 2015 core predates
  WPA3; WPA3-only (SAE) networks are not supported, WPA2/WPA3 transition
  networks connect via WPA2.
- **Roaming aids:** no signal-quality (CQM) events, so wpa_supplicant logs
  `bgscan simple: Failed to enable signal strength monitoring` and uses
  periodic background scans instead.
- **Future kernels** can change cfg80211 interfaces; DKMS rebuilds on every
  kernel update, but a major kernel release may need a source update, as for
  any out-of-tree driver.

## License

Everything in this folder is under the ISC license (see `LICENSE`). Broadcom's
core is under Broadcom's own license, which `scripts/fetch-blob.sh` saves as
`lib/LICENSE.txt` and `install.sh` installs to `/usr/share/licenses/bcm4360/`.

## Credits

Broadcom (original hybrid driver), the broadcom-wl contributors and Arch Linux
maintainers (kernel-compatibility patches), and the Linux wireless developers
whose cfg80211 documentation and `brcmfmac` driver served as reference.
