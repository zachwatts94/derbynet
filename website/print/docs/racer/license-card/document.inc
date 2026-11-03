<?php

// Novelty driver's license sized for a CR-80 plastic card (3.375" x 2.125"),
// styled after an Arkansas license: colored lattice background, header band
// with a gold star, ghost photo, license number and the usual boxed fields,
// and a signature line.  One card per PDF page; when two_sided is set, the back
// (the car) follows as the next page so a duplex card printer (e.g. Zebra ZC300
// dual-sided) puts page N on the front and page N+1 on the back.
//
// It is deliberately labeled "Pinewood Derby" and "not a government ID".

require_once('inc/partitions.inc');
require_once('inc/path-info.inc');
require_once('inc/photo-config.inc');
require_once('inc/photo-aspect.inc');

class CardLicenseDocument extends PrintableRacerDocument {
  // CR-80 card, landscape.  These are the page dimensions in Zebra's printer
  // profile (241.5 x 153.6 points), so the print system never has to rescale the
  // card.  (A true CR-80 is 3.370" x 2.125".)
  const CARD_W = 3.3541667;
  const CARD_H = 2.1333333;
  // The printed image is a little smaller than the real card, and a card never
  // sits in the printer perfectly, so the artwork is enlarged by this fraction
  // about the card's center; the overhang is cut off, which keeps the colored
  // background reaching all four edges instead of leaving thin white borders.
  const OVERSCAN = 0.02;
  // Card printers can't register artwork perfectly; keep text inside this.
  const SAFE = 0.09;

  function name() { return "Driver's License (CR-80 Card)"; }

  function get_available_options() {
    return array(
      'two_sided' => array('type' => 'bool',
                           'desc' => 'Emit a back side (car photo) as a second page',
                           'default' => true),
      'use_photo' => array('type' => 'bool',
                           'desc' => 'Include racer\'s photo',
                           'default' => true),
      'date_of_issue' => array('type' => 'string',
                               'desc' => 'Date of Issue',
                               'default' => self::Today()),
      'pack_name' => array('type' => 'string',
                           'desc' => 'Pack name',
                           'default' => 'Cub Scout Pack 525'),
      'expires' => array('type' => 'string',
                         'desc' => 'Expires',
                         'default' => 'Next Derby'),
      'weight_limit' => array('type' => 'string',
                              'desc' => 'Weight limit',
                              'default' => '5.0 oz'),
      // Groups whose limit differs from weight_limit, as "Group=limit; Group=limit".
      'weight_overrides' => array('type' => 'string',
                                  'desc' => 'Weight limits by group (Group=limit; ...)',
                                  'default' => 'Outlaw=10 oz'),
      'restrictions' => array('type' => 'string',
                              'desc' => 'Restrictions',
                              'default' => 'Cub Scout Law'),
      'endorsements' => array('type' => 'string',
                              'desc' => 'Endorsements',
                              'default' => 'None'),
      'show_lattice' => array('type' => 'bool',
                              'desc' => 'Diamond crosshatch pattern in the background',
                              'default' => true),
      'color_scheme' => array('type' => 'radio',
                              'desc' => 'Color Scheme',
                              'values' => array(array('value' => 'blue', 'desc' => 'Blue'),
                                                array('value' => 'green', 'desc' => 'Green'),
                                                array('value' => 'orange', 'desc' => 'Orange'),
                                                array('value' => 'red', 'desc' => 'Red'),
                                                array('value' => 'maroon', 'desc' => 'Maroon')),
                              'default' => 'blue'),
    );
  }

