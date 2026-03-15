Pebble.addEventListener('ready', function() {
  console.log('PebbleKit JS ready');
});

var exportData = null;

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
    'style="font-size:16px;padding:10px 24px;margin-right:12px;background:#ccc;border:none;border-radius:4px;cursor:pointer">Cancel</button>' +
    '<button onclick="doExport()" ' +
    'style="font-size:16px;padding:10px 24px;background:#5a9fd4;color:#fff;border:none;border-radius:4px;cursor:pointer">Export Data</button>' +
    '<script>' +
    'function doSubmit(){' +
    '  var v=document.getElementById("dt").value;' +
    '  if(!v){alert("Pick a date and time first.");return;}' +
    '  var ts=Math.floor(new Date(v).getTime()/1000);' +
    '  location.href="pebblejs://close#"+encodeURIComponent(JSON.stringify({action:"log",timestamp:ts}));' +
    '}' +
    'function doCancel(){location.href="pebblejs://close#"+encodeURIComponent(JSON.stringify({action:"cancel"}));}' +
    'function doExport(){location.href="pebblejs://close#"+encodeURIComponent(JSON.stringify({action:"export"}));}' +
    '<\/script></body></html>';

  Pebble.openURL('data:text/html,' + encodeURIComponent(html));
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (!e.response || e.response.length === 0) return;
  var payload;
  try { payload = JSON.parse(decodeURIComponent(e.response)); } catch(ex) { return; }
  if (!payload) return;

  if (payload.action === 'log' && typeof payload.timestamp === 'number') {
    var ts = Math.floor(payload.timestamp);
    Pebble.sendAppMessage(
      { 'RETROACTIVE_TIMESTAMP': ts },
      function() { console.log('Retro timestamp sent: ' + ts); },
      function(msg, err) { console.log('sendAppMessage failed: ' + err); }
    );
  } else if (payload.action === 'export') {
    exportData = { summary: null, days: [], weeks: [], hours: null };
    Pebble.sendAppMessage(
      { 'EXPORT_REQUEST': 1 },
      function() { console.log('Export request sent'); },
      function(msg, err) { console.log('Export request failed: ' + err); }
    );
  }
});

Pebble.addEventListener('appmessage', function(e) {
  var d = e.payload;
  if (d['EXPORT_TYPE'] === undefined || exportData === null) return;
  var t = d['EXPORT_TYPE'];
  if (t === 0) {
    exportData.summary = {
      total: d['EXPORT_A'],
      totalDays: d['EXPORT_B'],
      goal: d['EXPORT_C']
    };
  } else if (t === 1) {
    exportData.days[d['EXPORT_IDX']] = {
      ts: d['EXPORT_A'],
      count: d['EXPORT_B']
    };
  } else if (t === 2) {
    exportData.weeks[d['EXPORT_IDX']] = {
      ts: d['EXPORT_A'],
      total: d['EXPORT_B'],
      active: d['EXPORT_C']
    };
  } else if (t === 3) {
    exportData.hours = d['EXPORT_HOURS'];
  } else if (t === 4) {
    showExportPage();
  }
});

function fmtDate(ts) {
  return new Date(ts * 1000).toLocaleDateString();
}

