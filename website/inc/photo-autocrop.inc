<?php
// Helpers for cropping headshots automatically.

require_once('inc/photo-aspect.inc');

// The crop is this many times as wide as the face the detector finds.  The
// detector's box runs from about the brow to the chin and covers the width of the
// face, so 1.9 leaves room for hair, ears, neck and the tops of the shoulders.
define('HEADSHOT_FRAME_WIDTH', 1.9);

// Where the middle of the detected face sits, measured down from the top of the
// crop (0.5 would be dead center).  A little above center leaves room below the
// chin for the neck and shoulders, and still some headroom above the hair.
define('HEADSHOT_FACE_FROM_TOP', 0.44);

// Works out the crop for a face found at $face (array with x, y, width, height)
// in a picture of $img_w by $img_h pixels.  Returns array(x, y, width, height),
// whole pixels, always in the requested shape ($aspect = width / height) and
// always inside the picture: if the framing would run off an edge, the crop
// slides back inside rather than being cut short, and if the picture is too small
// for the framing, the crop shrinks (keeping its shape).
function headshot_crop_box($face, $img_w, $img_h, $aspect = HEADSHOT_ASPECT) {
  $face_w = max(1, $face['width']);
  $center_x = $face['x'] + $face['width'] / 2;
  $center_y = $face['y'] + $face['height'] / 2;

  $crop_w = $face_w * HEADSHOT_FRAME_WIDTH;
  $crop_h = $crop_w / $aspect;

  $shrink = min(1.0, $img_w / $crop_w, $img_h / $crop_h);
  $crop_w *= $shrink;
  $crop_h *= $shrink;

  $x = $center_x - $crop_w / 2;
  $y = $center_y - HEADSHOT_FACE_FROM_TOP * $crop_h;
  $x = max(0, min($x, $img_w - $crop_w));
  $y = max(0, min($y, $img_h - $crop_h));

  $w = min($img_w, (int) round($crop_w));
  $h = (int) round($w / $aspect);
  if ($h > $img_h) {
    $h = $img_h;
    $w = (int) round($h * $aspect);
  }
  $x = (int) round($x);
  $y = (int) round($y);
  if ($x + $w > $img_w) { $x = $img_w - $w; }
  if ($y + $h > $img_h) { $y = $img_h - $h; }
  return array(max(0, $x), max(0, $y), $w, $h);
}

// Looks for the face in a headshot picture ($img is a GD image).  Returns array
// with x, y, width, height in the picture's own pixels, or false.
//
// First the whole picture is searched.  If no face turns up, the search is
// repeated on a smaller and smaller window around the center of the picture: a
// face that is only a small part of a wide picture is much easier to recognize
// once the surroundings are trimmed away.  This suits a headshot station, where
// the person is sitting in front of the camera.
//
// The total time is held to about $budget_sec seconds (a web request is allowed
// 30), and each pass gets a share of it.
function find_headshot_face($img, $budget_sec = 15) {
  require_once('inc/facedetection.inc');
  $w = imagesx($img);
  $h = imagesy($img);
  $deadline = microtime(true) + $budget_sec;
  $detector = new FaceDetector();

  foreach (array(1.0, 0.65, 0.45) as $fraction) {
    $remaining = $deadline - microtime(true);
    if ($remaining < 1) {
      break;
    }
    $detector->set_time_limit_sec(max(1, min(6, (int) floor($remaining))));

    $sub_w = (int) round($w * $fraction);
    $sub_h = (int) round($h * $fraction);
    $left = (int) round(($w - $sub_w) / 2);
    $top = (int) round(($h - $sub_h) / 2);
    if ($fraction < 1.0) {
      $sub = imagecreatetruecolor($sub_w, $sub_h);
      imagecopy($sub, $img, 0, 0, $left, $top, $sub_w, $sub_h);
      $face = $detector->detect_face($sub);
      imagedestroy($sub);
    } else {
      $face = $detector->detect_face($img);
    }
    if ($face && $face['width'] > 0) {
      return array('x' => $face['x'] + $left, 'y' => $face['y'] + $top,
                   'width' => $face['width'], 'height' => $face['height']);
    }
  }
  return false;
}

// Phone cameras usually save a photo's pixels in the sensor's orientation and add
// a tag saying how to turn it upright.  Browsers honor that tag, but the image
// library and the license cards do not, so a portrait phone photo would be
// cropped, and printed, on its side.  This turns the pixels upright and rewrites
// the file (without the tag).  Returns true if the file was changed.
function normalize_image_orientation($path) {
  if (!function_exists('exif_read_data') || !function_exists('imagerotate')) {
    return false;
  }
  $info = @getimagesize($path);
  if (!$info || $info[2] != IMAGETYPE_JPEG) {
    return false;
  }
  $exif = @exif_read_data($path);
  $orientation = ($exif && isset($exif['Orientation'])) ? intval($exif['Orientation']) : 1;
  if ($orientation < 2 || $orientation > 8) {
    return false;
  }

  // A 12-megapixel photo takes about 50 MB to hold, and turning it needs a second copy.
  @ini_set('memory_limit', '512M');
  $im = @imagecreatefromjpeg($path);
  if (!$im) {
    return false;
  }
  // imagerotate turns counterclockwise.
  switch ($orientation) {
    case 2: imageflip($im, IMG_FLIP_HORIZONTAL); break;
    case 3: $im = imagerotate($im, 180, 0); break;
    case 4: imageflip($im, IMG_FLIP_VERTICAL); break;
    case 5: imageflip($im, IMG_FLIP_HORIZONTAL); $im = imagerotate($im, 90, 0); break;
    case 6: $im = imagerotate($im, -90, 0); break;
    case 7: imageflip($im, IMG_FLIP_HORIZONTAL); $im = imagerotate($im, -90, 0); break;
    case 8: $im = imagerotate($im, 90, 0); break;
  }
  $ok = $im && imagejpeg($im, $path, 92);
  if ($im) {
    imagedestroy($im);
  }
  return $ok;
}
?>
