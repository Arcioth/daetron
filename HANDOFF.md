# nixafnetlink handoff

Started 2026-09-03. WhatsApp away session.

## Status

Scaffold is in `~/Documents/nixafnetlink`. One C binary, `uevent`, binds `NETLINK_KOBJECT_UEVENT` group 1 as an unprivileged user and blocks on `recv`. `--applet` keeps only DJI / Logitech / IMEX add|remove.

Default build is **musl `-static`**. Live processes after 10 min (including the field test):

| process | disk | RSS | PSS | CPU | notes |
|---|---|---|---|---|---|
| `uevent` musl static | 38 KB | **68 KB** | 46 KB | **0 ticks**, cputime 0 | 310 voluntary wakes / 10 min (all kernel uevents) |
| `uevent --applet` | same | **68 KB** | 46 KB | **0 ticks** | 335 wakes |
| glibc dynamic (earlier) | 16 KB | 1492 KB | 134 KB | 0 ticks | shared libc |
| micmini2-watch bash | — | 4784 KB | — | 0.2% | 2s poll, 1d up |
| mother-watch bash | — | 4808 KB | — | 0.2% | 2s poll |
| imex-watch bash | — | 4788 KB | — | 0.0% | 2s poll |

Three bash watchers ≈ 14 MB and a 0–2 s delay. One netlink daemon ≈ 68 KB and event latency.

**Yes: this is the main trigger.** Field test proved add/remove for all three devices. Do not poll. Production is **one** daemon (the two copies now are just the test dump + filter).

Netlink does not replay. At start: one sysfs scan (already plugged), then block on `recv`. Keep DISMISS/STARTED from the bash watchers.

## Modular layout (locked)

Declarative applets: `rules.d/*.rule`. The daemon does **not** hotload and does **not** re-read files on each plug.

```
boot / SIGHUP  →  read rules.d into memory, one sysfs scan
kernel uevent  →  match the in-memory table  →  exec/kill + SIGRTMIN+N waybar
```

- **Load:** start, and `kill -HUP` / `systemctl --user reload nixafnetlink` (or a `uevent reload` that signals the running pid).
- **Not:** inotify on `rules.d/` (extra fd, extra wakes, new applets are rare).
- **Not:** `open()` the rule file on every add/remove.

New popup = drop a file in `rules.d/` + reload. No C edit. Draft rules: `dji`, `logi`, `imex`. IMEX `debounce_ms=500` + `suppress_if_product_prefix=2ca3/,46d/`.

Waybar: drop `interval: 2` on `custom/imex` and `custom/mother`; keep `signal` 6/7. The daemon pokes waybar only on add/remove. Status scripts still run, just not every 2s.

Dispatcher is `./nixafnetlink run`. musl static, ~92 KB disk, ~156 KB RSS dry-run (rules + poll). `--dry-run` prints actions. Do not swap systemd units until a live plug is proven in `run` (not just dump). `contrib/nixafnetlink.service` is an example only.

Adding a device is a `.rule` file + reload. Not a C change. `on_add` is any command (Tauri, Kitty, script).

**Idle daemon vs wake-on-plug:** the kernel only delivers uevents to a bound netlink socket. Something has to hold that fd. Today that is `nixafnetlink run` blocked in `recv` (~150 KB, 0% CPU). systemd *can* hold it instead (`ListenNetlink=kobject-uevent 1`, `contrib/nixafnetlink.socket`) and fork us per burst — but group 1 is *every* kobject, so that is more starts, not fewer, unless we stay running after the first packet. LISTEN_FDS is not wired yet. Do not switch unless idle RSS becomes a problem.

Verified: bind succeeds, process sits in `recv`. Writing `/sys/.../uevent` to synthesize a packet needs root; real proof is a hardware plug.

## Hardware — DJI Mic Mini 2 RX (2026-09-03)

Plug then unplug on USB-C `usb8/8-1`. Logs: `captures/dji-2026-09-03-*.log`.

`--applet` emitted exactly four lines:

```
imex add    typec  port1-partner          ← false positive
dji  add    usb    2ca3/4011/0  usb8/8-1
imex remove typec  port1-partner          ← same
dji  remove usb    2ca3/4011/0  usb8/8-1
```

Kernel dump: 80 events. `usb_device` add/remove is one each. The rest is interface / hid / input / sound / leds — the old six-copy burst. `DEVTYPE=usb_device` is the right USB gate.

**IMEX match is too wide.** Field test (2026-09-03) captured all three devices. Timeline `captures/field-2026-09-03-timeline.txt`:

```
12:31:56  imex add   port1-partner     DJI1 typec (false)
12:31:57  dji  add   2ca3/4011  usb8/8-1
12:32:02  dji  remove
12:32:09  logi add   46d/c53f   usb3/3-2   USB-A, no typec
12:32:13  logi remove
12:32:19  imex add   port1-partner     REAL pad (no usb_device)
12:32:24  imex remove
12:32:28  imex add   port1-partner     DJI2 typec (false)
12:32:32  dji  add   2ca3/4011  usb8/8-1
12:32:35  dji  remove
```

Real pad vs DJI dongle: both emit `typec_partner` + `usb_power_delivery` on `port1`. Only DJI then adds `usb_device 2ca3/4011`. Logi is `046d:c53f` on USB-A `usb3/3-2`, no typec. Live `imex-watch.sh` (`port*-partner`) has the same DJI false-positive.

Live watchers are **unchanged**: they still poll sysfs every 2s (`micmini2-watch`, `mother-watch`, `imex-watch`). This repo does not replace them yet.

## Why

`udevadm monitor` leftover processes held the watcher flock (`systemctl restart` became a no-op). libudev is a second userspace netlink (group 2) on top of the kernel multicast. We want the kernel packet itself.

## Run

```bash
cd ~/Documents/nixafnetlink
nix develop -c make
./nixafnetlink dump
./nixafnetlink applet
./nixafnetlink run -r rules.d --dry-run -v
./nixafnetlink run -r rules.d
./nixafnetlink reload
```

No root. If `bind` fails, say so — do not fall back to udev.

## Output

Default, one line:

```
ACTION SUBSYSTEM DEVTYPE PRODUCT DEVPATH [tag]
```

`--applet`:

```
dji add usb 2ca3/4011/100 /devices/...
logi remove usb 46d/c07d/8900 /devices/...
imex add typec - /devices/.../port0/port0-partner
```

## Next

1. Dry-run cycle 3 passed: DJI retracts false IMEX; logi exact; pad add/remove with no retract. Keep the sleeper daemon.
2. Live `run` (no `--dry-run`) still not the systemd trigger. Bash watchers still own plug popups.
3. Swap units only after a live (non-dry) plug with retract/sysfs cancel confirmed.
4. Waybar: drop `interval: 2` on imex/mother after the daemon is the trigger.

## Open questions

- One shared daemon vs three filtered readers?
- Typec partner fires for both the DJI RX and the IMEX pad (`port1`). Real pad: partner + PD, **no** `usb_device`. DJI: partner first, then `usb_device 2ca3/4011` ~1s later. Debounce, do not tag IMEX at partner-add instant.
- USB interface noise: we drop non-`usb_device`. Confirm that is enough vs SEQNUM coalesce.

## Do not

- Resume Metatron / thermal-p2 from here.
- Edit live `watch.sh` until this is verified on hardware.
