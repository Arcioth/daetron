# nixafnetlink

Kernel `NETLINK_KOBJECT_UEVENT` in a small C daemon. No libudev. A drop-in `.rule` file turns a plug/unplug into a command — Tauri popup, Kitty, a script, anything.

```bash
nix develop -c make
./nixafnetlink dump            # watch raw kernel events
./nixafnetlink run --dry-run   # load rules.d, print actions
```

## Status

As of 2026-09-03.

Works: dump, applet debug tags, `run` loader (in-memory rules, SIGHUP reload, typec debounce + sysfs cancel + retract, exec/kill, waybar signal). musl static. Dry-run field-tested 2026-09-03 on DJI Mic Mini 2 RX (`2ca3:4011`), Logitech Lightspeed (`046d:c53f`), IMEX Qi pad (typec `port*-partner` with no usb_device). USB-C gadgets also emit a partner node; IMEX retracts if `2ca3`/`46d` shows up.

Does not: replace the live `micmini2-watch` / `mother-watch` / `imex-watch` units yet. `reload` does not rescan already-plugged devices. Idle process is a netlink sleeper (~150 KB, 0% CPU), not a poll loop.

## Requirements

Linux. Unprivileged bind to netlink group 1 (kernel). musl-gcc for the small static binary; `make STATIC=0` is glibc.

## Run

```bash
cd ~/Documents/nixafnetlink
nix develop -c make          # musl -static
./nixafnetlink dump
./nixafnetlink applet        # hardcoded dji/logi/imex tags (debug)
./nixafnetlink run -r rules.d
./nixafnetlink run -r rules.d --dry-run -v
./nixafnetlink reload        # SIGHUP
```

Rules dir: `-r DIR`, `$NIXAFNETLINK_RULES`, `./rules.d`, else `~/.config/nixafnetlink/rules.d`.

User unit example: `contrib/nixafnetlink.service`. Not enabled.

## Add a device

Not a lot of work. No C.

1. Plug the device, run `./nixafnetlink dump`, note `SUBSYSTEM`, `DEVTYPE`, `PRODUCT` (USB) or `DEVPATH` (typec). Kernel `PRODUCT` is `%x` — Logitech `046d` arrives as `46d`.
2. Copy `examples/usb.rule` or `examples/typec-partner.rule` or `examples/kitty.rule` into `rules.d/yourname.rule`.
3. Set `name`, match keys, `on_add`, `on_remove`.
4. `./nixafnetlink reload` (or restart `run`).

`on_add` is a shell command. Tauri, Kitty, or a wrapper are the same:

```
on_add=$HOME/Documents/myapp/run.sh
on_add=kitty --class nixaf-nmtui -e nmtui
on_remove=kill:mybinary
on_remove=pkill -f 'kitty --class nixaf-nmtui'
```

USB-C gadgets also emit typec `*-partner`. If that is not your device, set `debounce_ms=1000` and `suppress_if_product_prefix=` for the USB VID that follows (see `rules.d/imex.rule`).

Waybar: `waybar_signal=6` sends `SIGRTMIN+6`. Drop `interval` on the custom module; keep `signal`.

## CLI

See `./nixafnetlink -h`. Default with no args is `dump` (no exec).

## Docs

README this file · AGENTS.md agents · HANDOFF.md session · CHANGELOG.md history