  // For each scheme: deep (header, band, number box), tint (background), lattice.
  protected static $schemes = array(
    'blue'   => array('deep' => array(0x0b, 0x3a, 0x82), 'tint' => array(0xd8, 0xe7, 0xf9),
                      'tint2' => array(0xb7, 0xd0, 0xee), 'lattice' => array(0x7f, 0xa7, 0xdb)),
    'green'  => array('deep' => array(0x1b, 0x6b, 0x3a), 'tint' => array(0xdb, 0xef, 0xe1),
                      'tint2' => array(0xb5, 0xdc, 0xc0), 'lattice' => array(0x7c, 0xc0, 0x92)),
    'orange' => array('deep' => array(0xc2, 0x5a, 0x0a), 'tint' => array(0xfc, 0xe9, 0xd3),
                      'tint2' => array(0xf6, 0xc9, 0x9c), 'lattice' => array(0xe9, 0xa2, 0x62)),
    'red'    => array('deep' => array(0xa5, 0x1c, 0x1c), 'tint' => array(0xf9, 0xdf, 0xdf),
                      'tint2' => array(0xef, 0xb8, 0xb8), 'lattice' => array(0xdd, 0x82, 0x82)),
    'maroon' => array('deep' => array(0x6d, 0x16, 0x34), 'tint' => array(0xf1, 0xdc, 0xe4),
                      'tint2' => array(0xdf, 0xb4, 0xc5), 'lattice' => array(0xc0, 0x7d, 0x98)),
  );
  protected $scheme;

  protected static $gold = array(0xf2, 0xb7, 0x05);

  protected function SetFillColor($rgb) { $this->pdf->SetFillColor($rgb[0], $rgb[1], $rgb[2]); }
  protected function SetTextColor($rgb) { $this->pdf->SetTextColor($rgb[0], $rgb[1], $rgb[2]); }
  protected function SetDrawColor($rgb) { $this->pdf->SetDrawColor($rgb[0], $rgb[1], $rgb[2]); }

  function StartDocument() {
    // FPDF normalizes the size array to portrait and then applies 'L',
    // so this yields a 3.375 x 2.125 landscape page.
    $this->initialize_pdf('L', 'in', array(self::CARD_H, self::CARD_W));
    $this->initialize_layout(self::CARD_W, self::CARD_H);
    // Exactly one card per page, no margins, no gaps.
    $this->layout->SetRawLayoutParameters(0, 0, 0, 0, /* n_per_row */ 1, /* n_per_page */ 1);

    $name = $this->get_option('color_scheme');
    $this->scheme = isset(self::$schemes[$name]) ? self::$schemes[$name] : self::$schemes['blue'];
  }

  // Today's date in the card's own time zone.  (PHP's default time zone is
  // normally UTC, which is already "tomorrow" every evening in US Central time.)
  // Set 'timezone' in local/config-card.inc to change it.
  protected static function Today() {
    $local = self::LocalConfig();
    $zone = isset($local['timezone']) ? $local['timezone'] : 'America/Chicago';
    try {
      $now = new DateTimeImmutable('now', new DateTimeZone($zone));
    } catch (Exception $e) {
      $now = new DateTimeImmutable('now');
    }
    return $now->format('F j, Y');
  }

  // Settings that belong to one event rather than to the code can be put in
  // local/config-card.inc, which returns an array of option => value.  (The
  // local directory is never overwritten when new code is deployed.)  Options
  // passed explicitly with a request still win.
  private static $local_config = null;

  protected static function LocalConfig() {
    if (self::$local_config === null) {
      self::$local_config = array();
      $dir = isset($_SERVER['DERBYNET_CONFIG_DIR']) ? $_SERVER['DERBYNET_CONFIG_DIR'] : 'local';
      $config = @include($dir.DIRECTORY_SEPARATOR.'config-card.inc');
      if (is_array($config)) {
        self::$local_config = $config;
      }
    }
    return self::$local_config;
  }

  function get_option($option) {
    if (isset($this->options[$option])) {
      return $this->options[$option];
    }
    $local = self::LocalConfig();
    if (isset($local[$option])) {
      return $local[$option];
    }
    return parent::get_option($option);
  }

