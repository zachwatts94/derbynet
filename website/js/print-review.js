'use strict';

// Review page for license cards: pick a racer, look at the card exactly as it
// will print, fix photos if needed, then send it to the card printer.

var g_racers = [];        // From query=racer.list
var g_printed = {};       // racerid -> {count, last}, from query=print.status
var g_selected = 0;       // racerid currently under review, or 0
var g_sending = false;
var g_printer_ok = false;

function has_photo(url) {
  // Placeholder photos made by the fake-roster generator are not real photos.
  return !!url && !/fake_[0-9]*\.svg/.test(url);
}

function printed_count(racerid) {
  return g_printed[racerid] ? g_printed[racerid].count : 0;
}

function racer_by_id(racerid) {
  for (var i = 0; i < g_racers.length; ++i) {
    if (g_racers[i].racerid == racerid) {
      return g_racers[i];
    }
  }
  return null;
}

function sort_key_compare(a, b) {
  // Oldest check-in first, so cards come out in the order racers arrived.
  var sa = a.seq || Number.MAX_VALUE;
  var sb = b.seq || Number.MAX_VALUE;
  if (sa != sb) {
    return sa < sb ? -1 : 1;
  }
  var na = (a.lastname + ' ' + a.firstname).toLowerCase();
  var nb = (b.lastname + ' ' + b.firstname).toLowerCase();
  return na < nb ? -1 : (na > nb ? 1 : 0);
}

function visible_racers() {
  var mode = $('#show-filter').val();
  var needle = $.trim($('#search').val()).toLowerCase();
  return g_racers.filter(function(r) {
    var n = printed_count(r.racerid);
    if (mode == 'todo' && (!r.passed || n > 0)) {
      return false;
    }
    if (mode == 'printed' && n == 0) {
      return false;
    }
    if (needle) {
      var hay = (r.firstname + ' ' + r.lastname + ' ' + r.carnumber).toLowerCase();
      if (hay.indexOf(needle) < 0) {
        return false;
      }
    }
    return true;
  }).sort(sort_key_compare);
}

function render_list() {
  var racers = visible_racers();
  var ul = $('#racer-list').empty();
  $('#list-empty').toggleClass('hidden', racers.length > 0);

  racers.forEach(function(r) {
    var li = $('<li/>').attr('data-racerid', r.racerid);
    if (r.racerid == g_selected) {
      li.addClass('selected');
    }
    if (has_photo(r.headshot)) {
      li.append($('<img/>').attr('src', r.headshot));
    } else {
      li.append($('<div class="nophoto"/>'));
    }
    var who = $('<div class="who"/>');
    who.append($('<b/>').text(r.firstname + ' ' + r.lastname));
    who.append($('<br/>'));
    var small = $('<small/>').text('#' + r.carnumber + (r.class ? ' · ' + r.class : ''));
    who.append(small);
    if (!has_photo(r.headshot)) {
      who.append($('<span class="badge warn"/>').text('no headshot'));
    }
    if (!has_photo(r.car_photo)) {
      who.append($('<span class="badge warn"/>').text('no car photo'));
    }
    var n = printed_count(r.racerid);
    if (n > 0) {
      who.append($('<span class="badge done"/>').text(n > 1 ? 'printed ×' + n : 'printed'));
    }
    li.append(who);
    li.on('click', function() { select_racer(r.racerid); });
    ul.append(li);
  });
}

function current_sides() {
  return $('input[name=sides]:checked').val() == '1' ? 1 : 2;
}

function redraw_preview() {
  if (!g_selected) {
    return;
  }
  var img = $('#preview');
  $('#preview-wait').removeClass('hidden').text('Drawing the card…');
  img.addClass('hidden');
  $('#preview-labels').addClass('hidden');
  var url = 'print-preview.php?racerid=' + g_selected + '&sides=' + current_sides()
      + '&t=' + Date.now();
  var racerid = g_selected;
  img.off('load error').on('load', function() {
    if (racerid != g_selected) {
      return;
    }
    $('#preview-wait').addClass('hidden');
    img.removeClass('hidden');
    $('#preview-labels').toggleClass('hidden', current_sides() == 1);
  }).on('error', function() {
    if (racerid != g_selected) {
      return;
    }
    $('#preview-wait').removeClass('hidden').text(
        'The card could not be drawn.  Check that you are still logged in, then try "Redraw card".');
  });
  img.attr('src', url);
}

function warnings_for(r) {
  var w = [];
  if (!r.passed) {
    w.push('This racer is not checked in yet.');
  }
  if (!has_photo(r.headshot)) {
    w.push('No headshot yet — the card will show "No photo".');
  }
  if (!has_photo(r.car_photo) && current_sides() == 2) {
    w.push('No car photo yet — the back will show "No car photo".');
  }
  if (!r.carname && current_sides() == 2) {
    w.push('No car name — the back header will be blank.');
  }
  var n = printed_count(r.racerid);
  if (n > 0) {
    w.push('A card has already been sent for this racer (' + n + (n == 1 ? ' time' : ' times')
           + ').  Print again only if the first one was spoiled.');
  }
  return w;
}

function update_review() {
  var r = racer_by_id(g_selected);
  if (!r) {
    return;
  }
  $('#racer-name').text(r.firstname + ' ' + r.lastname);
  $('#racer-detail').text('Car #' + r.carnumber + (r.carname ? ' — ' + r.carname : '')
                          + (r['class'] ? ' · ' + r['class'] : ''));

  var ul = $('#warnings').empty();
  warnings_for(r).forEach(function(text) { ul.append($('<li/>').text(text)); });

  $('#crop-head').attr('href', 'photo-thumbs.php?repo=head&racerid=' + r.racerid);
  $('#crop-car').attr('href', 'photo-thumbs.php?repo=car&racerid=' + r.racerid);
  $('#photo-actions, #print-controls').removeClass('hidden');
  $('#print-button').prop('disabled', g_sending || !g_printer_ok);
}

