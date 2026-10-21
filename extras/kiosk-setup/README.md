# Kiosk setup

`kiosk-setup.sh` turns a freshly installed Debian (Xfce) machine into a DerbyNet
kiosk that starts by itself at power-on: auto-login, full-screen Chromium, no
screen lock or blanking, and a fixed kiosk identity.

## Use

Copy it to the machine and run it with the kiosk's lowercase address:

```bash
scp extras/kiosk-setup/kiosk-setup.sh derbyadmin@<host>.local:/tmp/
ssh -t derbyadmin@<host>.local 'sudo bash /tmp/kiosk-setup.sh <address> [user] [camera]'
sudo reboot
```

- `<address>` is the kiosk's identity (for example `kiosk1`, `starter`, `replay1`).
  It must match the address table used by the naming timer on the server.
- `[camera]` makes it a **replay kiosk**: the part of the webcam's name to use
  (for example `Rally`). The machine then opens `replay.php` with
  `autostart=1` instead of the plain kiosk page.
- Plug replay cameras into a blue **USB 3.0** port, or modes above 720p
  may not be offered.

## Per-machine settings in `/etc/derbynet.conf`

Written by the script: `DERBYNET_SERVER`, `ADDRESS`, `CAMERA`.

Optional, replay kiosks only (applied each time the browser starts):

| Setting | Meaning |
|---|---|
| `CAMERA_EXPOSURE=50` | Force a short manual exposure, in units of 100 µs (50 = 5 ms) to cut motion blur. Needs bright light. Unset = automatic exposure. |
| `CAMERA_FOCUS=60` | Lock the focus (Rally accepts 0-255) so autofocus does not hunt on fast cars. Unset = automatic focus. |

The launcher always turns off the camera's *dynamic frame rate* (when the
camera has that control) so it does not drop below its full rate in dim light.

To find a good focus value, run `v4l2-ctl -d /dev/video0 --set-ctrl=focus_automatic_continuous=0 --set-ctrl=focus_absolute=N`
with different `N` while watching the picture.