  // The weight limit for this racer's group: the group's entry in
  // weight_overrides if there is one, otherwise the general weight_limit.
  protected function WeightLimit(&$racer) {
    $group = strtolower(trim(strval($racer['class'])));
    if ($group !== '') {
      foreach (explode(';', strval($this->get_option('weight_overrides'))) as $pair) {
        $parts = explode('=', $pair, 2);
        if (count($parts) == 2 && strtolower(trim($parts[0])) === $group) {
          return trim($parts[1]);
        }
      }
    }
    return $this->get_option('weight_limit');
  }

  // The base class hands us the (0,0) origin of one page; if a back side was
  // requested, we claim a second page for it here.
  function DrawOneAt($x, $y, &$racer) {
    $this->DrawOverscanned($x, $y, function() use ($x, $y, &$racer) {
      $this->DrawFront($x, $y, $racer);
    });
    if ($this->get_option('two_sided')) {
      $xy = $this->layout->NextTopLeft();
      $this->DrawOverscanned($xy['x'], $xy['y'], function() use ($xy, &$racer) {
        $this->DrawBack($xy['x'], $xy['y'], $racer);
      });
    }
  }

  protected function DrawOverscanned($x, $y, $draw) {
    $this->pdf->StartTransform();
    $this->pdf->ScaleXY(100 * (1 + self::OVERSCAN), $x + self::CARD_W / 2, $y + self::CARD_H / 2);
    $draw();
    $this->pdf->StopTransform();
  }

  // ---- Helpers ----------------------------------------------------------

  // Draws an image scaled to fit entirely inside the given box, preserving its
  // aspect ratio and centering it, so that photos of any shape stay on the card.
  protected function FitImage($file, $x, $y, $box_w, $box_h) {
    $size = @getimagesize($file);
    if (!$size || $size[0] <= 0 || $size[1] <= 0) {
      $this->pdf->Image($file, $x, $y, $box_w);
      return;
    }
    $scale = min($box_w / $size[0], $box_h / $size[1]);
    $w = $size[0] * $scale;
    $h = $size[1] * $scale;
    $this->pdf->Image($file, $x + ($box_w - $w) / 2, $y + ($box_h - $h) / 2, $w, $h);
  }

  // Draws a headshot so that it fills the box completely: a photo that isn't
  // already the right shape (say, an uncropped webcam picture) is trimmed evenly
  // from the sides or the top and bottom, rather than shrunk with empty bars.
  // Photos that are already the right shape are placed as they are.
  protected function CoverImage($file, $x, $y, $box_w, $box_h) {
    $size = @getimagesize($file);
    if (!$size || $size[0] <= 0 || $size[1] <= 0) {
      $this->FitImage($file, $x, $y, $box_w, $box_h);
      return;
    }
    $box_aspect = $box_w / $box_h;
    $aspect = $size[0] / $size[1];
    if (abs($aspect / $box_aspect - 1) < 0.02) {
      $this->pdf->Image($file, $x, $y, $box_w, $box_h);
      return;
    }
    if ($aspect > $box_aspect) {  // too wide: trim the sides
      $src_h = $size[1];
      $src_w = (int) round($src_h * $box_aspect);
    } else {                      // too tall: trim the top and bottom
      $src_w = $size[0];
      $src_h = (int) round($src_w / $box_aspect);
    }
    $src_x = (int) round(($size[0] - $src_w) / 2);
    $src_y = (int) round(($size[1] - $src_h) / 2);

    $im = false;
    switch ($size[2]) {
    case IMAGETYPE_JPEG: $im = @imagecreatefromjpeg($file); break;
    case IMAGETYPE_PNG: $im = @imagecreatefrompng($file); break;
    case IMAGETYPE_GIF: $im = @imagecreatefromgif($file); break;
    }
    if (!$im) {
      $this->FitImage($file, $x, $y, $box_w, $box_h);
      return;
    }
    // About 600 dpi is plenty for a card printer's 300 dpi.
    $out_w = min($src_w, (int) round($box_w * 600));
    $out_h = (int) round($out_w / $box_aspect);
    $out = imagecreatetruecolor($out_w, $out_h);
    imagecopyresampled($out, $im, 0, 0, $src_x, $src_y, $out_w, $out_h, $src_w, $src_h);
    $tmp = tempnam(sys_get_temp_dir(), 'cover-').'.jpg';
    imagejpeg($out, $tmp, 92);
    $this->pdf->Image($tmp, $x, $y, $box_w, $box_h);
    @unlink($tmp);
    imagedestroy($out);
    imagedestroy($im);
  }