function select_racer(racerid) {
  g_selected = racerid;
  $('#print-message').text('').removeClass('ok error');
  $('#racer-list li').removeClass('selected').filter(function() {
    return $(this).attr('data-racerid') == racerid;
  }).addClass('selected');
  update_review();
  redraw_preview();
  if (window.history && history.replaceState) {
    history.replaceState(null, '', 'print-review.php?racerid=' + racerid);
  }
}

function load_racers(done) {
  $.ajax({url: 'action.php', dataType: 'json',
          data: {query: 'racer.list', order: 'checkin', render: '200x200'}})
    .done(function(data) {
      g_racers = data.racers || [];
      render_list();
      update_review();
      if (done) {
        done();
      }
    });
}

function load_status() {
  $.ajax({url: 'action.php', dataType: 'json', data: {query: 'print.status'}})
    .done(function(data) {
      var state = $('#printer-state');
      if (!data.printer) {
        g_printer_ok = false;
        state.attr('class', 'state-unknown').text('Not authorized to use the printer.');
        return;
      }
      var p = data.printer;
      var text = p.name + ': ' + p.state;
      if (p.queued > 0) {
        text += ' · ' + p.queued + (p.queued == 1 ? ' card' : ' cards') + ' waiting';
      }
      if (p.state != 'idle' && p.state != 'printing') {
        text += ' — ' + p.text;
      }
      g_printer_ok = p.state == 'idle' || p.state == 'printing';
      state.attr('class', 'state-' + p.state).text(text);

      var before = JSON.stringify(g_printed);
      g_printed = data.printed || {};
      if (JSON.stringify(g_printed) != before) {
        render_list();
        update_review();
      }
      $('#print-button').prop('disabled', g_sending || !g_printer_ok || !g_selected);
    })
    .fail(function() {
      g_printer_ok = false;
      $('#printer-state').attr('class', 'state-unknown').text('Cannot reach the server.');
    });
}

function set_message(text, kind) {
  $('#print-message').text(text).removeClass('ok error').addClass(kind || '');
}

function next_racer_after(racerid) {
  var list = visible_racers();
  for (var i = 0; i < list.length; ++i) {
    if (list[i].racerid == racerid) {
      return list[i + 1] ? list[i + 1].racerid : 0;
    }
  }
  return list.length ? list[0].racerid : 0;
}

function send_card(force) {
  var r = racer_by_id(g_selected);
  if (!r) {
    return;
  }
  g_sending = true;
  $('#print-button').prop('disabled', true);
  set_message('Sending…');
  var racerid = r.racerid;
  $.ajax({url: 'action.php', type: 'POST', dataType: 'json',
          data: {action: 'print.card', racerid: racerid, sides: current_sides(),
                 copies: $('#copies').val(), force: force ? 1 : 0}})
    .done(function(data) {
      g_sending = false;
      if (data.outcome.summary == 'success') {
        set_message('Sent to the printer (' + data.job + ').  The card takes about half a minute.',
                    'ok');
        load_status();
        var next = next_racer_after(racerid);
        // After the status refresh the printed racer drops out of "Needs printing".
        setTimeout(function() {
          if (g_selected == racerid && next) {
            select_racer(next);
            set_message('Last card sent for ' + r.firstname + ' ' + r.lastname + '.', 'ok');
          }
        }, 1500);
      } else if (data.outcome.code == 'already-printed') {
        if (window.confirm('A card was already sent for ' + r.firstname + ' ' + r.lastname
                           + '.  Print another?')) {
          send_card(true);
        } else {
          set_message('Not printed.');
          $('#print-button').prop('disabled', !g_printer_ok);
        }
      } else {
        set_message(data.outcome.description || 'The card could not be sent.', 'error');
        $('#print-button').prop('disabled', !g_printer_ok);
      }
    })
    .fail(function() {
      g_sending = false;
      set_message('Could not reach the server.', 'error');
      $('#print-button').prop('disabled', !g_printer_ok);
    });
}

function on_print_clicked() {
  var r = racer_by_id(g_selected);
  if (!r) {
    return;
  }
  var problems = warnings_for(r).filter(function(w) { return w.indexOf('already been sent') < 0; });
  if (problems.length &&
      !window.confirm(problems.join('\n') + '\n\nPrint this card anyway?')) {
    return;
  }
  send_card(false);
}

$(function() {
  $('#search').on('input', render_list);
  $('#show-filter').on('change', render_list);
  $('input[name=sides]').on('change', function() { update_review(); redraw_preview(); });
  $('#refresh-preview').on('click', function() { load_racers(redraw_preview); });
  $('#print-button').on('click', on_print_clicked);

  // Coming back from fixing a photo in another tab: show the result.
  $(window).on('focus', function() {
    if (g_selected && !g_sending) {
      load_racers(redraw_preview);
    }
  });

  var wanted = /[?&]racerid=([0-9]+)/.exec(window.location.search);
  load_status();
  load_racers(function() {
    if (wanted && racer_by_id(wanted[1])) {
      $('#show-filter').val('all');
      render_list();
      select_racer(parseInt(wanted[1], 10));
    } else {
      var list = visible_racers();
      if (list.length) {
        select_racer(list[0].racerid);
      }
    }
  });
  setInterval(load_status, 4000);
});
