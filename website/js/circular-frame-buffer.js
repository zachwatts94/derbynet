'use strict';

// length_ms is how much video a replay shows.  capacity_ms (optional, at least
// length_ms) is how much video the buffer holds, so a replay can end well before
// the most recent frame.
function CircularFrameBuffer(stream, length_ms, capacity_ms, capture_fps) {
  console.log('CFB: length_ms = ', length_ms, 'capacity_ms = ', capacity_ms);
  capture_fps = capture_fps || 30;
  // Minimum time between captured frames, a little under the nominal interval.
  const min_frame_interval_ms = 1000 / capture_fps - 1.5;
  let captured_in_window = 0;
  let window_start_ts = 0;
  if (!capacity_ms || capacity_ms < length_ms) {
    capacity_ms = length_ms * 2;
  }
  // We expect a video refresh rate of 60 frames per second
  const k_refresh_fps = 60;
  let debugging = false;

  let this_cfb = this;
  let now_msg = (new Date()).toTimeString().split(" ", 1)[0];

  let resizing_callback = false;
  this.on_resize = function(cb) { resizing_callback = cb; }

  // Takes effect for the next recording, not the current one
  this.set_recording_length = function(number_of_milliseconds) {
    length_ms = number_of_milliseconds; }
  
  let offscreen_video = document.createElement('video');
  // For a remote stream, the width and height may initially not be set, and
  // will require fixing up in the catch clause in recording_playback.
  let stream_settings = stream.getVideoTracks()[0].getSettings();
  console.log('CircularFrameBuffer: stream reports w,h', stream_settings.width, stream_settings.height);
  offscreen_video.width = stream_settings.width;
  offscreen_video.height = stream_settings.height;
  // Without this, iOS produces NotAllowedError at the call to .play()
  offscreen_video.playsInline = true;

  let offscreen_canvas = document.createElement('canvas');
  offscreen_canvas.width = offscreen_video.width;
  offscreen_canvas.height = offscreen_video.height;
  let offscreen_context = offscreen_canvas.getContext('2d', {willReadFrequently: true});

  let frames;
  let frame_times;
  // frame_index is the index into frames[] and frame_times[] for the NEXT frame
  // to capture.
  let frame_index = 0;
  // last_recorded_frame_index, if >= 0, is the index of the most recently captured
  // frame.
  let last_recorded_frame_index = -1;

  // Above 30 fps, only the most recent hi_res_ms of video (enough for a normal
  // replay) is kept at the full capture rate.  Older frames are thinned to every
  // other frame, which keeps the memory use of a long buffer within what a
  // browser page is allowed.  total_seq counts the frames stored since recording
  // started; thin_seq is the next one to consider for thinning.
  let total_seq = 0;
  let thin_seq = 0;
  function thin_old_frames(now_ts) {
    if (capture_fps <= 30) {
      return;
    }
    let horizon = now_ts - (length_ms + 2000);
    let n = frames.length;
    if (thin_seq <= total_seq - n) {
      thin_seq = total_seq - n + 1;  // Older frames have been overwritten.
    }
    while (thin_seq < total_seq - 1 && frame_times[thin_seq % n] < horizon) {
      if (thin_seq % 2 == 1) {
        frames[thin_seq % n] = null;
      }
      ++thin_seq;
    }
  }
  let recording = false;

  offscreen_video.srcObject = stream;

  offscreen_video.play().then(
    function () { console.log('Video play() started'); },
    function(reason) { console.log('Video element play() rejected:', reason); });

  let fps_div = $("#fps");
  let last_ts = 0;
  function report_fps(ts, play_or_rec) {
    if (show_stats) {
      return;
    }
    let delta = ts - last_ts;
    last_ts = ts;
    if (delta != 0) {
      let fps = Math.round(1000 / delta);
      fps_div.css("background-color", fps < 30 ? "red" : (fps < k_refresh_fps ? "yellow" : "white"))
        .toggleClass('hidden', fps >= k_refresh_fps)
        .text(play_or_rec + fps + " fps");
    }
  }

  // With ?stats=1 on the replay page, show the measured capture rate (frames
  // actually stored per second) in the corner.
  let show_stats = /[?&]stats=1/.test(window.location.search);
  function window_stats(elapsed_ms) {
    if (show_stats) {
      fps_div.removeClass('hidden').css("background-color", "white")
        .text("cap " + Math.round(captured_in_window * 1000 / elapsed_ms) + "/" + capture_fps +
              " fps, " + Math.round(frames.length) + " slots");
    }
  }

  // This is the callback function passed to window.requestAnimationFrame on a
  // repaint.
  function recording_callback(ts) {
    // ts is a double giving time in milliseconds
    //
    // offscreen_video.currentTime appears to advance continuously, and doesn't
    // provide information about video frame changes.
    report_fps(ts, "rec ");
    if (!recording) {
      console.log(now_msg + " not recording in recording_callback");
      return;
    }

    // Only capture at 30fps (33.3ms per frame)
    if (last_recorded_frame_index < 0 ||
        ts - frame_times[last_recorded_frame_index] >= min_frame_interval_ms) {
      // Using the offscreen <video> element as an HTMLVideoSource, capture an
      // image of the current frame into the offscreen <canvas> element.
      offscreen_context.drawImage(offscreen_video, 0, 0);

      if (debugging) {
        offscreen_context.font = '20px serif';
        offscreen_context.fillStyle = 'red';
        let fr = '000' + frame_index;
        offscreen_context.fillText('Frame ' + fr.substr(fr.length - 3), 30, 40);
      }

      try {
        let tr = stream.getVideoTracks()[0];
        if (tr) {
          let settings = tr.getSettings();
          let did_resize = false;
          if (settings.width && offscreen_canvas.width != settings.width) {
            console.log("Adjusting width to " + settings.width);
            offscreen_video.width = offscreen_canvas.width = settings.width;
            did_resize = true;
          }

          if (settings.height && offscreen_canvas.height != settings.height) {
            console.log("Adjusting height to " + settings.height);
            offscreen_video.height = offscreen_canvas.height = settings.height;
            did_resize = true;
          }
          if (did_resize) {
            logstream("Stream resized to " + settings.width + 'x' + settings.height);
          }
          if (did_resize && resizing_callback) {
            resizing_callback(offscreen_video.width, offscreen_video.height);
          }
        }

        if (offscreen_canvas.width <= 0 || offscreen_canvas.height <= 0) {
          console.log('Skipping frame ' + frame_index + ' because offscreen canvas is ' +
                      offscreen_canvas.width + 'x' + offscreen_canvas.height);
        } else {
          // With this statement removed completely, iOS Safari stops complaining.
          // With just the getImageData call, also no complaint but a red "low
          // fps" marker.
          //
          // Capture an ImageData object from the <canvas>
          frames[frame_index] =
            offscreen_context.getImageData(0, 0,
                                           offscreen_canvas.width,
                                           offscreen_canvas.height);
          frame_times[frame_index] = ts;
          last_recorded_frame_index = frame_index;
          ++captured_in_window;
          if (ts - window_start_ts >= 1000) {
            window_stats(ts - window_start_ts);
            captured_in_window = 0;
            window_start_ts = ts;
          }
        }
      } catch(error) {
        console.log("CFB " + now_msg + " caught error " + error.message +
                    " at frame_index " + frame_index);
        // For a remote stream, the width and height may not have been known
        // initially, and require fixing up here.
      }

      // frame_index may wrap around to 0
      frame_index = (frame_index + 1) % frames.length;
      ++total_seq;
      thin_old_frames(ts);
    }

    if (recording) {
      if (this_cfb === g_recorder) {
        requestAnimationFrame(recording_callback);
      } else {
        console.log("g_recorder value has changed, so stopping " + now_msg + " recorder.");
        this_cfb.stop_recording();
        offscreen_context = null;
        offscreen_canvas.remove();
        // TODO offscreen_video.remove() ?
      }
    }
  }

  this.start_recording = function() {
    // Leave a little slack beyond the nominal capture rate.
    let slots = Math.ceil(Math.max(capacity_ms, length_ms) / 1000 * (capture_fps + 1));
    frames = null;  // Let the previous recording's frames be reclaimed first.
    frames = Array(slots);
    frame_times = Array(slots);
    frame_index = 0;
    last_recorded_frame_index = -1;
    total_seq = 0;
    thin_seq = 0;
    recording = true;
    requestAnimationFrame(recording_callback);
  }

  this.stop_recording = function() {
    console.log('stop_recording for ' + now_msg);
    recording = false;
  }

  this.width = function() { return offscreen_video.width; }
  this.height = function() { return offscreen_video.height; }

  // canvas -- a DOM <canvas> element
  // repeat -- number of times to play back the video
  // playback_rate -- percentage multiplier for playback (50 = half speed slow-motion)
  // on_precanvas -- callback invoked (once) on the offscreen <canvas> element that
  //    will be used to render each frame
  // on_playback_finished -- callback invoked when a playback finishes
  //    (may be called repeat times)
  // on_done -- callback to be invoked when playback completes.
  // end_back_ms and tail_ms (optional): end the replay end_back_ms before the
  // most recent frame, plus tail_ms.
  this.playback = function(canvas, repeat, playback_rate,
                           on_precanvas, on_playback_finished, on_done,
                           end_back_ms, tail_ms) {
    if (!frames) {
      console.log("No frames for playback! (" + now_msg + ")");
      if (on_done) {
        on_done();
      }
      return;
    }

    let context = canvas.getContext('2d');

    let pre_canvas = document.createElement('canvas');
    pre_canvas.width = offscreen_video.width;
    pre_canvas.height = offscreen_video.height;
    let pre_context = pre_canvas.getContext('2d');
    if (on_precanvas) {
      on_precanvas(pre_canvas);
    }

    let scale = Math.min(canvas.width / pre_canvas.width,
                         canvas.height / pre_canvas.height);
    let draw_width = pre_canvas.width * scale;
    let draw_height = pre_canvas.height * scale;
    let draw_x = (canvas.width - draw_width) / 2;
    let draw_y = (canvas.height - draw_height) / 2;

    if (last_recorded_frame_index < 0) {
      console.log("No captured frames! (" + now_msg + ")");
      if (on_done) {
        on_done();
      }
      return;
    }

    // frame_times[last_recorded_frame_index] is the time of the last captured frame
    // frame_times[last_recorded_frame_index] - length_ms is the time of the
    // first frame for playback
    let last_time = frame_times[last_recorded_frame_index];
    let anchor_time = last_time - (end_back_ms || 0);
    let end_time = (end_back_ms > 0)
        ? Math.min(last_time, anchor_time + (tail_ms || 0))
        : last_time;
    let start_goal = anchor_time - Math.round(length_ms);
    console.log("Playback from " + now_msg + ": repeat=" + repeat +
                ", playback_rate=" + playback_rate);
    console.log("last_recorded_frame_index = " + last_recorded_frame_index);
    console.log("Last frame time = " + frame_times[last_recorded_frame_index]);
    console.log("Start goal = " + start_goal);
    
    let start_index = -1;
    for (let step = 0; step < frames.length; ++step) {
      let pindex = (last_recorded_frame_index + frames.length - step) % frames.length;
      if (frame_times[pindex] < start_goal) {
        break;
      }
      start_index = pindex;
    }

    // Index of the last frame to show.
    let end_index = last_recorded_frame_index;
    for (let step = 0; step < frames.length; ++step) {
      let pindex = (last_recorded_frame_index + frames.length - step) % frames.length;
      if (frame_times[pindex] <= end_time) {
        end_index = pindex;
        break;
      }
    }
    console.log("start_index = " + start_index + ", end_index = " + end_index +
                ", end_back_ms = " + end_back_ms);

    // During playback, pindex runs from start_index to last_recorded_frame_index, circularly,
    // inclusive, but may skip steps.

    let pindex;
    let last_shown_frame;
    let playback_start_time;

    let rpt = 0;

    function start_playback() {
      pindex = start_index;
      last_shown_frame = -1;
      playback_start_time = performance.now();
      requestAnimationFrame(playback_callback);
    }

    // Callback to window.requestAnimationFrame that will draw the next frame
    // being played back.
    function playback_callback(ts) {
      report_fps(ts, "play ");

      let goal_frame_time = start_goal + (ts - playback_start_time) * playback_rate / 100;
      let found = false;
      for (let tries = 0; tries < frames.length; ++tries) {
        if (frame_times[pindex] >= goal_frame_time) {
          found = frame_times[pindex] <= end_time;
          break;
        }
        if (pindex == end_index) {
          break;
        }
        pindex = (pindex + 1) % frames.length;
      }
      if (found) {
        if (pindex == last_shown_frame) {
          //console.log("Skipping currently-displayed frame pindex = " + pindex +
          //            " at time " + frame_times[pindex] +
          //            " for goal " + goal_frame_time);
        } else {
          //console.log("Found pindex = " + pindex + " at time " + frame_times[pindex] +
          //            " for goal " + goal_frame_time);
          try {
            let shown = pindex;
            for (let back = 0; back < 3 && !frames[shown]; ++back) {
              shown = (shown + frames.length - 1) % frames.length;
            }
            pre_context.putImageData(frames[shown], 0, 0);
            context.drawImage(pre_canvas, draw_x, draw_y, draw_width, draw_height);
          } catch(e) {
            console.error(e);
          }

          last_shown_frame = pindex;
        }

        requestAnimationFrame(playback_callback);
      } else {
        console.log("Playback from " + now_msg + " done (once)");

        if (on_playback_finished) {
          try {
            on_playback_finished();
          } catch(e) {
            console.error(e);
          }
        }

        ++rpt;
        if (rpt < repeat) {
          start_playback();
        } else {
          console.log("Playback from " + now_msg + " fully complete (" + repeat + " time(s))");
          if (on_done) {
            on_done();
          }
        }
      }
    }

    start_playback();
  }
}
