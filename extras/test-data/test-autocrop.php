<?php
// Measures how well auto-cropping of headshots works, using the sample headshots
// that ship in testing/data/headshots.
//
// Those photos are already tight crops, so this builds webcam-style "scenes"
// from them: each face is scaled and placed at a random size and position inside
// a wider 4:3 frame, filled with the photo's own wall color.  Because the
// placement is known, we can tell whether the detector found the face and whether
// the resulting crop shows the whole head with room around it.
//
// Usage:  php test-autocrop.php <old|new> <headshots-dir> [scenes] [site-dir] [flat|textured|raw]
//   old   the detector and crop geometry as shipped upstream (needs OLD_FACEDETECT
//         environment variable: path to a copy of the upstream facedetection.inc)
//   new   this branch's detector and crop geometry (inc/photo-autocrop.inc)
//
// Not part of the web site; run it by hand after changing the auto-crop code.

$mode = isset($argv[1]) ? $argv[1] : 'new';
$dir = isset($argv[2]) ? $argv[2] : 'testing/data/headshots';
$n_scenes = isset($argv[3]) ? intval($argv[3]) : 40;
$site = isset($argv[4]) ? $argv[4] : '/var/www/html/derbynet';
// flat:     the face placed on a flat, wall-colored frame (hardest: no room context)
// textured: the face placed on a soft, varied background (more like a real room)
// raw:      the sample photo as it is, face filling the frame (like a headshot station)
$style = isset($argv[5]) ? $argv[5] : 'flat';

set_include_path($site.PATH_SEPARATOR.get_include_path());  // the site's includes are relative to its root
if ($mode == 'old') {
  require_once(getenv('OLD_FACEDETECT'));
} else {
  require_once($site.'/inc/facedetection.inc');
  require_once($site.'/inc/photo-aspect.inc');
  require_once($site.'/inc/photo-autocrop.inc');
}

// The crop geometry as shipped upstream (action.photo.upload.inc).
function old_widen_and_clip(&$v, &$width, $scale, $max) {
  $original_width = $width;
  $width = round($width * $scale);
  $v = $v - round(($width - $original_width) / 2);
  if ($v < 0) { $width = $width + $v; $v = 0; }
  if ($v + $width > $max) { $width = $max - $v; }
}
function old_crop_box($face, $iw, $ih) {
  $x = round($face['x']); $y = round($face['y']); $w = round($face['width']);
  old_widen_and_clip($x, $w, 1.10, $iw);
  $h = $w;
  old_widen_and_clip($y, $h, 4 / 3, $ih);
  return array($x, $y, $w, $h);
}

mt_srand(42);
$files = glob($dir.'/*.jpg');
sort($files);
shuffle($files);
$files = array_slice($files, 0, $n_scenes);

$stats = array('scenes' => 0, 'found' => 0, 'face_ok' => 0, 'head_in_crop' => 0, 'aspect_ok' => 0,
               'time_total' => 0.0, 'time_max' => 0.0);
$fails = array();

