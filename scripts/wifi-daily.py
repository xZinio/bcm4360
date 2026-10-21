#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
"""
Daily-use Wi-Fi log for the BCM4360 (bcm4360 or wl), run as the normal user.

    wifi-daily.py enable          log now and at every login (systemd user service)
    wifi-daily.py disable         stop logging
    wifi-daily.py status          is it logging, how much has been recorded
    wifi-daily.py report [--days N | --since 'YYYY-MM-DD[ HH:MM]'] [--network SSID]
    wifi-daily.py run             the logger itself (what the service runs)

Every 10 s (every 2 s while offline) it records the driver, the radio,
NetworkManager's state and connection, the access point (BSSID, band, signal,
bit rate) and - every 20 s and after every change - whether the internet
answers over the Wi-Fi interface (a connectivity-check request, under 2 MB a
day). It only watches: it never connects, disconnects or scans. Suspends are
detected and left out, as are periods with the radio off or after a deliberate
disconnect. eduroam's routine re-login every 30 minutes (RADIUS session
timeout) is recognised and reported apart. Data: ~/.local/state/bcm4360-daily/.
"""
import argparse
import datetime as dt
import glob
import json
import os
import re
import subprocess
import sys
import time

INTERVAL = 10		# seconds between samples
OFFLINE_INTERVAL = 2	# ... while not online
NET_EVERY = 20		# seconds between reachability checks while nothing changes
# Connectivity-check endpoints (Android's, then NetworkManager's on Arch): the
# internet counts as reachable if either answers as expected. Not 1.1.1.1:
# campus networks may block HTTPS to public DNS resolvers.
CHECKS = (('http://www.google.com/generate_204', '204'),
	  ('http://ping.archlinux.org/nm-check.txt', '200'))
UNIT = 'bcm4360-daily'
DIR = os.path.expanduser('~/.local/state/bcm4360-daily')


def sh(*cmd, timeout=10):
	try:
		return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout).stdout
	except (OSError, subprocess.TimeoutExpired):
		return ''


def wifi_iface():
	for d in sorted(glob.glob('/sys/class/net/*')):
		if os.path.exists(os.path.join(d, 'phy80211')):
			return os.path.basename(d)
	return None


def driver():
	for mod in ('bcm4360', 'wl'):
		if os.path.isdir('/sys/module/' + mod):
			try:
				ver = open('/sys/module/%s/version' % mod).read().strip()
			except OSError:
				ver = ''
			return (mod + ' ' + ver).strip()
	return 'none'


def sample(iface):
	r = {'t': round(time.time(), 1), 'drv': driver(), 'if': iface}
	r['radio'] = sh('nmcli', '-g', 'WIFI', 'general').strip()
	if iface:
		f = sh('nmcli', '-g', 'GENERAL.STATE,GENERAL.CONNECTION,GENERAL.AUTOCONNECT',
		       'device', 'show', iface).splitlines() + ['', '', '']
		m = re.match(r'(\d+)', f[0])
		r['state'] = int(m.group(1)) if m else 0
		r['conn'] = f[1]
		r['auto'] = f[2]
		link = sh('iw', 'dev', iface, 'link')
		for key, pat, conv in (('bssid', r'Connected to (\S+)', str),
				       ('ssid', r'SSID: (.*)', str),
				       ('freq', r'freq: ([\d.]+)', float),
				       ('sig', r'signal: (-?\d+)', int),
				       ('rate', r'tx bitrate: ([\d.]+)', float)):
			m = re.search(pat, link)
			if m:
				r[key] = conv(m.group(1).strip())
	return r


def check_net(iface):
	for url, code in CHECKS:
		out = sh('curl', '-s', '-o', '/dev/null', '-w', '%{http_code} %{time_total}',
			 '--connect-timeout', '3', '--max-time', '6', '--interface', iface, url,
			 timeout=10).split()
		if len(out) == 2 and out[0] == code:
			return True, round(float(out[1]), 3)
	return False, None


