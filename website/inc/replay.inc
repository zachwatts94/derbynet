<?php

// replay_last_contact

// replay_state:
define('REPLAY_NOT_CONNECTED', 1);
define('REPLAY_CONNECTED', 2);  // See replay_last_contact.
define('REPLAY_TROUBLE', 3);   // See replay_trouble value

// replay_reported - see $status_array, below.
define('REPLAY_REPORTED_PLAYING', 2);

// A registration request causes a HELLO message to be sent, and the state
// becomes either CONNECTED or TROUBLE depending on the outcome.

// replay_last_contact gets updated with successful message sent.  replay_trouble
// gets set to the error message for an unsuccessful message send.

// REPLAY_CONNECTED or REPLAY_TROUBLE become REPLAY_NOT_CONNECTED if
// REPLAY_TIMEOUT_LIMIT passes since replay_last_contact.
//
// REPLAY_TROUBLE causes message sends to be ignored (except re-registration
// attempts).  Re-registration attempt also sets replay_last_contact.


require_once('inc/data.inc');
require_once('inc/ws-trigger.inc');

define('REPLAY_TIMEOUT_LIMIT', 10);

function get_replay() {
  $state = read_raceinfo('replay_state', REPLAY_NOT_CONNECTED);
  $reported_status = read_raceinfo('replay_reported', 0);
  $last_contact = read_raceinfo('replay_last_contact', 0);
  if ($last_contact + REPLAY_TIMEOUT_LIMIT < time()) {
    $state = REPLAY_NOT_CONNECTED;
  }
  $trouble = read_raceinfo('replay_trouble');

  $status_array = array(
         0 => 'Ready',
         1 => 'Recording',
         2 => 'Playing',
         -1 => 'Connecting...',
         -2 => 'No video source chosen',
         -3 => 'No audio source chosen',
         -4 => 'Recording error');

  return array('state' => $state,
               'status' => $reported_status,
               'reported_status' => $status_array[$reported_status],
               'last_contact' => $last_contact,
               'trouble' => $trouble);
}

function send_replay_message($cmd) {
  global $homedir;
  global $db;
  $replay = get_replay();
  if ($replay['state'] == REPLAY_CONNECTED) {
    if (maybe_send_ws_trigger(array('url' => read_raceinfo('_websocket_url'),
                                    'topic' => 'replay-commands',
                                    'type' => 'replay-cmd',
                                    'cmd' => $cmd))) {
      // Prefer the websocket if it's available
    } else if (isset($homedir) && is_dir($homedir) && is_writable($homedir)) {
      $fp = fopen($homedir.DIRECTORY_SEPARATOR.'replay_queue', 'c+');
      try {
        if (flock($fp, LOCK_EX)) {
          if (fseek($fp, 0, SEEK_END) == 0) {
            fwrite($fp, $cmd."\n");
          }
        }
      } finally {
        fclose($fp);
      }
    } else {
      // TODO: The trouble here is that the RaceInfo table's ItemValue may not be
      // long enough for what we need, including file names
      try {
        if ($db->beginTransaction()) {
          try {
            write_raceinfo('replay_queue', read_raceinfo('replay_queue', '').$cmd."\n");
            $db->commit();
          } catch (PDOException $e) {
            $db->rollBack();
            error_log("send_replay_message: transaction rolled back: ".$e->getMessage());
          }
        } else {
          error_log("send_replay_message: beginTransaction returns false");
        }
      } catch (PDOException $e) {
        error_log("send_replay_message: beginTransaction throws ".$e->getMessage());
      }
      try {
        $db->setAttribute(PDO::ATTR_AUTOCOMMIT, true);
      } catch (PDOException $e) {
        error_log("send_replay_message: set autocommit throws ".$e->getMessage());
      }
    }
    return true;
  } else {
    return false;
  }
}

