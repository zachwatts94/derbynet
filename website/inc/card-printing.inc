<?php
// Server-side printing of racer license cards to a CUPS queue (the Zebra ZC300).
//
// The card artwork comes from the same render-document.php that makes the
// printables PDFs, so what the review page previews is exactly what gets sent.
// The PDF is handed to CUPS with `lp`; CUPS and the Zebra driver do the rest.

require_once('inc/data.inc');
require_once('inc/sql-script.inc');

define('CARD_DOCUMENT_CLASS', 'CardLicenseDocument');

// The CUPS queue to print to.  Override by setting RaceInfo 'card-printer'.
function card_printer_name() {
  $name = read_raceinfo('card-printer');
  return $name ? $name : 'ZC300';
}

// Per-job printer options for the Zebra driver.  A card is either front-only
// (YMCKO) or full color on both sides.  (Two-sided color uses a full set of ribbon
// panels for each side, so a 300-image ribbon makes about 150 two-sided cards.)
function card_print_options($sides) {
  $options = array('PageSize=CR80', 'RibbonName=0YMCKO');
  if ($sides == 2) {
    $options[] = 'DualSidePrinting=1true';
    $options[] = 'RibbonCombination=1FrontYmckoBackYmcko';
  } else {
    $options[] = 'DualSidePrinting=0false';
    $options[] = 'RibbonCombination=0FrontYmcko';
  }
  return $options;
}

function card_print_jobs_ready() {
  global $db;
  try {
    $db->query('SELECT 1 FROM PrintJobs LIMIT 1');
  } catch (PDOException $p) {
    if (!is_no_such_table_exception($p)) {
      throw $p;
    }
    global $dbtype;
    $statements = @include(sql_file_path('print-jobs-table'));
    if (!is_array($statements)) {
      return false;
    }
    foreach ($statements as $sql) {
      $db->exec($sql);
    }
  }
  return true;
}

// Returns the PDF for one racer's card as a binary string, or false.  This
// asks this same server for render-document.php, so there's one code path for
// drawing cards.
function card_pdf($racerid, $sides) {
  $ssl = !empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off';
  $host = isset($_SERVER['HTTP_HOST']) ? $_SERVER['HTTP_HOST'] : 'localhost';
  $port = isset($_SERVER['SERVER_PORT']) ? $_SERVER['SERVER_PORT'] : ($ssl ? 443 : 80);
  $dir = rtrim(dirname($_SERVER['SCRIPT_NAME']), '/');
  $url = ($ssl ? 'https' : 'http').'://127.0.0.1:'.$port.$dir
       .'/render-document.php/racer/'.CARD_DOCUMENT_CLASS
       .'?ids='.intval($racerid)
       .'&options='.urlencode(json_encode(array('two_sided' => $sides == 2)));
  $context = stream_context_create(array(
      'http' => array('header' => "Host: $host\r\n", 'timeout' => 30),
      'ssl' => array('verify_peer' => false, 'verify_peer_name' => false)));
  $pdf = @file_get_contents($url, false, $context);
  if ($pdf === false || substr($pdf, 0, 4) !== '%PDF') {
    return false;
  }
  return $pdf;
}

// Runs a command (given as an argument array, so no shell is involved).
// Returns array(exit_code, stdout, stderr).
function card_run($argv) {
  $proc = @proc_open($argv, array(1 => array('pipe', 'w'), 2 => array('pipe', 'w')), $pipes);
  if (!is_resource($proc)) {
    return array(-1, '', 'could not start '.$argv[0]);
  }
  $out = stream_get_contents($pipes[1]);
  $err = stream_get_contents($pipes[2]);
  fclose($pipes[1]);
  fclose($pipes[2]);
  return array(proc_close($proc), $out, $err);
}

// Renders each page of a PDF to a PNG.  Returns an array of PNG binary strings,
// or false.  Uses Ghostscript, which CUPS needs anyway.
function card_pdf_to_pngs($pdf, $dpi = 200) {
  $dir = sys_get_temp_dir().'/card-preview-'.bin2hex(random_bytes(6));
  if (!@mkdir($dir, 0700)) {
    return false;
  }
  $pngs = array();
  if (file_put_contents($dir.'/in.pdf', $pdf) !== false) {
    list($code, $out, $err) = card_run(array('gs', '-q', '-dNOPAUSE', '-dBATCH', '-dSAFER',
                                             '-sDEVICE=png16m', '-r'.intval($dpi),
                                             '-sOutputFile='.$dir.'/page-%d.png', $dir.'/in.pdf'));
    if ($code == 0) {
      for ($i = 1; is_file($dir.'/page-'.$i.'.png'); ++$i) {
        $pngs[] = file_get_contents($dir.'/page-'.$i.'.png');
      }
    }
  }
  foreach (glob($dir.'/*') as $f) {
    @unlink($f);
  }
  @rmdir($dir);
  return empty($pngs) ? false : $pngs;
}

// Sends one card to the printer.  Returns array(ok, message, cups_id).
function card_submit($racerid, $sides, $copies) {
  global $db;
  card_print_jobs_ready();

  $pdf = card_pdf($racerid, $sides);
  if ($pdf === false) {
    return array(false, 'Could not generate the card PDF.', '');
  }

  $file = tempnam(sys_get_temp_dir(), 'card-');
  file_put_contents($file, $pdf);

  $argv = array('lp', '-d', card_printer_name(),
                '-t', 'DerbyNet card '.intval($racerid),
                '-n', strval(max(1, intval($copies))));
  foreach (card_print_options($sides) as $opt) {
    $argv[] = '-o';
    $argv[] = $opt;
  }
  $argv[] = $file;
  list($code, $out, $err) = card_run($argv);
  @unlink($file);

  $cups_id = '';
  if ($code == 0 && preg_match('/request id is (\S+)/', $out, $m)) {
    $cups_id = $m[1];
  }
  $ok = $code == 0 && $cups_id !== '';
  $message = $ok ? 'Sent to printer as '.$cups_id : trim($err ? $err : $out);

  $stmt = $db->prepare('INSERT INTO PrintJobs(racerid, submitted, cups_id, sides, status, message)'
                       .' VALUES(:racerid, :submitted, :cups_id, :sides, :status, :message)');
  $stmt->execute(array(':racerid' => intval($racerid), ':submitted' => time(),
                       ':cups_id' => $cups_id, ':sides' => $sides,
                       ':status' => $ok ? 'submitted' : 'failed',
                       ':message' => substr($message, 0, 300)));
  return array($ok, $message, $cups_id);
}

// Returns array(state, text, queued): state is 'idle', 'printing', 'stopped' or
// 'unknown'; text is CUPS's own description; queued is the number of jobs
// waiting or printing.
function card_printer_status() {
  $name = card_printer_name();
  list($code, $out, $err) = card_run(array('lpstat', '-p', $name));
  $text = trim($code == 0 ? $out : ($err ? $err : $out));
  $state = 'unknown';
  if ($code == 0) {
    if (strpos($text, 'is idle') !== false) {
      $state = 'idle';
    } else if (strpos($text, 'now printing') !== false) {
      $state = 'printing';
    } else if (strpos($text, 'disabled') !== false) {
      $state = 'stopped';
    }
  }
  list($qcode, $qout, $qerr) = card_run(array('lpstat', '-o', $name));
  $queued = 0;
  foreach (explode("\n", $qout) as $line) {
    if (strpos($line, $name.'-') === 0) {
      ++$queued;
    }
  }
  return array($state, $text, $queued);
}
?>