def run(args):
	os.makedirs(args.dir, exist_ok=True)
	last_mono = last_boot = None
	last_key = None
	last_net_t = 0
	last_net_ok = True
	while True:
		mono = time.monotonic()
		boot = time.clock_gettime(time.CLOCK_BOOTTIME)
		iface = wifi_iface()
		r = sample(iface)
		if last_boot is not None and (boot - last_boot) - (mono - last_mono) > 3:
			r['suspended'] = round((boot - last_boot) - (mono - last_mono))
		key = (r.get('state'), r.get('conn'), r.get('bssid'), r['drv'])
		if iface and (key != last_key or not last_net_ok or mono - last_net_t >= NET_EVERY
			      or 'suspended' in r):
			r['net'], r['lat'] = check_net(iface)
			last_net_t, last_net_ok = mono, r['net']
		last_key, last_mono, last_boot = key, mono, boot
		day = dt.datetime.fromtimestamp(r['t']).strftime('%Y-%m-%d')
		with open(os.path.join(args.dir, 'samples-%s.jsonl' % day), 'a') as f:
			f.write(json.dumps(r, separators=(',', ':')) + '\n')
		# every 2 s while offline, so that short outages are timed well
		offline = not (r.get('state') == 100 and r.get('bssid') and last_net_ok)
		step = OFFLINE_INTERVAL if offline else args.interval
		time.sleep(max(1.0, step - (time.monotonic() - mono)))


# ---------------------------------------------------------------- report

def band(freq):
	return None if not freq else ('2.4 GHz' if freq < 3000 else '5 GHz')


def load(d, since, until):
	recs = []
	for path in sorted(glob.glob(os.path.join(d, 'samples-*.jsonl'))):
		day = os.path.basename(path)[8:18]
		if day < dt.datetime.fromtimestamp(since).strftime('%Y-%m-%d'):
			continue
		with open(path) as f:
			for line in f:
				try:
					r = json.loads(line)
				except ValueError:
					continue
				if since <= r['t'] <= until:
					recs.append(r)
	recs.sort(key=lambda r: r['t'])
	return recs


