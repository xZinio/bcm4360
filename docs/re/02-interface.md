# The interface between the open glue and Broadcom's core

The object is self-contained apart from 85 imports, and the glue uses 39 of
its 41 exported functions. Because `src/` is open, both sides of the
interface are known exactly: the prototypes below are those of the headers in
`src/`, the behaviour of the imports is the code in `src/shared/linux_osl.c`
and `src/wl/sys/wl_linux.c`.

This interface matters in two ways:

* It is what a **drop-in replacement** of the object would have to implement
  (the exports) and could rely on (the imports).
* It is the complete boundary for **emulation**: the object never touches the
  kernel or the hardware except through the imports, so it can be executed
  outside a kernel with the imports replaced (`tools/re/emu.py`, `osl.py`).

## Imports (85)

### Hardware access

| Import | Prototype | Notes |
|---|---|---|
| `osl_readb/w/l` | `uint8/16/32 osl_readX(volatile uintX *r)` | every register read |
| `osl_writeb/w/l` | `void osl_writeX(uintX v, volatile uintX *r)` | value first |
| `osl_pci_read_config` | `uint32 (osl_t *osh, uint offset, uint size)` | |
| `osl_pci_write_config` | `void (osl_t *osh, uint offset, uint size, uint val)` | |
| `osl_pci_bus`, `osl_pci_slot` | `uint (osl_t *osh)` | used for the device path `pci/<bus>/<slot>/` of variables |
| `osl_reg_map`, `osl_reg_unmap` | `void *(uint32 pa, uint size)` | not used on PCI |
| `osl_pcmcia_read_attr`, `osl_pcmcia_write_attr` | | not used on PCI |
| `osl_uncached`, `osl_cached` | `void *(void *va)` | identity |
| `wl_pcie_bar1` | `uint32 (struct wl_info *wl, uchar **addr)` | address and size of BAR1 (memory of the on-chip ARM) |
| `wl_osl_pcie_rc` | `int (struct wl_info *wl, uint op, int param)` | returns 0 |

A small number of accesses do not go through these imports: the DMA
descriptor rings and packet data are ordinary memory, and a few places read a
register by dereferencing the mapped address directly (none on the BCM4360
path seen so far; the emulator leaves the register window unmapped to catch
such reads).

### Memory, strings, printing, time

`osl_malloc(osh, size)`, `osl_mfree(osh, addr, size)`, `osl_malloced(osh)`,
`osl_memcpy`, `osl_memmove`, `osl_memset`, `osl_memcmp`, `osl_strlen`,
`osl_strcmp`, `osl_strncmp`, `osl_strcpy`, `osl_strncpy`, `osl_strchr`,
`osl_strrchr`, `osl_printf`, `osl_sprintf`, `osl_snprintf`, `osl_vsnprintf`,
`osl_delay(usec)` (busy wait), `osl_sysuptime()` (milliseconds),
`g_assert_type` (a variable, not a function).

### DMA

`osl_dma_alloc_consistent(osh, size, align_bits, &allocated, &pa)`,
`osl_dma_free_consistent`, `osl_dma_map(osh, va, size, direction, pkt, segmap)`
returning the bus address, `osl_dma_unmap`, `wl_alloc_dma_resources`
(returns true).

### Packets

The object never looks into a `struct sk_buff`; it uses accessors:
`osl_pktget(osh, len)`, `osl_pktdup`, `osl_pktfree(osh, p, send)`,
`osl_pktdata`, `osl_pktlen`, `osl_pktsetlen`, `osl_pktheadroom`,
`osl_pkttailroom`, `osl_pktpush`, `osl_pktpull`, `osl_pktnext`,
`osl_pktsetnext`, `osl_pktlink`, `osl_pktsetlink`, `osl_pktprio`,
`osl_pktsetprio`, `osl_pkttag` (32 bytes of per-packet driver data: the
`cb` of the sk_buff), `osl_pktshared`, `osl_pktalloced`.

