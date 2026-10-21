#!/bin/bash
# DerbyNet kiosk setup for a freshly installed Debian (Xfce) machine.
#
# Usage:  sudo bash kiosk-setup.sh <address> [username] [camera]
#   <address>  lowercase kiosk id, e.g. kiosk1, starter, replay1
#   [username] desktop user that auto-logs-in (default: derbyadmin)
#   [camera]   makes this a REPLAY kiosk: part of the webcam's name, e.g. Rally
#              (omit for an ordinary display kiosk)
# Safe to re-run.
set -eu

ADDRESS="${1:?usage: sudo bash $0 <address> [username] [camera]}"
KUSER="${2:-derbyadmin}"
CAMERA="${3:-}"
SERVER_URL="https://derbynet.local/derbynet"
KHOME=$(getent passwd "$KUSER" | cut -d: -f6)

[ "$(id -u)" -eq 0 ] || { echo "Run with sudo"; exit 1; }
[ -d "$KHOME" ] || { echo "No such user: $KUSER"; exit 1; }

echo "== passwordless sudo for $KUSER"
echo "$KUSER ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/$KUSER
chmod 440 /etc/sudoers.d/$KUSER
visudo -c >/dev/null

echo "== packages"
apt-get update
apt-get install -y avahi-daemon libnss-mdns curl wget apt-transport-https \
  chromium unclutter v4l-utils

echo "== DerbyNet apt repository + extras"
wget -q -O- https://derbynet.org/debian/derbynet.gpg \
  > /usr/share/keyrings/derbynet-archive-keyring.gpg
echo "deb [arch=all signed-by=/usr/share/keyrings/derbynet-archive-keyring.gpg] https://derbynet.org/debian stable main" \
  > /etc/apt/sources.list.d/derbynet.list
apt-get update
apt-get install -y derbynet-extras

echo "== /etc/derbynet.conf"
sed -i '/^DERBYNET_SERVER=/d;/^ADDRESS=/d;/^CAMERA=/d' /etc/derbynet.conf
echo "DERBYNET_SERVER=\"$SERVER_URL\"" >> /etc/derbynet.conf
echo "ADDRESS=$ADDRESS" >> /etc/derbynet.conf
if [ -n "$CAMERA" ]; then
  echo "CAMERA=\"$CAMERA\"" >> /etc/derbynet.conf
fi

echo "== kiosk launcher"
cat > /usr/local/bin/derbynet-kiosk.sh <<'EOF'
#!/bin/sh
# Ordinary kiosk, or (when CAMERA is set in /etc/derbynet.conf) a replay kiosk.
. /etc/derbynet.conf
xset s off; xset -dpms; xset s noblank
# Xfce's power manager turns DPMS back on at login unless told otherwise.
xfconf-query -c xfce4-power-manager -p /xfce4-power-manager/dpms-enabled \
  -n -t bool -s false 2>/dev/null || true
unclutter -idle 1 &
until curl -k -s --connect-timeout 6 -L "$DERBYNET_SERVER/index.php" >/dev/null; do sleep 3; done

EXTRA=""
if [ -n "${CAMERA:-}" ]; then
  CAM=$(printf %s "$CAMERA" | sed 's/ /%20/g')
  URL="$DERBYNET_SERVER/replay.php?address=$ADDRESS&autostart=1&camera=$CAM"
  EXTRA="--use-fake-ui-for-media-stream --autoplay-policy=no-user-gesture-required"
else
  URL="$DERBYNET_SERVER/kiosk.php?address=$ADDRESS"
fi

# Replay kiosks: find the video device whose name contains $CAMERA.
camera_device() {
  v4l2-ctl --list-devices 2>/dev/null | awk -v want="$(printf %s "$CAMERA" | tr A-Z a-z)" \
    '/^[^\t]/ { match_hdr = (index(tolower($0), want) > 0) }
     match_hdr && /\/dev\/video/ { print $1; exit }'
}

# Keep the camera at its full frame rate (many webcams quietly lower it in dim
# light). Optionally also force a short exposure to reduce motion blur: set
# CAMERA_EXPOSURE in /etc/derbynet.conf in units of 100 microseconds, e.g. 50
# for 5 ms. Leave it unset to keep automatic exposure.
tune_camera() {
  [ -n "${CAMERA:-}" ] || return 0
  DEV=$(camera_device)
  [ -n "$DEV" ] || return 0
  v4l2-ctl -d "$DEV" --set-ctrl=exposure_dynamic_framerate=0 2>/dev/null || true
  if [ -n "${CAMERA_EXPOSURE:-}" ]; then
    v4l2-ctl -d "$DEV" --set-ctrl=auto_exposure=1 \
      --set-ctrl=exposure_time_absolute="$CAMERA_EXPOSURE" 2>/dev/null || true
  fi
}

while true; do
  tune_camera
  chromium --kiosk --password-store=basic --noerrdialogs --no-default-browser-check \
    --disable-session-crashed-bubble --disable-infobars \
    --ignore-certificate-errors $EXTRA \
    "$URL"
  sleep 2
done
EOF
chmod +x /usr/local/bin/derbynet-kiosk.sh

echo "== autostart for $KUSER"
install -d -o "$KUSER" -g "$KUSER" "$KHOME/.config/autostart"
cat > "$KHOME/.config/autostart/kiosk.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Start kiosk
Exec=/usr/local/bin/derbynet-kiosk.sh
EOF
chown "$KUSER:$KUSER" "$KHOME/.config/autostart/kiosk.desktop"

echo "== autologin (lightdm)"
if systemctl list-unit-files | grep -q '^lightdm'; then
  mkdir -p /etc/lightdm/lightdm.conf.d
  printf '[Seat:*]\nautologin-user=%s\n' "$KUSER" \
    > /etc/lightdm/lightdm.conf.d/50-autologin.conf
else
  echo "!! lightdm not found - set up autologin by hand for your display manager"
fi

echo "== no screen lock, no sleep, no monitor blanking"
apt-get purge -y xfce4-screensaver light-locker 2>/dev/null || true
systemctl mask sleep.target suspend.target hibernate.target hybrid-sleep.target

# Turn off DPMS/blanking at the X server level so no desktop setting can undo it.
mkdir -p /etc/X11/xorg.conf.d
cat > /etc/X11/xorg.conf.d/10-no-blanking.conf <<'EOF'
Section "ServerFlags"
    Option "BlankTime"   "0"
    Option "StandbyTime" "0"
    Option "SuspendTime" "0"
    Option "OffTime"     "0"
EndSection
EOF

# Also turn off the Xfce power manager's DPMS for the kiosk user (best effort:
# needs a running session, so it is skipped quietly on a fresh install).
KUID=$(id -u "$KUSER")
if [ -S "/run/user/$KUID/bus" ]; then
  sudo -u "$KUSER" DISPLAY=:0 DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$KUID/bus" \
    xfconf-query -c xfce4-power-manager -p /xfce4-power-manager/dpms-enabled \
    -n -t bool -s false 2>/dev/null || true
fi

echo
echo "Done. Address: $ADDRESS   Server: $SERVER_URL${CAMERA:+   Replay camera: $CAMERA}"
[ -n "$CAMERA" ] && echo "Plug the camera into a blue USB 3.0 port."
echo "Reboot to start the kiosk:  sudo reboot"
