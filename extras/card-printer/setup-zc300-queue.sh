#!/bin/bash
# Sets up a Zebra ZC300 card printer as a CUPS queue on a Debian server, for
# DerbyNet's license-card printing (see README.md in this directory).
#
# Usage:  sudo bash setup-zc300-queue.sh <printer-ip> [queue-name]
#
# First run Zebra's own Linux driver installer (not included here: it is
# Zebra's software and about 100 MB; see README.md for where to get it):
#
#   sudo ./Zebra_ZC_Card_Printer_Driver-1.0.0.0-Linux-x86_64-Install \
#        --mode silent --prefix /opt/zebra-zc
#
# This script then does by hand what Zebra's own Install.sh would have done,
# minus the parts we do not want (it appends "xhost +" to /etc/profile).
#
# Assembled from the steps that were done by hand on the event server; safe to
# re-run, but it has not been run end to end from a clean machine.
set -eu

IP="${1:?usage: sudo bash $0 <printer-ip> [queue-name]}"
QUEUE="${2:-ZC300}"
Z=/opt/zebra-zc

[ "$(id -u)" -eq 0 ] || { echo "Run with sudo"; exit 1; }
[ -x "$Z/rastertojg" ] || { echo "Run Zebra's installer into $Z first (see the top of this file)"; exit 1; }

echo "== packages (CUPS listens on localhost only; no network printer discovery)"
apt-get update
apt-get install -y --no-install-recommends cups cups-client ghostscript \
  poppler-utils libcupsimage2t64 libpoppler-glib8t64

echo "== Zebra's libraries, and the path its filters expect"
cp -n "$Z/libtinyxml.so.2.6.2" /usr/lib/x86_64-linux-gnu/
cp -f "$Z/libzmjxml.so" /usr/lib/
ldconfig
ln -sfn "$Z" /usr/local/ZebraJaguarDriver
[ -d "$Z/jre1.8.0_111" ] || tar -xzf "$Z/jre1.8.0_111.tar.gz" -C "$Z"
install -d -m 775 -o root -g lp /var/log/JaguarLog "$Z/config"

echo "== print filters and the printer profile"
install -m 755 -o root -g root "$Z/rastertojg" "$Z/pdftojgpdf" /usr/lib/cups/filter/
install -m 644 -o root -g root "$Z/ZebraZC300Printer.ppd" /usr/share/cups/model/

echo "== the queue: $QUEUE -> socket://$IP:9100"
lpadmin -p "$QUEUE" -E -v "socket://$IP:9100" -m ZebraZC300Printer.ppd \
  -D "Zebra ZC300 (network)" -L "Event table" \
  -o PageSize-default=CR80 \
  -o printer-error-policy=retry-job
systemctl restart cups
sleep 2

echo "== ribbon combination: full color on both sides"
# Zebra's filter IGNORES per-job and per-queue RibbonCombination settings and
# reads *DefaultRibbonCombination directly from the queue's PPD file.  The
# stock default (2FrontYmcoBackK) prints the back of the card black-only.
# lpadmin can regenerate this file, so re-run this script after any change.
PPD="/etc/cups/ppd/$QUEUE.ppd"
cp -n "$PPD" "$PPD.orig"
sed -i 's/^\*DefaultRibbonCombination:.*/*DefaultRibbonCombination: 1FrontYmckoBackYmcko/' "$PPD"
grep -n '^\*DefaultRibbonCombination' "$PPD"

echo
lpstat -p "$QUEUE"
echo "Done.  Queue name for DerbyNet: $QUEUE (RaceInfo key 'card-printer' overrides the default of ZC300)."
