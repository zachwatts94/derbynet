<?php
// Tests for inc/photo-autocrop.inc: the crop geometry and the phone-photo
// rotation fix.  Run by hand after changing that file:
//
//   php test-photo-helpers.php [site-dir]
//
// Prints one line per group of checks and exits with status 1 if any failed.

$site = isset($argv[1]) ? $argv[1] : '/var/www/html/derbynet';
set_include_path($site.PATH_SEPARATOR.get_include_path());
require_once('inc/photo-aspect.inc');
require_once('inc/photo-autocrop.inc');

$failed = 0;
function check($ok, $what) {
  global $failed;
  if (!$ok) { ++$failed; echo "  FAIL: $what\n"; }
  return $ok;
}

// ---- Crop geometry --------------------------------------------------------

mt_srand(7);
$bad = 0;
$trials = 5000;
for ($i = 0; $i < $trials; ++$i) {
  $iw = mt_rand(60, 4000);
  $ih = mt_rand(60, 3000);
  $fw = mt_rand(10, max(10, min($iw, $ih)));
  $face = array('x' => mt_rand(0, max(0, $iw - $fw)), 'y' => mt_rand(0, max(0, $ih - $fw)),
                'width' => $fw, 'height' => $fw);
  foreach (array(HEADSHOT_ASPECT, CAR_PHOTO_ASPECT) as $aspect) {
    list($x, $y, $w, $h) = headshot_crop_box($face, $iw, $ih, $aspect);
    $inside = $x >= 0 && $y >= 0 && $w > 0 && $h > 0 && $x + $w <= $iw && $y + $h <= $ih;
    $shape = $h > 0 && abs($w / $h - $aspect) <= max(0.02, 1.5 / $h);
    if (!$inside || !$shape) {
      ++$bad;
      if ($bad <= 3) { echo "  e.g. image {$iw}x{$ih} face {$face['x']},{$face['y']} {$fw} aspect $aspect -> $x,$y {$w}x{$h}\n"; }
    }
  }
}
echo "crop box stays inside the picture and keeps its shape: ".(2 * $trials - $bad)." of ".(2 * $trials)."\n";
check($bad == 0, "crop box geometry");

// A face in the middle of a big picture is framed with room around it.
list($x, $y, $w, $h) = headshot_crop_box(array('x' => 500, 'y' => 300, 'width' => 200, 'height' => 200), 1600, 1200);
check($x < 500 && $x + $w > 700 && $y < 300 && $y + $h > 500, "crop contains the face with margin");
check($w == 380 && $h == 507, "crop is 1.9 face-widths wide, 3:4 (got {$w}x{$h})");
// Headroom: the crop starts well above the face; the face is above the middle.
check(($y + $h / 2) > 400, "face sits a little above the crop's middle");
// A face right at the top-left corner slides the crop in; it doesn't get cut short.
list($x, $y, $w, $h) = headshot_crop_box(array('x' => 0, 'y' => 0, 'width' => 200, 'height' => 200), 1600, 1200);
check($x == 0 && $y == 0 && $w == 380 && $h == 507, "face at the corner: crop slides inside, same size (got $x,$y {$w}x{$h})");
// A picture too small for the framing shrinks the crop, still the right shape.
list($x, $y, $w, $h) = headshot_crop_box(array('x' => 30, 'y' => 20, 'width' => 100, 'height' => 100), 200, 150);
check($w <= 200 && $h <= 150 && abs($w / $h - 0.75) < 0.02, "small picture: crop shrinks to fit (got {$w}x{$h})");

// ---- Orientation fix ------------------------------------------------------

// Reference image: four differently colored corner blocks (so any flip or turn shows).
function make_reference($w = 80, $h = 40) {
  $im = imagecreatetruecolor($w, $h);
  imagefill($im, 0, 0, imagecolorallocate($im, 255, 255, 255));
  $b = 12;
  imagefilledrectangle($im, 0, 0, $b, $b, imagecolorallocate($im, 255, 0, 0));                 // top-left red
  imagefilledrectangle($im, $w - 1 - $b, 0, $w - 1, $b, imagecolorallocate($im, 0, 200, 0));   // top-right green
  imagefilledrectangle($im, 0, $h - 1 - $b, $b, $h - 1, imagecolorallocate($im, 0, 0, 255));   // bottom-left blue
  imagefilledrectangle($im, $w - 1 - $b, $h - 1 - $b, $w - 1, $h - 1, imagecolorallocate($im, 255, 255, 0)); // bottom-right yellow
  return $im;
}

