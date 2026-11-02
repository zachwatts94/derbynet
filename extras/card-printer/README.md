# Card printing (Zebra ZC300)

DerbyNet's **Print Licenses** page (`print-review.php`) previews each racer's
license card exactly as it will print and sends it to a card printer attached to
the *server* through CUPS.  No separate print computer is needed.

## Pieces

| What | Where |
|---|---|
| Card artwork | `website/print/docs/racer/license-card/document.inc` (`CardLicenseDocument`) |
| Review page | `website/print-review.php`, `js/print-review.js`, `css/print-review.css` |
| Preview image | `website/print-preview.php` |
| Printing and the job log | `website/inc/card-printing.inc`, `ajax/action.print.card.inc`, `ajax/query.print.status.inc`, table `PrintJobs` (created on first use) |
| Printer setup | `setup-zc300-queue.sh` in this directory |

## Setting up a server

1. Download Zebra's **ZC100/300 Series Linux Driver** (and its user guide) from
   the ZC300 support page on zebra.com, accepting Zebra's license yourself.  It
   is not in this repository.
2. Run it as root, into `/opt/zebra-zc`:
   `sudo ./Zebra_ZC_Card_Printer_Driver-1.0.0.0-Linux-x86_64-Install --mode silent --prefix /opt/zebra-zc`
3. `sudo bash setup-zc300-queue.sh <printer-ip>`
4. Copy the logo file by hand, if you use one (see below).

The printer should have a fixed address (a reservation in the router, or a static
address outside the DHCP range).  If it changes:
`sudo lpadmin -p ZC300 -v socket://NEWIP:9100`.

## Things worth knowing

* **The ribbon combination is read from the queue's PPD file**, not from print
  options.  Zebra's filter ignores `RibbonCombination` on the command line and in
  `lpadmin`, and reads `*DefaultRibbonCombination` straight out of
  `/etc/cups/ppd/ZC300.ppd`.  The stock value prints the back of the card
  black-only.  `setup-zc300-queue.sh` sets it to full color on both sides.
  Two-sided color uses two ribbon panel sets per card (a 300-image ribbon makes
  about 150 two-sided cards).
* **How to see what the driver decided, without wasting cards:** the filter
  leaves `/tmp/printdata.xml` (what it will print on each side) and
  `/tmp/JgFilter.log` ("Selected RibbonCombination is N") in CUPS's private
  temporary folder.  A second CUPS queue pointed at a dummy network port is a
  free way to run jobs through the real driver.
* **Card size:** the card PDF page is exactly the profile's 241.5 x 153.6 points
  so nothing is rescaled, and the artwork is enlarged 2% (`OVERSCAN`) to hide
  the thin white edge the printer leaves.  Text is pure black, because the
  printer prints pure black with its crisp black panel and mixes grays from the
  color panels (which comes out faint at small sizes).
* **Per-event settings** (pack name, expiration, weight limits per group, ...)
  can go in `local/config-card.inc`, which returns an array of option => value.
  `weight_overrides` is `"Group=limit; Group=limit"` and matches the racer's
  group name, ignoring case.  The default sets `Outlaw=10 oz`.

## The logo

The Cub Scouts logo watermark is **not in this repository** on purpose.  The
Scouting America Brand Center's terms restrict editing and publishing its
artwork, and this repository is public.  The card looks for
`website/print/docs/racer/license-card/logo-watermark.png` (a one-color,
transparent image) and draws nothing if it is missing.  Both `logo.jpg` and
`logo-watermark.png` are listed in `.gitignore`; put the file on the server by
hand.  Whether the watermark treatment fits the Brand Center terms is the pack's
call.
