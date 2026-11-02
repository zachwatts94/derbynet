<?php
// Sends one racer's license card to the card printer.
//
// $_POST['racerid']
// $_POST['sides']   1 (front only) or 2 (front and back, the default)
// $_POST['copies']  1 to 5, default 1
// $_POST['force']   required to print again for a racer who already has a card
//                   in the print ledger

require_once('inc/card-printing.inc');

if (!have_permission(ASSIGN_RACER_IMAGE_PERMISSION)) {
  json_not_authorized();
} else if (!isset($_POST['racerid']) || !is_numeric($_POST['racerid'])) {
  json_failure('args', 'A racerid is required.');
} else {
  $racerid = intval($_POST['racerid']);
  $sides = @$_POST['sides'] == 1 ? 1 : 2;
  $copies = isset($_POST['copies']) ? max(1, min(5, intval($_POST['copies']))) : 1;

  if (read_single_value('SELECT COUNT(*) FROM RegistrationInfo WHERE racerid = :racerid',
                        array(':racerid' => $racerid), 0) == 0) {
    json_failure('nosuchracer', 'No such racer.');
  } else {
    card_print_jobs_ready();
    $already = read_single_value("SELECT COUNT(*) FROM PrintJobs"
                                 ." WHERE racerid = :racerid AND status = 'submitted'",
                                 array(':racerid' => $racerid), 0);
    if ($already > 0 && empty($_POST['force'])) {
      json_out('printed_count', $already);
      json_failure('already-printed',
                   'A card has already been sent for this racer.  Print again only if the first one was spoiled.');
    } else {
      list($ok, $message, $cups_id) = card_submit($racerid, $sides, $copies);
      if ($ok) {
        json_out('job', $cups_id);
        json_success();
      } else {
        json_failure('print', $message);
      }
    }
  }
}
?>