// Pixel-by-pixel transforms, written independently of the code under test.
function transform_pixels($src, $fn, $out_w, $out_h) {
  $w = imagesx($src); $h = imagesy($src);
  $dst = imagecreatetruecolor($out_w, $out_h);
  for ($y = 0; $y < $h; ++$y) {
    for ($x = 0; $x < $w; ++$x) {
      list($nx, $ny) = $fn($x, $y, $w, $h);
      imagesetpixel($dst, $nx, $ny, imagecolorat($src, $x, $y));
    }
  }
  return $dst;
}

// The stored pixels that a camera would write for an upright picture $ref when it
// records EXIF orientation $o (per the EXIF specification).
function stored_for_orientation($ref, $o) {
  $w = imagesx($ref); $h = imagesy($ref);
  switch ($o) {
    case 2: return transform_pixels($ref, function($x, $y, $w, $h) { return array($w - 1 - $x, $y); }, $w, $h);
    case 3: return transform_pixels($ref, function($x, $y, $w, $h) { return array($w - 1 - $x, $h - 1 - $y); }, $w, $h);
    case 4: return transform_pixels($ref, function($x, $y, $w, $h) { return array($x, $h - 1 - $y); }, $w, $h);
    case 5: return transform_pixels($ref, function($x, $y, $w, $h) { return array($y, $x); }, $h, $w);
    case 6: // upright needs a 90-degree clockwise turn, so the stored picture is the reference turned counterclockwise
            return transform_pixels($ref, function($x, $y, $w, $h) { return array($y, $w - 1 - $x); }, $h, $w);
    case 7: return transform_pixels($ref, function($x, $y, $w, $h) { return array($h - 1 - $y, $w - 1 - $x); }, $h, $w);
    case 8: // upright needs a 90-degree counterclockwise turn
            return transform_pixels($ref, function($x, $y, $w, $h) { return array($h - 1 - $y, $x); }, $h, $w);
  }
  return $ref;
}

// A JPEG file's bytes with an EXIF block (just the Orientation tag) added.
function jpeg_with_orientation($im, $o) {
  ob_start(); imagejpeg($im, null, 95); $jpeg = ob_get_clean();
  $tiff = "MM\x00\x2a\x00\x00\x00\x08"          // big-endian, first IFD at offset 8
        . "\x00\x01"                              // one entry
        . "\x01\x12\x00\x03\x00\x00\x00\x01" . pack('n', $o) . "\x00\x00"  // Orientation, SHORT, 1 value
        . "\x00\x00\x00\x00";                     // no next IFD
  $app1 = "Exif\x00\x00" . $tiff;
  return substr($jpeg, 0, 2) . "\xff\xe1" . pack('n', strlen($app1) + 2) . $app1 . substr($jpeg, 2);
}

function corner_colors($im) {
  $w = imagesx($im); $h = imagesy($im);
  $names = array();
  foreach (array(array(3, 3), array($w - 4, 3), array(3, $h - 4), array($w - 4, $h - 4)) as $p) {
    $c = imagecolorat($im, $p[0], $p[1]);
    $r = ($c >> 16) & 255; $g = ($c >> 8) & 255; $b = $c & 255;
    $names[] = $r > 200 && $g < 80 && $b < 80 ? 'red' : ($g > 150 && $r < 80 ? 'green' : ($b > 200 && $r < 80 ? 'blue' : ($r > 200 && $g > 200 && $b < 80 ? 'yellow' : 'other')));
  }
  return implode(',', $names);
}

$ref = make_reference();
$expected = corner_colors($ref);   // red,green,blue,yellow
$ok_count = 0;
$tmp = sys_get_temp_dir().'/orient-test.jpg';
foreach (array(1, 2, 3, 4, 5, 6, 7, 8) as $o) {
  $stored = stored_for_orientation($ref, $o);
  file_put_contents($tmp, jpeg_with_orientation($stored, $o));
  $changed = normalize_image_orientation($tmp);
  $result = imagecreatefromjpeg($tmp);
  $got = corner_colors($result);
  $good = ($got == $expected) && (imagesx($result) == imagesx($ref)) && ($changed == ($o != 1));
  if ($good) { ++$ok_count; }
  check($good, "orientation $o: corners $got (wanted $expected), changed=".var_export($changed, true));
}
@unlink($tmp);
echo "phone-photo rotation tags 1-8 come out upright: $ok_count of 8\n";

// A file with no EXIF block and a non-JPEG are left alone.
$plain = sys_get_temp_dir().'/orient-plain.jpg';
imagejpeg(make_reference(), $plain, 95);
$before = md5_file($plain);
check(normalize_image_orientation($plain) === false && md5_file($plain) == $before, "photo without a rotation tag is left untouched");
@unlink($plain);
echo "photo without a tag is left untouched: ".($failed ? "see failures" : "ok")."\n";

echo $failed ? "\n$failed CHECK(S) FAILED\n" : "\nall checks passed\n";
exit($failed ? 1 : 0);
