Pebble.addEventListener('ready', function() {
  console.log('PebbleKit JS ready');
});

Pebble.addEventListener('showConfiguration', function() {
  var now = new Date();
  var pad = function(n) { return n < 10 ? '0' + n : '' + n; };
  var defaultVal = now.getFullYear() + '-' +
                   pad(now.getMonth() + 1) + '-' +
                   pad(now.getDate()) + 'T' +
                   pad(now.getHours()) + ':' +
                   pad(now.getMinutes());

  var html = '<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"></head>' +
    '<body style="background:#f5f5f5;font-family:sans-serif;padding:24px;max-width:400px;margin:0 auto">' +
    '<h2 style="margin-top:0">Log Missed Cigarette</h2>' +
    '<label style="display:block;margin-bottom:16px">Date &amp; Time:<br>' +
    '<input type="datetime-local" id="dt" value="' + defaultVal + '" ' +
    'style="font-size:16px;margin-top:6px;width:100%;box-sizing:border-box"></label>' +
    '<button onclick="doSubmit()" ' +
    'style="font-size:16px;padding:10px 24px;margin-right:12px;background:#f5a623;border:none;border-radius:4px;cursor:pointer">Log</button>' +
    '<button onclick="doCancel()" ' +
    'style="font-size:16px;padding:10px 24px;background:#ccc;border:none;border-radius:4px;cursor:pointer">Cancel</button>' +
    '<script>' +
    'function doSubmit(){' +
    '  var v=document.getElementById("dt").value;' +
    '  if(!v){alert("Pick a date and time first.");return;}' +
    '  var ts=Math.floor(new Date(v).getTime()/1000);' +
    '  location.href="pebblejs://close#"+encodeURIComponent(JSON.stringify({timestamp:ts}));' +
    '}' +
    'function doCancel(){location.href="pebblejs://close#";}' +
    '<\/script></body></html>';

  Pebble.openURL('data:text/html,' + encodeURIComponent(html));
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (!e.response || e.response.length === 0) return;
  var payload;
  try { payload = JSON.parse(decodeURIComponent(e.response)); } catch(ex) { return; }
  if (!payload || typeof payload.timestamp !== 'number') return;
  var ts = Math.floor(payload.timestamp);
  Pebble.sendAppMessage(
    { 'RETROACTIVE_TIMESTAMP': ts },
    function() { console.log('Retro timestamp sent: ' + ts); },
    function(msg, err) { console.log('sendAppMessage failed: ' + err); }
  );
});