The first bytes of the opaque `osl_t` are public (`osl_pubinfo_t` in
`linux_osl.h`): the object stores its transmit completion callback there and
`osl_pktfree()` calls it for packets freed with `send` set.

### Files

`osl_os_open_image(name)`, `osl_os_get_image_block(buf, len, image)`,
`osl_os_close_image(image)`. At attach `nvram_init()` asks for a file named
`nvram.txt` (variables in `name=value` form that are used in addition to the
SROM contents). Nothing else is loaded from files: microcode and tables come
from inside the object.

### Imports a session actually uses

In an emulated session (attach, up, channel changes, watchdog, down) 42 of the
85 imports are called. The most frequent: `osl_memcmp` (114,000 calls, mostly
the regulatory database), `osl_writel` (25,000), `osl_writew` (8,900),
`osl_readw` (4,400), `osl_strcmp` (4,100), `osl_delay` (3,800), `osl_readl`
(2,100). The core creates 21 timers at attach; only `watchdog` (1 s, periodic)
is armed when the interface comes up.

### Callbacks into the glue

| Import | What the glue does |
|---|---|
| `wl_init(wl)` | `wl_reset(wl)`, then `wlc_init(wl->wlc)` |
| `wl_reset(wl)` | interrupts off, `wlc_reset(wl->wlc)`, interrupts restored |
| `wl_up(wl)` | `wlc_up(wl->wlc)` unless already up, then opens the transmit queues |
| `wl_down(wl)` | stops the queues, `wlc_down(wl->wlc)`, waits for pending callbacks |
| `wl_intrson(wl)`, `wl_intrsoff(wl)`, `wl_intrsrestore(wl, mask)` | the `wlc_intrs*` function under the interrupt lock |
| `wl_init_timer(wl, fn, arg, name)`, `wl_add_timer(wl, t, ms, periodic)`, `wl_del_timer`, `wl_free_timer` | kernel timers; the callback runs under the driver lock |
| `wl_sendup(wl, wlif, p, numpkt)` | received frame (Ethernet format) to the network stack |
| `wl_txflowcontrol(wl, wlif, state, prio)` | stop/wake the transmit queue |
| `wl_event(wl, ifname, e)`, `wl_event_sync` | driver events (link, scan complete, ...) to the cfg80211 glue |
| `wl_ifname(wl, wlif)` | interface name for messages |
| `wl_monitor`, `wl_set_monitor` | monitor interface |
| `wl_dump_ver` | version line |

## Exports (41)

Entry points used by the glue (prototypes in `src/wl/sys/wlc_pub.h`):

| Export | Use |
|---|---|
| `wlc_chipmatch(vendor, device)` | probe: is this PCI id supported |
| `wlc_attach(wl, vendor, device, unit, piomode, osh, regsva, bustype, btparam, &err)` | create the driver instance; returns the `wlc` handle |
| `wlc_detach(wlc)` | |
| `wlc_pub(wlc)` | public state (`wlc_pub_t`) |
| `wlc_up`, `wlc_down`, `wlc_init`, `wlc_reset` | |
| `wlc_isr(wlc, &wantdpc)`, `wlc_dpc(wlc, bounded, &info)`, `wlc_intrson`, `wlc_intrsoff`, `wlc_intrsrestore`, `wlc_intrsupd` | interrupt handling |
| `wlc_sendpkt(wlc, sdu, wlcif)` | transmit an Ethernet frame |
| `wlc_ioctl(wlc, cmd, arg, len, wlcif)` | all configuration, see below |
| `wlc_iovar_op`, `wlc_iovar_getint`, `wlc_iovar_setint`, `wlc_get`, `wlc_set` | named variables, integer ioctls |
| `wlc_module_register`, `wlc_module_unregister` | the glue registers its watchdog |
| `wlc_wlcif_get_by_index`, `wlc_wlcif_stats_get` | interfaces |
| `wlc_wowl_wake_reason_process` | resume |
| `si_pci_sleep`, `si_pci_pmeclr` | suspend/resume |
| `getvar(vars, name)` | variables made from the SROM |
| `bcm_strtoul`, `bcm_parse_tlvs`, `bcm_mkiovar`, `bcm_bprintf`, `bcm_qdbm_to_mw`, `bcm_mw_to_qdbm`, `pktsetprio`, `wf_chspec_ctlchan`, `wf_mhz2channel`, `wf_channel2mhz` | utilities |

