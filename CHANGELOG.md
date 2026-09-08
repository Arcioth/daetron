# Changelog

## 0.4.1 — 2026-09-08

- `rules.d/ctron.rule`: USB `DEVTYPE=usb_device` → `~/Documents/ctron/scripts/daetron-spawn.sh`. Prefix `0000/0000/` until a spare dongle is identified. No `on_remove` kill.

## 0.4.0 — 2026-09-08

- Renamed **arclinkdae → daetron**. Folder `~/Documents/daetron`. GitHub `Arcioth/daetron`. Binary, systemd units, pid/lock, env `DAETRON_RULES`, `~/.config/daetron/rules.d`. LICENSE (Apache-2.0) unchanged.
- New GitHub repo `Arcioth/daetron`. Deleting `Arcioth/arclinkdae` needs a token with `delete_repo` (current `gh` token does not). Do **not** run `~/Documents/nixos/apply.sh` for this rename — the NixOS tree has no arclinkdae/daetron package. HHO PDF left as-is.

## 0.3.0 — 2026-09-03

- Project renamed to **arclinkdae**. Folder `~/Documents/arclinkdae`. GitHub `Arcioth/arclinkdae` (former nixafnetlink name redirected). Binary, rules dir, pid/lock, env `ARCLINKDAE_RULES`.
- Future field tests target Arch as well as NixOS.

## 0.1.0 — 2026-09-03

- New repo: raw `NETLINK_KOBJECT_UEVENT` listener in C. No libudev.
- `src/uevent.c`: bind group 1, 2 MiB `SO_RCVBUF`, NUL kv parse, stdout lines.
- `--applet` filter: DJI Mic Mini 2 `2ca3/4011`, Logitech `46d`, IMEX typec `*-partner`. USB limited to `DEVTYPE=usb_device` so interface bursts do not spawn six applets.
- Makefile + flake (same nixpkgs pin as micmini2 / mother / imex).
- Does not replace live `*-watch.service` units yet.

## 0.1.1 — 2026-09-03

- Default link is musl `-static` (`make STATIC=0` for glibc). Idle RSS **64 KB**, CPU 0 ticks / 3s. glibc dynamic was 1492 KB RSS / 134 KB PSS, almost all shared libc.

## 0.1.2 — 2026-09-03

- First hardware capture: DJI Mic Mini 2 RX plug+unplug (`captures/dji-2026-09-03-*.log`).
- `dji` tag is correct: one `usb_device` add and one remove (`2ca3/4011/0` on `usb8/8-1`). Kernel burst was 80 events.
- `imex` tag false-positive: RX is USB-C so `port1-partner` add/remove fires too. Filter needs tightening after a real pad plug.

## 0.1.3 — 2026-09-03

- Full field test: DJI in/out, G receiver in/out, IMEX pad in/out, DJI in/out (`captures/field-2026-09-03-*`).
- Logi is `46d/c53f/5501` `usb_device` on `usb3/3-2` (USB-A). No typec. One add, one remove. Extra hid `046D:4074` is the mouse behind the receiver — ignored by `usb_device` gate.
- Real IMEX pad: `port1-partner` + `usb_power_delivery`, no `usb_device`. DJI also emits partner+PD, then `usb_device 2ca3/4011` ~1s later. Tagging IMEX at partner-add is wrong; debounce.

## 0.1.4 — 2026-09-03

- Live stats after 10 min: 68 KB RSS, 46 KB PSS, 0 CPU ticks. Three bash watchers still ~4.8 MB / 0.2% each.
- `rules.d/{dji,logi,imex}.rule` — drop-in match + exec + waybar signal. IMEX rule has 500 ms debounce and suppress on `2ca3/` / `46d/`. Dispatcher not written yet.

## 0.1.5 — 2026-09-03

- Loader locked: in-memory at boot, SIGHUP reload. No inotify hotload, no per-plug file read. Waybar modules should become signal-only (`interval` off, SIGRTMIN+6/+7).

## 0.2.0 — 2026-09-03

- Dispatcher: `daetron run -r rules.d`. Loads `*.rule` into memory, one sysfs scan, then netlink. SIGHUP / `daetron reload`. `--dry-run`.
- IMEX: 1000 ms debounce, suppress `2ca3/` and `46d/`. Remove is skipped if add never fired.
- `on_add` is `/bin/sh -c` (Tauri, Kitty, anything). `on_remove=kill:comm` or a command. `$HOME` expanded.
- Examples in `examples/`. User unit in `contrib/daetron.service`. README for adding a device.
- Binary ~92 KB static, ~156 KB RSS with three rules. Bash watchers not replaced yet.

## 0.2.1 — 2026-09-03

- Dry-run: DJI USB-C still beat 1000–1500 ms debounce (partner add, then `2ca3` ~1s later). Timer fired before the USB packet was read.
- Drain netlink (`MSG_DONTWAIT`) before firing debounce. Retract an armed rule if a suppress VID shows up after the fact (`retract imex (saw 2ca3/…)`).
- Dry-run cycle 3: DJI retracts IMEX on `2ca3/4011/0`; logi add/remove; real pad add/remove with no retract. During debounce, sysfs is rechecked every 100 ms so a gadget VID can cancel before `on_add` runs.

## 0.2.2 — 2026-09-03

- Apache License 2.0 (`LICENSE`).
- README rewritten: results and comparative RSS/CPU first, mermaid charts, dry-run log, approach diagrams. web4p-style sections.
