<?php
// Review and release license cards to the card printer.
//
// Shows each checked-in racer's card exactly as it will print, so cropping and
// missing photos can be fixed before any card is made, then sends it to the
// server's card printer (see inc/card-printing.inc).

session_start();
require_once('inc/data.inc');
require_once('inc/authorize.inc');
session_write_close();
require_once('inc/banner.inc');

require_permission(ASSIGN_RACER_IMAGE_PERMISSION);

?><!DOCTYPE html>
<html>
<head>
<meta http-equiv="Content-Type" content="text/html; charset=UTF-8"/>
<title>Print Licenses</title>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<?php require('inc/stylesheet.inc'); ?>
<link rel="stylesheet" type="text/css" href="css/print-review.css?v=<?php echo @filemtime('css/print-review.css') ?: time(); ?>"/>
<script type="text/javascript" src="js/jquery.js"></script>
<script type="text/javascript" src="js/ajax-setup.js"></script>
<script type="text/javascript" src="js/print-review.js?v=<?php echo @filemtime('js/print-review.js') ?: time(); ?>"></script>
</head>
<body>
<?php make_banner('Print Licenses', 'checkin.php'); ?>

<div id="printer-bar"><span id="printer-state" class="state-unknown">Checking printer&hellip;</span></div>

<div id="layout">
  <div id="list-pane">
    <input type="text" id="search" placeholder="Search name or car number" autocomplete="off"/>
    <select id="show-filter">
      <option value="todo" selected="selected">Needs printing</option>
      <option value="printed">Already printed</option>
      <option value="all">Everyone</option>
    </select>
    <ul id="racer-list"></ul>
    <p id="list-empty" class="hidden">No racers to show.</p>
  </div>

  <div id="review-pane">
    <h2 id="racer-name">Choose a racer</h2>
    <p id="racer-detail"></p>

    <div id="preview-box">
      <img id="preview" alt="Card preview" class="hidden"/>
      <div id="preview-wait" class="hidden">Drawing the card&hellip;</div>
    </div>
    <div id="preview-labels" class="hidden"><span>Front</span><span>Back</span></div>

    <ul id="warnings"></ul>

    <div id="photo-actions" class="hidden">
      <a id="crop-head" class="button" target="_blank" rel="noopener">Fix headshot</a>
      <a id="crop-car" class="button" target="_blank" rel="noopener">Fix car photo</a>
      <button type="button" id="refresh-preview">Redraw card</button>
    </div>

    <div id="print-controls" class="hidden">
      <label><input type="radio" name="sides" value="2" checked="checked"/> Front and back</label>
      <label><input type="radio" name="sides" value="1"/> Front only</label>
      <label>Copies <input type="number" id="copies" min="1" max="5" value="1"/></label>
      <button type="button" id="print-button">Print card</button>
    </div>
    <p id="print-message"></p>
  </div>
</div>
</body>
</html>
