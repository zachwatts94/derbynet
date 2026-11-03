<?php
// Reports the card printer's state and how many cards have been sent per racer.

require_once('inc/card-printing.inc');

if (!have_permission(ASSIGN_RACER_IMAGE_PERMISSION)) {
  json_not_authorized();
} else {
  list($state, $text, $queued) = card_printer_status();
  json_out('printer', array('name' => card_printer_name(),
                            'state' => $state,
                            'text' => $text,
                            'queued' => $queued));

  card_print_jobs_ready();
  $printed = array();
  foreach ($db->query("SELECT racerid, COUNT(*) AS n, MAX(submitted) AS last"
                      ." FROM PrintJobs WHERE status = 'submitted' GROUP BY racerid") as $row) {
    $printed[$row['racerid']] = array('count' => intval($row['n']),
                                      'last' => intval($row['last']));
  }
  json_out('printed', (object) $printed);

  // Racers whose headshot isn't the shape the card uses (it was never cropped, or
  // auto-crop found no face): the card then trims it from the middle, which can
  // cut off a face that isn't centered.
  require_once('inc/photo-aspect.inc');
  $uncropped = array();
  foreach ($db->query("SELECT racerid, imagefile FROM RegistrationInfo WHERE imagefile <> ''") as $row) {
    $size = @getimagesize($row['imagefile']);
    if ($size && $size[1] > 0 && abs(($size[0] / $size[1]) / HEADSHOT_ASPECT - 1) > 0.03) {
      $uncropped[] = intval($row['racerid']);
    }
  }
  json_out('uncropped', $uncropped);
  json_success();
}
?>
