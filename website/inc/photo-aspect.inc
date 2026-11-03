<?php
// The shapes (width / height) that racer photos are cropped to.  The manual
// crop screen, auto-crop, and the license card all use these, so a cropped
// photo fills its place on the card with nothing cut off and nothing stretched.

// Headshots: portrait, 3 wide by 4 tall (the photo box on the front of the card).
define('HEADSHOT_ASPECT', 3 / 4);

// Car photos: landscape, 8 wide by 5 tall (the car frame on the back of the card).
define('CAR_PHOTO_ASPECT', 8 / 5);

// 'head' (racer photos) or 'car'
function crop_aspect_for_repo($repo_name) {
  return $repo_name == 'car' ? CAR_PHOTO_ASPECT : HEADSHOT_ASPECT;
}
?>