def hms(s):
	s = int(round(s))
	if s < 60:
		return '%ds' % s
	if s < 3600:
		return '%dm%02ds' % (s // 60, s % 60)
	return '%dh%02dm' % (s // 3600, s % 3600 // 60)


def journal_counts(since, until):
	pats = [
		('driver: join failed', r'bcm4360: \S+: join failed'),
		('driver: stuck scan ended', r'bcm4360: \S+: scan not completed'),
		('driver: firmware rejoined on its own', r'bcm4360: \S+: firmware rejoined'),
		('driver: failed roam, old link reported lost', r'bcm4360: \S+: joining another AP failed'),
		('watchdog: driver reloaded', r'reloading bcm4360'),
		('fallback: wl loaded at boot', r'bcm4360 is not available for kernel'),
		('NetworkManager: activation failed', r'Activation: failed for connection'),
		('wpa_supplicant: link lost (reason=0)', r'CTRL-EVENT-DISCONNECTED .*reason=0'),
		# less the core's "Unpatched return thunk" notice at every boot
		('kernel WARNING', r'WARNING: (?!arch/x86/kernel/cpu/bugs\.c)'),
	]
	log = sh('journalctl', '--since', '@%d' % since, '--until', '@%d' % until,
		 '--no-pager', '-o', 'cat', timeout=60)
	return [(name, len(re.findall(p, log))) for name, p in pats]


def report(args):
	until = time.time()
	if args.since:
		fmt = '%Y-%m-%d %H:%M' if ' ' in args.since else '%Y-%m-%d'
		since = dt.datetime.strptime(args.since, fmt).timestamp()
	else:
		since = until - args.days * 86400
	recs = load(args.dir, since, until)
	if len(recs) < 2:
		sys.exit('no data in that period (is the logger running? wifi-daily.py status)')

	gap_limit = 3 * INTERVAL
	wanted = online = off = deliberate = unmonitored = 0.0
	per_net = {}		# ssid -> dict
	outages = []		# dicts
	cur = None		# outage in progress
	last_on = None		# last online sample
	net_ok = True
	roams = 0
	drivers = {}
	on_since = None		# start of the current stretch online on one network

	def net_of(r):
		return r.get('ssid') or r.get('conn') or '?'

	def associated(r):
		# NetworkManager stays "connected" for up to 15 s after the link
		# went; the link itself (iw) says whether the card is on an AP
		return r.get('state') == 100 and bool(r.get('bssid'))

	def close(end, after, why=None):
		nonlocal cur
		if cur:
			cur['end'], cur['after'], cur['why'] = end, after, why
			outages.append(cur)
			cur = None

	for a, b in zip(recs, recs[1:]):
		if 'net' in a:
			net_ok = a['net']
		span = b['t'] - a['t']
		if span > gap_limit or 'suspended' in b:
			unmonitored += span
			close(a['t'], None, 'suspend or logger stopped')
			on_since = None
			continue
		drivers[a['drv']] = drivers.get(a['drv'], 0) + span
		if a.get('radio') != 'enabled':
			off += span
			close(a['t'], None, 'radio switched off')
			on_since = None
			continue
		if a.get('state') != 100 and a.get('auto') == 'no':
			deliberate += span
			close(a['t'], None, 'disconnected on purpose')
			on_since = None
			continue
		wanted += span
		is_on = associated(a) and net_ok
		if is_on:
			online += span
			n = per_net.setdefault(net_of(a), {'on': 0.0, 'bands': {}, 'drops': 0,
							    'drop_bands': {}, 'sig': {}})
			n['on'] += span
			bd = band(a.get('freq'))
			if bd:
				n['bands'][bd] = n['bands'].get(bd, 0) + span
				n['sig'].setdefault(bd, []).append(a.get('sig'))
			if last_on and net_of(last_on) == net_of(a) and last_on.get('bssid') \
					and a.get('bssid') and last_on['bssid'] != a['bssid'] and not cur:
				roams += 1
			if cur or not last_on or net_of(last_on) != net_of(a) or on_since is None:
				on_since = a['t']
			close(a['t'], a)
			last_on = a
		elif not cur:
			cur = {'start': a['t'], 'before': last_on,
			       'assoc': associated(a),
			       'up_for': a['t'] - on_since if on_since is not None and last_on else None}
		elif not associated(a):
			cur['assoc'] = False
	close(recs[-1]['t'], None, 'end of data')

	for o in outages:
		o['len'] = o['end'] - o['start']
		bef = o['before']
		# eduroam networks commonly end every session after 30 minutes
		# (RADIUS Session-Timeout); that is the network, not the driver
		o['relogin'] = o['up_for'] is not None and 1740 <= o['up_for'] <= 1890
		# a lost link always counts; a failed check while on the AP only
		# from 15 s on (shorter ones are blips, e.g. a slow server)
		o['counted'] = not o['assoc'] or o['len'] >= 15
		if bef and o['counted']:
			n = per_net.get(net_of(bef))
			if n is not None:
				n['drops'] += 1
				n.setdefault('lens', []).append(o['len'])
				if o['relogin']:
					n['relogins'] = n.get('relogins', 0) + 1
				bd = band(bef.get('freq')) or '?'
				n['drop_bands'][bd] = n['drop_bands'].get(bd, 0) + 1

	fmt_t = lambda t: dt.datetime.fromtimestamp(t).strftime('%a %H:%M:%S')
	print('Wi-Fi daily report  %s  ->  %s' % (
		dt.datetime.fromtimestamp(recs[0]['t']).strftime('%Y-%m-%d %H:%M'),
		dt.datetime.fromtimestamp(recs[-1]['t']).strftime('%Y-%m-%d %H:%M')))
	print('driver: ' + ', '.join('%s (%s)' % (d, hms(s)) for d, s in drivers.items()))
	print()
	print('Wi-Fi wanted  %-9s  online %s (%.2f %%)' % (
		hms(wanted), hms(online), 100 * online / wanted if wanted else 0))
	print('not counted   suspended/off-line %s, radio off %s, deliberately disconnected %s' % (
		hms(unmonitored), hms(off), hms(deliberate)))
	counted = [o for o in outages if o['counted']]
	relogins = [o for o in counted if o['relogin']]
	print('outages       %d (%d link lost, %d on the AP without internet), longest %s; '
	      '%d blips under 15 s; roams %d' % (
		len(counted), sum(not o['assoc'] for o in counted), sum(o['assoc'] for o in counted),
		hms(max([o['len'] for o in counted], default=0)), len(outages) - len(counted), roams))
	if args.network:
		per_net = {k: v for k, v in per_net.items() if k == args.network}

	print('\nper network (online time; drops = outages that started on it)')
	for name, n in sorted(per_net.items(), key=lambda kv: -kv[1]['on']):
		hours = n['on'] / 3600
		lens = sorted(n.get('lens', []))
		print('  %-22s online %-8s drops %-3d %s%s%s' % (
			name, hms(n['on']), n['drops'],
			'(%.1f per hour) ' % (n['drops'] / hours) if hours >= 0.25 else '',
			'of them %d after exactly ~30 min (session re-login) ' % n['relogins']
			if n.get('relogins') else '',
			'- typical time to get back online %s' % hms(lens[len(lens) // 2]) if lens else ''))
		for bd in sorted(set(n['bands']) | set(n['drop_bands'])):
			t = n['bands'].get(bd, 0)
			d = n['drop_bands'].get(bd, 0)
			sig = [s for s in n['sig'].get(bd, []) if s is not None]
			print('      %-8s online %-8s drops %-3d %s%s' % (
				bd, hms(t), d,
				'mean signal %d dBm  ' % (sum(sig) / len(sig)) if sig else '',
				'(%.1f per hour)' % (d / (t / 3600)) if t >= 900 else ''))

	routine = [o for o in relogins if o['len'] < 30]
	listed = [o for o in counted if o not in routine]
	if routine:
		lens = sorted(o['len'] for o in routine)
		print('\n%d routine 30-min re-logins, back online after %s (typical %s); not listed below'
		      % (len(routine), ' to '.join(hms(x) for x in (lens[0], lens[-1])),
			 hms(lens[len(lens) // 2])))
	if listed:
		print('\noutages')
		for o in listed:
			b, a = o['before'], o['after']
			where = ('%s %s %s' % (net_of(b), b.get('bssid', ''), band(b.get('freq')) or '')
				 if b else 'not connected before')
			back = ('back on %s %s' % (net_of(a), band(a.get('freq')) or '')
				if a else 'ended by: ' + o['why'])
			kind = 'associated, no internet' if o['assoc'] else 'link lost'
			if o['up_for'] is not None:
				kind += ' after %s' % hms(o['up_for'])
			if o['relogin']:
				kind += ' (30-min re-login)'
			print('  %s  %-7s %-46s on %s; %s' % (
				fmt_t(o['start']), hms(o['len']), kind, where.strip(), back))

	print('\nlog events in this period')
	for name, n in journal_counts(recs[0]['t'], recs[-1]['t']):
		print('  %-44s %d' % (name, n))


# ---------------------------------------------------------------- service

def service(args):
	unit_dir = os.path.expanduser('~/.config/systemd/user')
	unit = os.path.join(unit_dir, UNIT + '.service')
	if args.cmd == 'enable':
		os.makedirs(unit_dir, exist_ok=True)
		with open(unit, 'w') as f:
			f.write('[Unit]\nDescription=Daily-use Wi-Fi log (bcm4360)\n\n'
				'[Service]\nExecStart=%s %s run\nRestart=always\nRestartSec=30\n\n'
				'[Install]\nWantedBy=default.target\n' % (sys.executable, os.path.abspath(__file__)))
		subprocess.run(['systemctl', '--user', 'daemon-reload'], check=True)
		subprocess.run(['systemctl', '--user', 'enable', '--now', UNIT], check=True)
		print('logging to %s (now and at every login); report: %s report' % (args.dir, __file__))
	elif args.cmd == 'disable':
		subprocess.run(['systemctl', '--user', 'disable', '--now', UNIT])
		if os.path.exists(unit):
			os.remove(unit)
		subprocess.run(['systemctl', '--user', 'daemon-reload'])
		print('stopped; the data stays in %s' % args.dir)
	else:
		active = sh('systemctl', '--user', 'is-active', UNIT).strip()
		files = sorted(glob.glob(os.path.join(args.dir, 'samples-*.jsonl')))
		n = sum(1 for p in files for _ in open(p))
		print('logger: %s; %d samples in %d day file(s) in %s' % (active or 'not installed',
								   n, len(files), args.dir))


def main():
	p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	p.add_argument('--dir', default=DIR, help=argparse.SUPPRESS)
	sub = p.add_subparsers(dest='cmd', required=True)
	for c in ('enable', 'disable', 'status'):
		sub.add_parser(c)
	r = sub.add_parser('run')
	r.add_argument('--interval', type=float, default=INTERVAL, help=argparse.SUPPRESS)
	rep = sub.add_parser('report')
	rep.add_argument('--days', type=float, default=1)
	rep.add_argument('--since')
	rep.add_argument('--network')
	args = p.parse_args()
	if args.cmd == 'run':
		run(args)
	elif args.cmd == 'report':
		report(args)
	else:
		service(args)


if __name__ == '__main__':
	main()
