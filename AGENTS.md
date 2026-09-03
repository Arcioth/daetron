# nixafnetlink

Raw kernel netlink `NETLINK_KOBJECT_UEVENT` from C userspace. No libudev, no udevadm.

Intended to replace the 2s sysfs poll in the plug-popup watchers:

| tag | applet | match |
|---|---|---|
| `dji` | `~/Documents/micmini2` | USB `PRODUCT=2ca3/4011/*` `DEVTYPE=usb_device` |
| `logi` | `~/Documents/mother` | USB `PRODUCT=46d/*` `DEVTYPE=usb_device` |
| `imex` | `~/Documents/imex` | `SUBSYSTEM=typec` `DEVPATH=...-partner` |

Kernel `PRODUCT` is `%x` (Logitech `046d` arrives as `46d`).

## Do

- Keep the C tiny. One socket, one `recv` loop, NUL-separated kv parse.
- Default link is musl `-static`. Idle target is well under 500 KB RSS (measured 64 KB). `make STATIC=0` is glibc.
- Bind multicast **group 1** (kernel). Group 2 is udev userspace — do not use it.
- Filter `usb_device` only for USB applets. Interface `add` bursts were the six-copy bug.
- New applet = new file in `rules.d/`. Do not hardcode the next device in C. Tauri vs Kitty is `on_add` only.
- Rules load at start into memory. Reload is SIGHUP / `nixafnetlink reload`. No inotify, no disk read per plug.
- One daemon: `./nixafnetlink run -r rules.d`. Dump/applet are debug.
- Event-driven does not respawn: add execs once, remove kills. User closed the window while plugged stays down until unplug. No DISMISS file.

## Do not

- Link libudev / systemd.
- Touch `/etc/nixos` or replace live `*-watch.service` units until dispatch is proven on a plug.
- Poll `/sys` in a loop. One sysfs scan at start (already-plugged), then block on netlink.