  // Writes text at a baseline position, shrinking the font until it fits.
  protected function FitText($x, $y, $text, $max_w, $style, $size, $min_size = 4.5) {
    $text = strval($text);
    do {
      $this->pdf->SetFont('Arial', $style, $size);
      if ($this->pdf->GetStringWidth($text) <= $max_w || $size <= $min_size) {
        break;
      }
      $size -= 0.25;
    } while (true);
    $this->pdf->Text($x, $y, $text);
  }

  // A small caption above a value, as on real licenses.  Small text is pure
  // black: the card printer prints pure black with its crisp black panel, while
  // grays are mixed from the color panels and come out faint.
  protected function Caption($x, $y, $text) {
    $this->pdf->SetTextColor(0, 0, 0);
    $this->pdf->SetFont('Arial', '', 4.8);
    $this->pdf->Text($x, $y, strtoupper($text));
  }

  protected function Value($x, $y, $text, $max_w, $size = 7) {
    $this->pdf->SetTextColor(0, 0, 0);
    $this->FitText($x, $y, strtoupper(strval($text)), $max_w, 'B', $size);
  }

  protected function LicenseNumber(&$racer) {
    return sprintf('PWD %06d', $racer['racerid']);
  }

  protected function IssueDate() {
    $d = new DateTimeImmutable($this->get_option('date_of_issue'));
    return $d->format('m-d-Y');
  }

  // An optional logo (logo-watermark.png, beside this file: a one-color,
  // transparent version of the artwork) shown faintly behind the front of the
  // card.  Without the file, nothing is drawn.  The logo files are kept out of
  // version control.
  protected function DrawLogo($x, $y) {
    $file = dirname(__FILE__).DIRECTORY_SEPARATOR.'logo-watermark.png';
    if (!is_file($file)) {
      return;
    }
    $this->pdf->SetAlpha(0.24);
    $this->FitImage($file, $x + 1.02, $y + 0.48, 1.62, 1.46);
    $this->pdf->SetAlpha(1);
  }

  // Full-bleed colored ground: soft gradient plus a diamond lattice.
  protected function DrawBackground($x, $y) {
    $s = $this->scheme;
    // Deliberately oversized: the ZC300 prints past the card edge, and FPDF
    // clips whatever falls off the page.
    $this->pdf->LinearGradient($x - 0.05, $y - 0.05, self::CARD_W + 0.1, self::CARD_H + 0.1,
                               $s['tint'], $s['tint2'], array(0, 0, 1, 1));

    if (!$this->get_option('show_lattice')) {
      return;
    }
    $this->SetDrawColor($s['lattice']);
    $this->pdf->SetLineWidth(0.012);
    $this->pdf->SetAlpha(0.40);
    $step = 0.17;
    for ($i = -self::CARD_H; $i <= self::CARD_W + self::CARD_H; $i += $step) {
      $this->pdf->Line($x + $i, $y, $x + $i + self::CARD_H, $y + self::CARD_H);
      $this->pdf->Line($x + $i + self::CARD_H, $y, $x + $i, $y + self::CARD_H);
    }
    $this->pdf->SetAlpha(1);
  }

  // The gold star badge, like the REAL ID star in the corner of a license.
  protected function DrawStar($cx, $cy, $r) {
    $this->SetFillColor(self::$gold);
    $this->SetDrawColor($this->scheme['deep']);
    $this->pdf->SetLineWidth(0.012);
    $this->pdf->Circle($cx, $cy, $r, 'DF');
    $this->pdf->SetFont('ZapfDingbats', '', $r * 100);
    $this->SetTextColor($this->scheme['deep']);
    $w = $this->pdf->GetStringWidth('H');
    $this->pdf->Text($cx - $w / 2, $cy + $r * 0.36, 'H');
    $this->pdf->SetTextColor(0, 0, 0);
  }

