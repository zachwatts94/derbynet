'use strict';

// A drop-in alternative to CircularFrameBuffer for cameras that deliver 60 fps.
//
// CircularFrameBuffer copies each frame into an RGBA ImageData (8 MB at 1080p),
// which costs enough CPU to cap capture below 60 fps and enough memory that a
// long buffer overruns what a browser page is allowed.  This version reads
// VideoFrames straight from the camera track (MediaStreamTrackProcessor) and
// keeps their planar YUV bytes (3 MB at 1080p) in a fixed set of ArrayBuffers
// that are reused in place, so nothing is allocated or garbage-collected while
// racing.
//
// Same interface as CircularFrameBuffer: start_recording, stop_recording,
// playback, set_recording_length, on_resize, width, height.
//
// length_ms is how much video a replay shows.  capacity_ms is how much video is
// kept.  capture_fps sizes the buffer (frames are not dropped to match it).
function WebCodecsFrameBuffer(stream, length_ms, capacity_ms, capture_fps) {
  capture_fps = capture_fps || 60;
  if (!capacity_ms || capacity_ms < length_ms) {
    capacity_ms = length_ms * 2;
  }
  console.log('WCFB: length_ms = ', length_ms, 'capacity_ms = ', capacity_ms,
              'capture_fps = ', capture_fps);

  const this_fb = this;
  const track = stream.getVideoTracks()[0];
  const fps_div = $("#fps");
  const show_stats = /[?&]stats=1/.test(window.location.search);

  let resizing_callback = false;
  this.on_resize = function(cb) { resizing_callback = cb; };
  this.set_recording_length = function(number_of_milliseconds) {
    length_ms = number_of_milliseconds;
  };

  // A little slack beyond the nominal rate.
  const slot_count = Math.ceil(capacity_ms / 1000 * (capture_fps + 2));
  let buffers = new Array(slot_count);      // ArrayBuffer per slot, reused
  let frame_times = new Array(slot_count);  // capture time in ms
  // frame_index is the slot for the NEXT frame; last_recorded_frame_index is the
  // slot of the most recent one, or -1.  valid_count is how many slots hold
  // frames of the current recording.
  let frame_index = 0;
  let last_recorded_frame_index = -1;
  let valid_count = 0;
  let recording = false;

  // Format of the frames in buffers: {format, w, h, size, layout}
  let fmt = null;
  let unsupported_logged = false;

  let stats_window_start = 0;
  let stats_count = 0;

  this.width = function() { return fmt ? fmt.w : 0; };
  this.height = function() { return fmt ? fmt.h : 0; };

  async function store(frame) {
    const format = frame.format;
    if (!format) {
      if (!unsupported_logged) {
        unsupported_logged = true;
        console.log('WCFB: camera frames have no readable format; cannot record');
      }
      return;
    }
    const w = frame.codedWidth;
    const h = frame.codedHeight;
    const size = frame.allocationSize();
    if (!fmt || fmt.w != w || fmt.h != h || fmt.format != format || fmt.size != size) {
      console.log('WCFB: frame format ' + format + ' ' + w + 'x' + h + ', ' + size + ' bytes');
      fmt = {format: format, w: w, h: h, size: size, layout: null};
      buffers = new Array(slot_count);
      frame_index = 0;
      last_recorded_frame_index = -1;
      valid_count = 0;
      if (resizing_callback) {
        resizing_callback(w, h);
      }
    }

    let buf = buffers[frame_index];
    if (!buf) {
      buf = buffers[frame_index] = new ArrayBuffer(size);
    }
    const layout = await frame.copyTo(buf);
    if (!fmt.layout) {
      fmt.layout = layout;
    }
    frame_times[frame_index] = frame.timestamp / 1000;
    last_recorded_frame_index = frame_index;
    frame_index = (frame_index + 1) % slot_count;
    valid_count = Math.min(valid_count + 1, slot_count);

    if (show_stats) {
      ++stats_count;
      const now = performance.now();
      if (now - stats_window_start >= 1000) {
        fps_div.removeClass('hidden').css("background-color", "white")
          .text("wc cap " + Math.round(stats_count * 1000 / (now - stats_window_start)) +
                " fps, " + slot_count + " slots");
        stats_count = 0;
        stats_window_start = now;
      }
    }
  }

  // Reads every frame from the camera track, whether or not recording.
  async function pump() {
    const processor = new MediaStreamTrackProcessor({track: track});
    const reader = processor.readable.getReader();
    for (;;) {
      let result;
      try {
        result = await reader.read();
      } catch (e) {
        console.log('WCFB: read error', e);
        return;
      }
      if (result.done) {
        return;
      }
      const frame = result.value;
      if (typeof g_recorder != 'undefined' && g_recorder && this_fb !== g_recorder) {
        console.log('WCFB: replaced by another recorder; stopping this one');
        frame.close();
        reader.cancel().catch(function() {});
        return;
      }
      try {
        if (recording) {
          await store(frame);
        }
      } catch (e) {
        console.log('WCFB: store error', e);
      } finally {
        frame.close();
      }
    }
  }
  pump();

  this.start_recording = function() {
    // The buffers are kept and reused; only the bookkeeping starts over.
    frame_index = 0;
    last_recorded_frame_index = -1;
    valid_count = 0;
    recording = true;
  };

  this.stop_recording = function() {
    console.log('WCFB stop_recording');
    recording = false;
  };

  // canvas -- a DOM <canvas> element
  // repeat -- number of times to play back the video
  // playback_rate -- percentage multiplier for playback (50 = half speed)
  // on_precanvas -- callback invoked (once) on the offscreen <canvas> element
  //    that renders each frame
  // on_playback_finished -- callback invoked when a playback finishes (may be
  //    called repeat times)
  // on_done -- callback invoked when playback completes
  // end_back_ms and tail_ms (optional): end the replay end_back_ms before the
  //    most recent frame, plus tail_ms.
  this.playback = function(canvas, repeat, playback_rate,
                           on_precanvas, on_playback_finished, on_done,
                           end_back_ms, tail_ms) {
    if (!fmt || last_recorded_frame_index < 0) {
      console.log('WCFB: no captured frames to play back');
      if (on_done) {
        on_done();
      }
      return;
    }

    const context = canvas.getContext('2d');
    const pre_canvas = document.createElement('canvas');
    pre_canvas.width = fmt.w;
    pre_canvas.height = fmt.h;
    const pre_context = pre_canvas.getContext('2d');
    if (on_precanvas) {
      on_precanvas(pre_canvas);
    }

    const scale = Math.min(canvas.width / pre_canvas.width,
                           canvas.height / pre_canvas.height);
    const draw_width = pre_canvas.width * scale;
    const draw_height = pre_canvas.height * scale;
    const draw_x = (canvas.width - draw_width) / 2;
    const draw_y = (canvas.height - draw_height) / 2;

    const last_time = frame_times[last_recorded_frame_index];
    const anchor_time = last_time - (end_back_ms || 0);
    const end_time = (end_back_ms > 0)
        ? Math.min(last_time, anchor_time + (tail_ms || 0))
        : last_time;
    const start_goal = anchor_time - Math.round(length_ms);

    let start_index = last_recorded_frame_index;
    for (let step = 0; step < valid_count; ++step) {
      const i = (last_recorded_frame_index + slot_count - step) % slot_count;
      if (frame_times[i] < start_goal) {
        break;
      }
      start_index = i;
    }
    let end_index = last_recorded_frame_index;
    for (let step = 0; step < valid_count; ++step) {
      const i = (last_recorded_frame_index + slot_count - step) % slot_count;
      if (frame_times[i] <= end_time) {
        end_index = i;
        break;
      }
    }
    console.log('WCFB playback: repeat=' + repeat + ', rate=' + playback_rate +
                ', start_index=' + start_index + ', end_index=' + end_index +
                ', end_back_ms=' + end_back_ms + ', valid=' + valid_count);

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

    function playback_callback(ts) {
      const goal_frame_time = start_goal + (ts - playback_start_time) * playback_rate / 100;
      let found = false;
      for (let tries = 0; tries < valid_count; ++tries) {
        if (frame_times[pindex] >= goal_frame_time) {
          found = frame_times[pindex] <= end_time;
          break;
        }
        if (pindex == end_index) {
          break;
        }
        pindex = (pindex + 1) % slot_count;
      }
      if (found) {
        if (pindex != last_shown_frame) {
          try {
            const vf = new VideoFrame(buffers[pindex], {
              format: fmt.format,
              codedWidth: fmt.w,
              codedHeight: fmt.h,
              timestamp: Math.round(frame_times[pindex] * 1000),
              layout: fmt.layout
            });
            pre_context.drawImage(vf, 0, 0);
            vf.close();
            context.drawImage(pre_canvas, draw_x, draw_y, draw_width, draw_height);
          } catch (e) {
            console.error(e);
          }
          last_shown_frame = pindex;
        }
        requestAnimationFrame(playback_callback);
      } else {
        console.log('WCFB playback done (once)');
        if (on_playback_finished) {
          try {
            on_playback_finished();
          } catch (e) {
            console.error(e);
          }
        }
        ++rpt;
        if (rpt < repeat) {
          start_playback();
        } else {
          console.log('WCFB playback fully complete (' + repeat + ' time(s))');
          if (on_done) {
            on_done();
          }
        }
      }
    }

    start_playback();
  };
}
