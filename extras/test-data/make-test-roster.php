<?php
// Adds made-up racers to the EXISTING racing groups, with stand-in photos, for
// trying out check-in, the Print Licenses page, scheduling and awards.
//
// Why not use DerbyNet's own "Build Fake Roster"?  That one files its racers in
// new "Fake Lions", "Fake Tigers", ... groups and sets a "fake-racers" flag; when
// you later "Purge racers" on the Set-Up page, the flag makes DerbyNet delete
// EVERY group, subgroup and aggregate, including the ones you set up for real.
// This script uses your real groups and never sets that flag, so a normal purge
// of racers leaves the group structure alone.
//
// Usage (on the server, as the web server's user so file ownership is right):
//   cd /var/www/html/derbynet
//   sudo -u www-data php /path/to/make-test-roster.php [racers_per_group] [outlaw_racers]
//
// Test racers carry the note TESTROSTER and photo files named testroster_*, so
// they are easy to recognize.  To get back to a clean state, restore the backup
// of the database you took first, and delete photos named testroster_* in the
// racer and car photo folders.
//
// Set DERBYNET_CONFIG_DIR to try it against a copy of the database.

chdir(isset($_SERVER['DERBYNET_SITE_DIR']) ? $_SERVER['DERBYNET_SITE_DIR'] : '/var/www/html/derbynet');
set_include_path(getcwd().PATH_SEPARATOR.get_include_path());
$_SESSION = array();
require_once('inc/data.inc');
require_once('inc/permissions.inc');
require_once('inc/action-helpers.inc');
require_once('inc/partitions.inc');
require_once('inc/schema_version.inc');
require_once('inc/sql-script.inc');
require_once(sql_file_path('column-widths'));
require_once('inc/newracer.inc');
require_once('inc/fake_names.inc');
require_once('inc/photo-config.inc');

$per_group = isset($argv[1]) ? max(1, intval($argv[1])) : 8;
$outlaw_count = isset($argv[2]) ? max(0, intval($argv[2])) : 6;

if (read_single_value('SELECT COUNT(*) FROM RegistrationInfo') > 0) {
  fwrite(STDERR, "There are already racers in this database; not adding test racers.\n");
  exit(1);
}
if (read_raceinfo_boolean('fake-racers')) {
  fwrite(STDERR, "The database is marked as holding a DerbyNet fake roster; refusing.\n");
  exit(1);
}

$car_names = array('Lightning Bolt', 'The Blue Streak', 'Rocket Sled', 'Thunder Wedge',
                   'Mighty Mouse', 'Flying Banana', 'Silver Arrow', 'Red Baron',
                   'Night Owl', 'Speedy Gonzales', 'The Pickle', 'Hot Wheels Jr',
                   'Green Machine', 'Wolf Pack', 'Cheddar Chaser', 'Mach 5');
$tints = array(array(70,110,170), array(160,90,90), array(80,140,100), array(150,120,60),
               array(110,90,150), array(60,130,140), array(170,100,140), array(100,100,100));

function make_head_photo($path, $tint) {
  $im = imagecreatetruecolor(480, 640);
  for ($y = 0; $y < 640; $y++) {
    $f = $y / 640;
    imageline($im, 0, $y, 480, $y,
              imagecolorallocate($im, $tint[0] * (1 - .4 * $f), $tint[1] * (1 - .4 * $f), $tint[2] * (1 - .4 * $f)));
  }
  $skin = imagecolorallocate($im, 230, 190, 160);
  imagefilledellipse($im, 240, 700, 520, 420, imagecolorallocate($im, 30, 60, 140));
  imagefilledrectangle($im, 215, 400, 265, 480, $skin);
  imagefilledellipse($im, 240, 300, 220, 270, $skin);
  $black = imagecolorallocate($im, 20, 20, 20);
  imagefilledellipse($im, 200, 280, 22, 22, $black);
  imagefilledellipse($im, 280, 280, 22, 22, $black);
  imagearc($im, 240, 340, 90, 60, 20, 160, $black);
  imagejpeg($im, $path, 90);
  imagedestroy($im);
}

function make_car_photo($path, $tint) {
  $im = imagecreatetruecolor(640, 480);
  imagefill($im, 0, 0, imagecolorallocate($im, 200, 220, 240));
  imagefilledrectangle($im, 0, 330, 640, 480, imagecolorallocate($im, 90, 90, 95));
  $body = imagecolorallocate($im, min(255, $tint[0] + 60), min(255, $tint[1] + 40), min(255, $tint[2] + 40));
  imagefilledpolygon($im, array(80,320, 160,240, 440,230, 560,300, 580,330, 70,335), 6, $body);
  $w = imagecolorallocate($im, 20, 20, 20);
  imagefilledellipse($im, 170, 340, 90, 90, $w);
  imagefilledellipse($im, 470, 340, 90, 90, $w);
  imagefilledellipse($im, 170, 340, 40, 40, imagecolorallocate($im, 180, 180, 180));
  imagefilledellipse($im, 470, 340, 40, 40, imagecolorallocate($im, 180, 180, 180));
  imagejpeg($im, $path, 90);
  imagedestroy($im);
}

$fake_names = all_fake_names();
$groups = array();
foreach ($db->query('SELECT class FROM Classes WHERE constituents = \'\' OR constituents IS NULL'
                    .' ORDER BY sortorder') as $row) {
  $groups[] = $row['class'];
}

$total = 0;
foreach ($groups as $gi => $group) {
  $n = strtolower($group) == 'outlaw' ? $outlaw_count : $per_group;
  for ($r = 0; $r < $n; ++$r) {
    $index = mt_rand(0, count($fake_names) - 1);
    $name = $fake_names[$index];
    array_splice($fake_names, $index, 1);
    if (count($fake_names) == 0) { $fake_names = all_fake_names(); }

    $racerid = insert_new_racer(array('firstname' => $name['firstname'],
                                      'lastname' => $name['lastname'],
                                      'carname' => $car_names[mt_rand(0, count($car_names) - 1)],
                                      'note_from' => 'TESTROSTER',
                                      'partition' => $group));
    $tint = $tints[($racerid + $gi) % count($tints)];
    $head = photo_directory().DIRECTORY_SEPARATOR.'testroster_head_'.$racerid.'.jpg';
    $car = car_photo_directory().DIRECTORY_SEPARATOR.'testroster_car_'.$racerid.'.jpg';
    // Leave a few racers without photos, to see the warnings and placeholders.
    $with_head = ($racerid % 11) != 0;
    $with_car = ($racerid % 7) != 0;
    if ($with_head) { make_head_photo($head, $tint); }
    if ($with_car) { make_car_photo($car, $tint); }
    $stmt = $db->prepare('UPDATE RegistrationInfo SET imagefile = :h, carphoto = :c,'
                         .' passedinspection = :p WHERE racerid = :id');
    $stmt->execute(array(':h' => $with_head ? $head : '', ':c' => $with_car ? $car : '',
                         ':p' => ($racerid % 13) == 0 ? 0 : 1, ':id' => $racerid));
    ++$total;
  }
  echo str_pad($group, 10)." $n racers\n";
}
echo "Added $total test racers (note TESTROSTER) to the existing groups.\n";