function showExportPage() {
  var d = exportData;
  var avgPerDay = (d.summary && d.summary.totalDays > 0)
    ? (d.summary.total / d.summary.totalDays).toFixed(1) : '-';
  var goalStr = (d.summary && d.summary.goal > 0) ? d.summary.goal : 'off';

  var rows = '';
  rows += '<tr><td>All-time total</td><td>' + (d.summary ? d.summary.total : '-') + '</td></tr>';
  rows += '<tr><td>Days tracked</td><td>' + (d.summary ? d.summary.totalDays : '-') + '</td></tr>';
  rows += '<tr><td>Avg / day</td><td>' + avgPerDay + '</td></tr>';
  rows += '<tr><td>Daily goal</td><td>' + goalStr + '</td></tr>';

  var dayRows = '';
  for (var i = 0; i < d.days.length; i++) {
    if (!d.days[i]) continue;
    dayRows += '<tr><td>' + fmtDate(d.days[i].ts) + '</td><td>' + d.days[i].count + '</td></tr>';
  }

  var weekRows = '';
  var csvWeeks = 'Week Start,Total,Days Active,Avg/Day\n';
  for (var i = 0; i < d.weeks.length; i++) {
    if (!d.weeks[i]) continue;
    var wAvg = d.weeks[i].active > 0
      ? (d.weeks[i].total / d.weeks[i].active).toFixed(1) : '-';
    weekRows += '<tr><td>' + fmtDate(d.weeks[i].ts) + '</td><td>' + d.weeks[i].total +
      '</td><td>' + d.weeks[i].active + '</td><td>' + wAvg + '</td></tr>';
    csvWeeks += fmtDate(d.weeks[i].ts) + ',' + d.weeks[i].total + ',' +
      d.weeks[i].active + ',' + wAvg + '\n';
  }

  var hourRows = '';
  var csvHours = 'Hour,Count\n';
  if (d.hours) {
    for (var h = 0; h < 24; h++) {
      var cnt = Array.isArray(d.hours) ? d.hours[h] : 0;
      hourRows += '<tr><td>' + h + ':00</td><td>' + cnt + '</td></tr>';
      csvHours += h + ',' + cnt + '\n';
    }
  }

  var csvDays = 'Date,Count\n';
  for (var i = 0; i < d.days.length; i++) {
    if (!d.days[i]) continue;
    csvDays += fmtDate(d.days[i].ts) + ',' + d.days[i].count + '\n';
  }

  var fullCsv = '=== SUMMARY ===\nTotal,' + (d.summary ? d.summary.total : '') +
    '\nDays Tracked,' + (d.summary ? d.summary.totalDays : '') +
    '\nAvg/Day,' + avgPerDay +
    '\nGoal,' + goalStr + '\n\n' +
    '=== THIS WEEK ===\n' + csvDays + '\n' +
    '=== PAST WEEKS ===\n' + csvWeeks + '\n' +
    '=== HOURLY ===\n' + csvHours;

  var html = '<!DOCTYPE html><html><head>' +
    '<meta name="viewport" content="width=device-width,initial-scale=1">' +
    '<style>body{font-family:sans-serif;padding:16px;max-width:500px;margin:0 auto;background:#f5f5f5}' +
    'h2{margin-top:0}h3{margin-bottom:4px}' +
    'table{border-collapse:collapse;width:100%;margin-bottom:16px}' +
    'th,td{border:1px solid #ccc;padding:6px 10px;text-align:left}' +
    'th{background:#eee}' +
    'button{font-size:14px;padding:8px 18px;background:#5a9fd4;color:#fff;border:none;border-radius:4px;cursor:pointer;margin-bottom:16px}' +
    '</style></head><body>' +
    '<h2>Zigarettentracker Export</h2>' +
    '<button onclick="copyCSV()">Copy CSV</button>' +
    '<h3>Summary</h3><table><tr><th>Key</th><th>Value</th></tr>' + rows + '</table>' +
    '<h3>This Week</h3><table><tr><th>Date</th><th>Count</th></tr>' + dayRows + '</table>' +
    '<h3>Past Weeks</h3><table><tr><th>Week Start</th><th>Total</th><th>Days</th><th>Avg/Day</th></tr>' + weekRows + '</table>' +
    '<h3>By Hour (this week)</h3><table><tr><th>Hour</th><th>Count</th></tr>' + hourRows + '</table>' +
    '<textarea id="csv" style="display:none">' + fullCsv.replace(/</g,'&lt;') + '</textarea>' +
    '<script>function copyCSV(){' +
    'var t=document.getElementById("csv");' +
    't.style.display="block";t.select();' +
    'try{document.execCommand("copy");alert("CSV copied!");}catch(e){alert("Select the text below and copy manually.");}' +
    '}<\/script></body></html>';

  Pebble.openURL('data:text/html,' + encodeURIComponent(html));
  exportData = null;
}