foreach ($files as $file) {
  $src = imagecreatefromjpeg($file);
  $sw = imagesx($src); $sh = imagesy($src);
  $bg = imagecolorat($src, 4, 4);

  // Scene: 1280x960, the headshot placed with height 38%..72% of the frame.
  $W = 1280; $H = 960;
  if ($style == 'raw') { $W = $sw; $H = $sh; }
  $ph = intval($H * (0.38 + mt_rand(0, 34) / 100));
  $pw = intval($sw * $ph / $sh);
  if ($pw > 0.7 * $W) {  // a landscape sample photo: keep it inside the frame
    $pw = intval(0.7 * $W);
    $ph = intval($sh * $pw / $sw);
  }
  $px = mt_rand(intval(0.12 * $W), intval($W - $pw - 0.12 * $W));
  $py = mt_rand(intval(0.06 * $H), max(intval(0.06 * $H) + 1, $H - $ph - intval(0.02 * $H)));
  if ($style == 'raw') {
    $px = 0; $py = 0; $pw = $sw; $ph = $sh;
    $scene = imagecreatetruecolor($W, $H);
    imagecopy($scene, $src, 0, 0, 0, 0, $sw, $sh);
  } else {
    $scene = imagecreatetruecolor($W, $H);
    imagefill($scene, 0, 0, $bg);
    if ($style == 'textured') {
      // A soft, varied backdrop: another sample photo, blurred by shrinking it hard and enlarging it.
      $other = imagecreatefromjpeg($files[mt_rand(0, count($files) - 1)]);
      $tiny = imagecreatetruecolor(24, 18);
      imagecopyresampled($tiny, $other, 0, 0, 0, 0, 24, 18, imagesx($other), imagesy($other));
      imagecopyresampled($scene, $tiny, 0, 0, 0, 0, $W, $H, 24, 18);
      imagedestroy($tiny); imagedestroy($other);
    }
    imagecopyresampled($scene, $src, $px, $py, 0, 0, $pw, $ph, $sw, $sh);
  }

  // Where the head really is, within the placed photo (generous bounds).
  $head = array('x1' => $px + 0.22 * $pw, 'x2' => $px + 0.78 * $pw,
                'y1' => $py + 0.10 * $ph, 'y2' => $py + 0.80 * $ph);

  $t0 = microtime(true);
  if ($mode == 'old') {
    $detector = new FaceDetector();
    $face = $detector->detect_face($scene);
  } else {
    $face = find_headshot_face($scene);  // what the site does
  }
  if ($face && !($face['width'] > 0)) { $face = false; }  // upstream returns a zero-size "face" on failure
  $dt = microtime(true) - $t0;

  $stats['scenes']++;
  $stats['time_total'] += $dt;
  $stats['time_max'] = max($stats['time_max'], $dt);
  $ok_face = false;
  if ($face) {
    $stats['found']++;
    $cx = $face['x'] + $face['width'] / 2;
    $cy = $face['y'] + $face['height'] / 2;
    $ok_face = $cx > $head['x1'] && $cx < $head['x2'] && $cy > $head['y1'] && $cy < $head['y2']
        && $face['width'] > 0.25 * $pw && $face['width'] < 0.85 * $pw;
    if ($ok_face) { $stats['face_ok']++; }

    if ($mode == 'old') {
      list($x, $y, $w, $h) = old_crop_box($face, $W, $H);
    } else {
      list($x, $y, $w, $h) = headshot_crop_box($face, $W, $H);
    }
    if ($h > 0 && abs($w / $h - 0.75) < 0.02) { $stats['aspect_ok']++; }
    // Is the whole head inside the crop?
    if ($x <= $head['x1'] && $x + $w >= $head['x2'] && $y <= $head['y1'] && $y + $h >= $head['y2']) {
      $stats['head_in_crop']++;
    }
  }
  if (!$ok_face) { $fails[] = basename($file).($face ? ' (wrong place/size)' : ' (not found)'); }
  // With DEBUG_DIR set, save each scene with the detected face (red) and the crop (green) drawn on it.
  if (getenv('DEBUG_DIR') && ($mode != 'old')) {
    $red = imagecolorallocate($scene, 255, 0, 0); $green = imagecolorallocate($scene, 0, 255, 0);
    imagesetthickness($scene, max(3, intval($W / 200)));
    if ($face) {
      imagerectangle($scene, $face['x'], $face['y'], $face['x'] + $face['width'], $face['y'] + $face['height'], $red);
      imagerectangle($scene, $x, $y, $x + $w, $y + $h, $green);
    }
    $small = imagecreatetruecolor(400, intval(400 * $H / $W));
    imagecopyresampled($small, $scene, 0, 0, 0, 0, 400, intval(400 * $H / $W), $W, $H);
    imagepng($small, getenv('DEBUG_DIR').'/'.($ok_face ? 'ok' : 'MISS').'-'.basename($file, '.jpg').'.png');
    imagedestroy($small);
  }
  imagedestroy($scene); imagedestroy($src);
}

printf("mode=%s  scenes=%d\n", $mode, $stats['scenes']);
printf("  face found:            %2d / %d\n", $stats['found'], $stats['scenes']);
printf("  found in right place:  %2d / %d\n", $stats['face_ok'], $stats['scenes']);
printf("  whole head in crop:    %2d / %d\n", $stats['head_in_crop'], $stats['scenes']);
printf("  crop is exactly 3:4:   %2d / %d\n", $stats['aspect_ok'], $stats['scenes']);
printf("  detector time:         avg %.2fs, max %.2fs\n", $stats['time_total'] / max(1, $stats['scenes']), $stats['time_max']);
if ($fails) { echo "  misses: ".implode(', ', array_slice($fails, 0, 12)).(count($fails) > 12 ? ', ...' : '')."\n"; }