  protected function DrawFooterBand($x, $y) {
    $top = $y + self::CARD_H - 0.17;
    $this->SetFillColor($this->scheme['deep']);
    $this->pdf->Rect($x - 0.05, $top, self::CARD_W + 0.1, 0.22, 'F');
    $this->SetFillColor(self::$gold);
    $this->pdf->Rect($x - 0.05, $top, self::CARD_W + 0.1, 0.012, 'F');
    $this->SetTextColor(array(0xff, 0xff, 0xff));
    $this->pdf->SetFont('Arial', 'B', 4.5);
    $text = 'NOT A GOVERNMENT ID  \x95  FOR PINEWOOD DERBY USE ONLY';
    $text = str_replace('\x95', chr(149), $text);
    $w = $this->pdf->GetStringWidth($text);
    $this->pdf->Text($x + (self::CARD_W - $w) / 2, $y + self::CARD_H - 0.065, $text);
    $this->pdf->SetTextColor(0, 0, 0);
  }

  // ---- Front ------------------------------------------------------------

  protected function DrawFront($x, $y, &$racer) {
    $s = $this->scheme;
    $this->DrawBackground($x, $y);
    $this->DrawLogo($x, $y);

    // Header band
    $this->SetFillColor($s['deep']);
    $this->pdf->Rect($x - 0.05, $y - 0.05, self::CARD_W + 0.1, 0.47, 'F');
    $this->SetFillColor(self::$gold);
    $this->pdf->Rect($x - 0.05, $y + 0.42, self::CARD_W + 0.1, 0.012, 'F');

    $this->SetTextColor(array(0xff, 0xff, 0xff));
    $this->pdf->SetFont('Arial', 'B', 15);
    $this->pdf->Text($x + self::SAFE + 0.03, $y + 0.28, 'ARKANSAS');
    $this->pdf->SetFont('Arial', 'B', 6);
    $this->pdf->Text($x + 1.42, $y + 0.17, 'PINEWOOD DERBY');
    $this->SetTextColor(self::$gold);
    $this->pdf->SetFont('Arial', 'B', 8.5);
    $this->pdf->Text($x + 1.42, $y + 0.30, 'DRIVER LICENSE');
    $this->DrawStar($x + self::CARD_W - 0.27, $y + 0.215, 0.135);

    // Photo, with a thin frame
    $px = $x + 0.14;
    $py = $y + 0.54;
    if ($this->get_option('use_photo')) {
      if ($racer['imagefile']) {
        $this->CoverImage($racer['imagefile'], $px, $py, 0.84, 0.84 / HEADSHOT_ASPECT);
      } else {
        $this->SetFillColor(array(0x9a, 0x9a, 0x9a));
        $this->pdf->Rect($px, $py, 0.84, 1.12, 'F');
        $this->pdf->SetFont('Arial', '', 7);
        $this->SetTextColor(array(0xff, 0xff, 0xff));
        $this->pdf->Text($px + 0.17, $py + 0.58, 'NO PHOTO');
      }
      $this->SetDrawColor($s['deep']);
      $this->pdf->SetLineWidth(0.012);
      $this->pdf->Rect($px, $py, 0.84, 1.12, 'D');
    }

    // Name
    $cx = $x + 1.10;
    $col_w = 1.52;
    $this->Caption($cx, $y + 0.57, 'Last name');
    $this->Value($cx, $y + 0.715, $racer['lastname'], $col_w, 11);
    $this->Caption($cx, $y + 0.81, 'First name');
    $this->Value($cx, $y + 0.925, $racer['firstname'], $col_w, 8.5);

    // Boxed fields
    $c2 = $cx + 0.78;
    $this->Caption($cx, $y + 1.02, 'Lic no');
    $this->Value($cx, $y + 1.125, $this->LicenseNumber($racer), 0.74);
    $this->Caption($c2, $y + 1.02, use_groups() ? group_label() : 'Den');
    $this->Value($c2, $y + 1.125, $racer['class'], 0.74);

    $this->Caption($cx, $y + 1.205, 'Iss');
    $this->Value($cx, $y + 1.31, $this->IssueDate(), 0.74);
    $this->Caption($c2, $y + 1.205, 'Exp');
    $this->Value($c2, $y + 1.31, $this->get_option('expires'), 0.74);

    $this->Caption($cx, $y + 1.39, 'Wgt');
    $this->Value($cx, $y + 1.495, $this->WeightLimit($racer), 0.74);
    $this->Caption($c2, $y + 1.39, 'End');
    $this->Value($c2, $y + 1.495, $this->get_option('endorsements'), 0.74);

    $this->Caption($cx, $y + 1.575, 'Restr');
    $this->Value($cx, $y + 1.68, $this->get_option('restrictions'), $col_w);

    // Signature, in italic, over a line
    $this->pdf->SetFont('Times', 'I', 11);
    $this->pdf->SetTextColor(0, 0, 0);
    $sig = $racer['firstname'].' '.$racer['lastname'];
    while ($this->pdf->GetStringWidth($sig) > $col_w && strlen($sig) > 3) {
      $sig = substr($sig, 0, -1);
    }
    $this->pdf->Text($cx, $y + 1.83, $sig);
    $this->SetDrawColor(array(0, 0, 0));
    $this->pdf->SetLineWidth(0.008);
    $this->pdf->Line($cx, $y + 1.855, $cx + $col_w, $y + 1.855);
    $this->Caption($cx, $y + 1.925, 'Driver signature');

    // Ghost photo, faded, like the security image on a real license
    if ($this->get_option('use_photo') && $racer['imagefile']) {
      $this->pdf->SetAlpha(0.32);
      $this->CoverImage($racer['imagefile'], $x + 2.72, $y + 0.54, 0.55, 0.55 / HEADSHOT_ASPECT);
      $this->pdf->SetAlpha(1);
    }

    // Car number badge
    $bx = $x + 2.68;
    $by = $y + 1.36;
    $this->SetFillColor($s['deep']);
    $this->pdf->RoundedRect($bx, $by, 0.60, 0.50, 0.05, 'F');
    $this->SetTextColor(self::$gold);
    $this->pdf->SetFont('Arial', 'B', 4.5);
    $this->pdf->Text($bx + 0.30 - $this->pdf->GetStringWidth('CAR NO') / 2, $by + 0.10, 'CAR NO');
    $this->SetTextColor(array(0xff, 0xff, 0xff));
    $num = strval($racer['carnumber']);
    $size = 22;
    do {
      $this->pdf->SetFont('Arial', 'B', $size);
      if ($this->pdf->GetStringWidth($num) <= 0.52 || $size <= 10) {
        break;
      }
      $size -= 1;
    } while (true);
    $this->pdf->Text($bx + 0.30 - $this->pdf->GetStringWidth($num) / 2, $by + 0.40, $num);
    $this->pdf->SetTextColor(0, 0, 0);

    $this->DrawFooterBand($x, $y);
  }

