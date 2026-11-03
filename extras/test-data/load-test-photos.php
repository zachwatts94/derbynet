<?php
// Gives the test racers (note TESTROSTER, from make-test-roster.php) real
// photos from DerbyNet's own test data (testing/data/headshots and
// testing/data/carphotos), and runs the site's auto-crop on each headshot, the
// same code and steps as an upload with "Auto-crop" on.  Racers that have no
// headshot or car photo (deliberately, from make-test-roster.php) stay that way.
//
// Usage (on the server, as the web server's user so file ownership is right):
//   cd /var/www/html/derbynet
//   sudo -u www-data php load-test-photos.php <testing/data directory> [max racers]
//
// Prints one line per racer.  Photo files are named testroster_real_*, so they
// are easy to find and remove; restoring a backup of the database undoes the rest.

chdir(isset($_SERVER['DERBYNET_SITE_DIR']) ? $_SERVER['DERBYNET_SITE_DIR'] : '/var/www/html/derbynet');
set_include_path(getcwd().PATH_SEPARATOR.get_include_path());
$_SESSION = array();
require_once('inc/data.inc');
require_once('inc/permissions.inc');
require_once('inc/action-helpers.inc');
require_once('inc/photo-config.inc');
require_once('inc/photo-autocrop.inc');

$data_dir = isset($argv[1]) ? rtrim($argv[1], '/') : 'testing/data';
$max_racers = isset($argv[2]) ? intval($argv[2]) : 0;  // 0 = all
$heads = glob($data_dir.'/headshots/*.jpg');
$cars = glob($data_dir.'/carphotos/*.jpg');
sort($heads);
sort($cars);
if (empty($heads) || empty($cars)) {
  fwrite(STDERR, "No photos found under $data_dir/headshots and $data_dir/carphotos.\n");
  exit(1);
}
mt_srand(11);
shuffle($heads);
shuffle($cars);

$racers = array();
foreach ($db->query("SELECT racerid, firstname, lastname, carnumber, imagefile, carphoto FROM RegistrationInfo"
                    ." WHERE note = 'TESTROSTER' ORDER BY racerid") as $row) {
  $racers[] = $row;
}
if (empty($racers)) {
  fwrite(STDERR, "No test racers (note TESTROSTER) in this database.\n");
  exit(1);
}
if ($max_racers > 0) {
  $racers = array_slice($racers, 0, $max_racers);
}

$head_repo = photo_repository('head');
$car_repo = photo_repository('car');
$stats = array('heads' => 0, 'cropped' => 0, 'no_face' => 0, 'cars' => 0, 'seconds' => 0.0);
$old_files = array();

foreach ($racers as $i => $r) {
  $line = sprintf("#%-4s %-18s", $r['carnumber'], substr($r['firstname'].' '.$r['lastname'], 0, 18));
  $update = array();

  // Headshot (only for racers that have one now).
  if ($r['imagefile'] !== '') {
    $src = $heads[$i % count($heads)];
    $name = 'testroster_real_head_'.$r['racerid'].'.jpg';
    $path = $head_repo->lookup(RENDER_ORIGINAL)->file_path($name);
    copy($src, $path);
    normalize_image_orientation($path);
    list($w0, $h0, $type) = getimagesize($path);

    $t0 = microtime(true);
    $original = open_image($path, $type);
    $face = $original ? find_headshot_face($original) : false;
    $dt = microtime(true) - $t0;
    $stats['seconds'] += $dt;
    $stats['heads']++;
    $final = $path;
    if ($face && $face['width'] > 0) {
      list($x, $y, $w, $h) = headshot_crop_box($face, imagesx($original), imagesy($original));
      $cropped_path = $head_repo->lookup(RENDER_CROPPED)->target_file_path($name);
      $cropped = imagecreatetruecolor($w, $h);
      imagecopy($cropped, $original, 0, 0, $x, $y, $w, $h);
      write_image($cropped, $cropped_path, $type);
      imagedestroy($cropped);
      $final = $head_repo->lookup(RENDER_CROPPED)->file_path($name);
      $stats['cropped']++;
      $line .= sprintf(" head %4dx%-4d -> face found, cropped %dx%d (%.1fs)", $w0, $h0, $w, $h, $dt);
    } else {
      $stats['no_face']++;
      $line .= sprintf(" head %4dx%-4d -> NO FACE FOUND, left uncropped (%.1fs)", $w0, $h0, $dt);
    }
    if ($original) { imagedestroy($original); }
    $update['imagefile'] = $final;
    $old_files[] = $r['imagefile'];
  }

  // Car photo (only for racers that have one now); cars are not auto-cropped.
  if ($r['carphoto'] !== '') {
    $src = $cars[$i % count($cars)];
    $name = 'testroster_real_car_'.$r['racerid'].'.jpg';
    $path = $car_repo->lookup(RENDER_ORIGINAL)->file_path($name);
    copy($src, $path);
    normalize_image_orientation($path);
    $update['carphoto'] = $path;
    $old_files[] = $r['carphoto'];
    $stats['cars']++;
  }

  foreach ($update as $column => $value) {
    $stmt = $db->prepare("UPDATE RegistrationInfo SET $column = :v WHERE racerid = :id");
    $stmt->execute(array(':v' => $value, ':id' => $r['racerid']));
  }
  echo $line."\n";
  flush();
}

// The stand-in cartoon files from make-test-roster.php are no longer used.
foreach ($old_files as $f) {
  if (strpos(basename($f), 'testroster_head_') === 0 || strpos(basename($f), 'testroster_car_') === 0) {
    @unlink($f);
  }
}

printf("\n%d headshots: %d cropped around a face, %d with no face found (left uncropped); %d car photos.\n",
       $stats['heads'], $stats['cropped'], $stats['no_face'], $stats['cars']);
printf("Auto-crop time: %.1fs in all, %.1fs per headshot on average.\n",
       $stats['seconds'], $stats['seconds'] / max(1, $stats['heads']));