function drain_queue() {
  global $homedir;
  global $db;
  $queue = '';
  if (isset($homedir) && is_dir($homedir) && is_writable($homedir)) {
    $fp = fopen($homedir.DIRECTORY_SEPARATOR.'replay_queue', 'c+');
    try {
      if (flock($fp, LOCK_EX)) {
        $queue = stream_get_contents($fp);
        ftruncate($fp, 0);
      }
    } finally {
      fclose($fp);
    }
  } else {
    $db->beginTransaction();
    $queue = read_raceinfo('replay_queue');
    write_raceinfo('replay_queue', '');
    $db->commit();
    try {
      $db->setAttribute(PDO::ATTR_AUTOCOMMIT, true);
    } catch (PDOException $p) {
    }
  }

  return array_filter(explode("\n", $queue), 'not_empty_string');
}

function send_replay_TEST() {
  // test <skipback_seconds> <repeat> <rate>
  $skipback = read_raceinfo('replay-skipback', '4000');
  $num_showings = read_raceinfo('replay-num-showings', '2');
  $rate = read_raceinfo('replay-rate', '50');
  send_replay_message("TEST ".$skipback." ".$num_showings." ".$rate);
}

// "START video_name_root  -- start recording video\r\n"
function send_replay_START(&$current) {
  send_replay_message("START ".@$current['class']
                      .'_Round'.@$current['round']
                      .'_Heat'.sprintf("%02d", @$current['heat']));
}

// "REPLAY skipback showings rate end_back tail  -- stop recording if recording; playback\r\n"
//
// end_back_ms and tail_ms (both default 0) say where the replay should end:
// end_back_ms milliseconds before the moment this message arrives (when the
// last car actually crossed the finish line), plus tail_ms after that.  The
// replay shows skipback ms leading up to that point.
function send_replay_REPLAY($end_back_ms = 0, $tail_ms = 0) {
  $skipback = read_raceinfo('replay-skipback', '4000');
  $num_showings = read_raceinfo('replay-num-showings', '2');
  $rate = read_raceinfo('replay-rate', '50');
  send_replay_message("REPLAY ".$skipback." ".$num_showings." ".$rate
                      ." ".intval($end_back_ms)." ".intval($tail_ms));
}

// How long ago the last car crossed the finish line, in milliseconds, given
// the timer's heat results ($results as from parse_heat_results).
//
// When every lane has a time, the timer reports as the last car finishes, so
// the answer is 0.  When some lane did not finish, the timer reports only when
// its time-out (replay-timer-timeout seconds, counted from the start of the
// heat) expires, so the last car to finish crossed (timeout - its time) ago.
function replay_end_back_ms($results) {
  $timeout = floatval(read_raceinfo('replay-timer-timeout', '10'));
  $slowest_finisher = 0.0;
  $any_unfinished = false;
  foreach ($results as $r) {
    $t = isset($r['time']) ? floatval($r['time']) : 0.0;
    if ($t <= 0 || $t >= 9.99) {
      $any_unfinished = true;
    } else {
      $slowest_finisher = max($slowest_finisher, $t);
    }
  }
  if (!$any_unfinished || $slowest_finisher <= 0 || $slowest_finisher >= $timeout) {
    return 0;
  }
  return intval(round(($timeout - $slowest_finisher) * 1000));
}

// "CANCEL  -- cancel recording\r\n"
function send_replay_CANCEL() {
  send_replay_message("CANCEL");
}

// RACE_STARTS skipback showings rate -- induce a replay in replay-skipback less epsilon
function send_replay_RACE_STARTS() {
  $skipback = read_raceinfo('replay-skipback', '4000');
  $num_showings = read_raceinfo('replay-num-showings', '2');
  $rate = read_raceinfo('replay-rate', '50');
  send_replay_message("RACE_STARTS $skipback $num_showings $rate");
}


// "LIVE on|off [address]  -- show the replay machine's camera live, full screen\r\n"
// With an address, only the replay page loaded with that address responds.
function send_replay_LIVE($on, $address = '') {
  $address = preg_replace('/[^A-Za-z0-9._-]/', '', $address);
  send_replay_message("LIVE ".($on ? "on" : "off").($address == '' ? '' : " ".$address));
}

?>