  // ---- Back -------------------------------------------------------------

  // No mirroring here: on a card printer each side is its own page, unlike the
  // sheet-fed license-back document.
  protected function DrawBack($x, $y, &$racer) {
    $s = $this->scheme;
    $this->DrawBackground($x, $y);

    // Header band
    $this->SetFillColor($s['deep']);
    $this->pdf->Rect($x - 0.05, $y - 0.05, self::CARD_W + 0.1, 0.35, 'F');
    $this->SetFillColor(self::$gold);
    $this->pdf->Rect($x - 0.05, $y + 0.30, self::CARD_W + 0.1, 0.012, 'F');
    $this->SetTextColor(self::$gold);
    $this->pdf->SetFont('Arial', 'B', 9);
    $this->pdf->Text($x + self::SAFE + 0.03, $y + 0.22, 'REGISTERED VEHICLE');
    $this->SetTextColor(array(0xff, 0xff, 0xff));
    $this->pdf->SetFont('Arial', 'B', 6);
    $label = strtoupper($this->get_option('pack_name'));
    $this->pdf->Text($x + self::CARD_W - self::SAFE - 0.03 - 0.30 - $this->pdf->GetStringWidth($label),
                     $y + 0.21, $label);
    $this->DrawStar($x + self::CARD_W - 0.22, $y + 0.15, 0.10);

    // Car photo in a white frame
    $fx = $x + 0.14;
    $fy = $y + 0.42;
    $fh = 1.24;
    $fw = $fh * CAR_PHOTO_ASPECT;
    $this->SetFillColor(array(0xff, 0xff, 0xff));
    $this->pdf->Rect($fx - 0.03, $fy - 0.03, $fw + 0.06, $fh + 0.06, 'F');
    if ($racer['carphoto']) {
      $this->FitImage($racer['carphoto'], $fx, $fy, $fw, $fh);
    } else {
      $this->SetFillColor(array(0x9a, 0x9a, 0x9a));
      $this->pdf->Rect($fx, $fy, $fw, $fh, 'F');
      $this->pdf->SetFont('Arial', '', 7);
      $this->SetTextColor(array(0xff, 0xff, 0xff));
      $this->pdf->Text($fx + $fw / 2 - $this->pdf->GetStringWidth('NO CAR PHOTO') / 2,
                       $fy + $fh / 2, 'NO CAR PHOTO');
    }
    $this->SetDrawColor($s['deep']);
    $this->pdf->SetLineWidth(0.012);
    $this->pdf->Rect($fx - 0.03, $fy - 0.03, $fw + 0.06, $fh + 0.06, 'D');

    // Right-hand column
    $cx = $x + 2.28;
    $col_w = 0.98;
    $this->Caption($cx, $y + 0.46, 'Vehicle');
    $this->Value($cx, $y + 0.59, $racer['carname'] !== '' ? $racer['carname'] : '-', $col_w, 9);

    $this->Caption($cx, $y + 0.72, 'Car no');
    $this->Value($cx, $y + 0.94, strval($racer['carnumber']), $col_w, 19);

    $this->Caption($cx, $y + 1.06, 'Driver');
    $this->Value($cx, $y + 1.17, $racer['firstname'].' '.$racer['lastname'], $col_w, 7);

    $this->Caption($cx, $y + 1.28, use_groups() ? group_label() : 'Den');
    $this->Value($cx, $y + 1.38, $racer['class'], $col_w);

    $this->Caption($cx, $y + 1.49, 'Weight limit');
    $this->Value($cx, $y + 1.59, $this->WeightLimit($racer), $col_w);

    $this->Caption($cx, $y + 1.70, 'Lic no');
    $this->Value($cx, $y + 1.80, $this->LicenseNumber($racer), $col_w);

    // Fine print, on a light strip so the background pattern doesn't interfere
    $this->SetFillColor(array(0xff, 0xff, 0xff));
    $this->pdf->SetAlpha(0.65);
    $this->pdf->Rect($fx - 0.03, $y + 1.735, 2.06, 0.205, 'F');
    $this->pdf->SetAlpha(1);
    $this->pdf->SetTextColor(0, 0, 0);
    $this->pdf->SetFont('Arial', '', 5.2);
    $this->pdf->SetXY($fx - 0.03, $y + 1.745);
    $this->pdf->MultiCell(2.06, 0.082,
                          'Authorized to operate one (1) pinewood vehicle at the '
                          .$this->get_option('pack_name').' Derby, subject to the Cub Scout Law.',
                          0, 'L');
    $this->pdf->SetTextColor(0, 0, 0);

    $this->DrawFooterBand($x, $y);
  }
}
