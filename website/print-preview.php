<?php
// Returns a PNG preview of one racer's license card, front and back side by
// side, drawn from the very same PDF that would be sent to the card printer.
//
// $_GET['racerid']
// $_GET['sides']  1 or 2 (default 2)

session_start();
require_once('inc/data.inc');
require_once('inc/authorize.inc');
session_write_close();

require_permission(ASSIGN_RACER_IMAGE_PERMISSION);

require_once('inc/card-printing.inc');

function preview_failure($message) {
  http_response_code(500);
  header('Content-Type: text/plain; charset=utf-8');
  echo $message."\n";
  exit(0);
}

if (!isset($_GET['racerid']) || !is_numeric($_GET['racerid'])) {
  preview_failure('A racerid is required.');
}
$sides = @$_GET['sides'] == 1 ? 1 : 2;

$pdf = card_pdf($_GET['racerid'], $sides);
if ($pdf === false) {
  preview_failure('Could not generate the card.');
}
$pngs = card_pdf_to_pngs($pdf, 200);
if ($pngs === false) {
  preview_failure('Could not render the card preview.');
}

$images = array();
foreach ($pngs as $png) {
  $images[] = imagecreatefromstring($png);
}
$gap = 40;
$width = 0;
$height = 0;
foreach ($images as $im) {
  $width += imagesx($im);
  $height = max($height, imagesy($im));
}
$width += $gap * (count($images) - 1);

$sheet = imagecreatetruecolor($width, $height);
imagefill($sheet, 0, 0, imagecolorallocate($sheet, 0xd0, 0xd4, 0xdc));
$x = 0;
foreach ($images as $im) {
  imagecopy($sheet, $im, $x, 0, 0, 0, imagesx($im), imagesy($im));
  $x += imagesx($im) + $gap;
  imagedestroy($im);
}

header('Content-Type: image/png');
header('Cache-Control: no-store');
imagepng($sheet);
imagedestroy($sheet);
?>