Exported but not called by the glue: `wlc_statsupd`, `wlc_calloc`.

### The configuration interface the glue relies on

The cfg80211 and wireless-extensions code in `src/` configures the core only
through `wlc_ioctl()` and named variables. A drop-in replacement would have to
implement exactly these (a driver built on mac80211 would not need any of it).

45 ioctls: `WLC_DISASSOC`, `WLC_GET_AP`, `WLC_GET_AUTH`, `WLC_GET_BANDLIST`,
`WLC_GET_BSS_INFO`, `WLC_GET_BSSID`, `WLC_GET_CHANNEL`, `WLC_GET_CURR_RATESET`,
`WLC_GET_INFRA`, `WLC_GET_KEY`, `WLC_GET_KEY_PRIMARY`, `WLC_GET_LRL`,
`WLC_GET_MONITOR`, `WLC_GET_PHY_NOISE`, `WLC_GET_PHYLIST`, `WLC_GET_PHYTYPE`,
`WLC_GET_PM`, `WLC_GET_RADIO`, `WLC_GET_RATE`, `WLC_GET_RSSI`, `WLC_GET_SRL`,
`WLC_GET_SSID`, `WLC_GET_VALID_CHANNELS`, `WLC_GET_VAR`, `WLC_GET_WSEC`,
`WLC_REASSOC`, `WLC_SCAN`, `WLC_SCAN_RESULTS`,
`WLC_SCB_DEAUTHENTICATE_FOR_REASON`, `WLC_SET_AP`, `WLC_SET_AUTH`,
`WLC_SET_CHANNEL`, `WLC_SET_INFRA`, `WLC_SET_KEY`, `WLC_SET_KEY_PRIMARY`,
`WLC_SET_LRL`, `WLC_SET_MONITOR`, `WLC_SET_PASSIVE_SCAN`, `WLC_SET_PM`,
`WLC_SET_PROMISC`, `WLC_SET_RADIO`, `WLC_SET_RATESET`, `WLC_SET_SRL`,
`WLC_SET_SSID`, `WLC_SET_VAR`.

35 variables: `a_rate`, `allmulti`, `assoc_info`, `assoc_req_ies`,
`assoc_resp_ies`, `auth`, `bg_rate`, `chanspec`, `counters`, `cur_etheraddr`,
`fragthresh`, `leddc`, `mcast_list`, `mimo_bw_cap`, `mpc`, `nmode`,
`offloads`, `pmkid_info`, `qtxpower`, `rtsthresh`, `rx_unencrypted_eapol`,
`scan_passive_time`, `sgi_tx`, `tkip_countermeasures`, `toe`, `toe_ol`,
`vlan_mode`, `wowl`, `wowl_activate`, `wowl_replay`, `wowl_wakeind`,
`wpa_auth`, `wpaie`, `wsec`, `wsec_restrict`.

Behind these the object implements a complete 802.11 station: scanning,
authentication and association, roaming, the WPA/WPA2 4-way handshake
(`wlc_sup.c`), power save, rate selection, aggregation. With mac80211 all of
that exists in the kernel; what a mac80211 driver needs from this object is
the part below `wlc_bmac.c` plus the frame formats of the MAC core, see
[README.md](README.md).

## A remark on `SIOCDEVPRIVATE`

`wl_ioctl()` in `wl_linux.c` is installed as `ndo_do_ioctl`. Since Linux 5.15
private ioctls from user space are delivered through `ndo_siocdevprivate`
instead, so tools that talk to the driver with `SIOCDEVPRIVATE` (Broadcom's
`wl` utility) have no way in on current kernels. This was not tested on the
target machine.
