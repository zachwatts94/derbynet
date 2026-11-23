<?php
require_once('inc/replay.inc');

if (have_permission(CONTROL_RACE_PERMISSION)) {
  send_replay_LIVE(@$_POST['on'] == '1', isset($_POST['address']) ? $_POST['address'] : '');
  json_success();
} else {
  json_not_authorized();
}
?>
