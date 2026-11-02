// Pixel Dungeon Face, phone side: the Clay settings page, and the weather (Open-Meteo, no key needed).
var Clay = require('./clay');
var clay = new Clay(require('./config'), null, { autoHandleEvents: false });
var ORDER = ['CLK_ON', 'CLK_FMT', 'CLK_SIZE', 'CLK_LINE', 'UI_CLOCK', 'DATE_ORD', 'UNIT_F', 'SL_DEPTH', 'SL_KEYS', 'SL_LVL', 'SL_HP', 'SL_GOLD'];   // = FaceCfg in face.inc
var DEF = { CLK_ON: 1, CLK_LINE: 1, UNIT_F: 1 };
function stored() { try { return JSON.parse(localStorage.getItem('clay-settings')) || {}; } catch (e) { return {}; } }
function num(v, key) {
  if (v && typeof v === 'object') v = v.value;
  if (v === undefined || v === null) return DEF[key] || 0;
  if (v === true) return 1; if (v === false) return 0;
  return parseInt(v, 10) || 0;
}
function dmyPhone() {                                   // does the phone write the day before the month?
  try { var s = new Date(2001, 10, 25).toLocaleDateString(); var a = s.indexOf('25'), b = s.indexOf('11'); if (a >= 0 && b >= 0) return a < b; } catch (e) {}
  return false;
}
function pack(s) {
  var out = [];
  for (var i = 0; i < ORDER.length; i++) out.push(num(s[ORDER[i]], ORDER[i]));
  if (out[5] === 2) out[5] = dmyPhone() ? 1 : 0;
  return out;
}
function send(msg, tries) {
  Pebble.sendAppMessage(msg, function () {}, function () { if (tries > 0) setTimeout(function () { send(msg, tries - 1); }, 4000); });
}
function needWeather() {
  var s = stored(), w = { '3': 1, '4': 1, '5': 1, '15': 1 };
  for (var i = 7; i < 12; i++) if (w[String(num(s[ORDER[i]], ORDER[i]))]) return true;
  return false;
}
function tenths(x) { return Math.round(x * 10); }
function weather() {
  if (!needWeather()) return;
  navigator.geolocation.getCurrentPosition(function (p) {
    var url = 'https://api.open-meteo.com/v1/forecast?latitude=' + p.coords.latitude.toFixed(3) + '&longitude=' + p.coords.longitude.toFixed(3) +
      '&current=temperature_2m&daily=temperature_2m_max,temperature_2m_min&forecast_days=1&timezone=auto';
    var x = new XMLHttpRequest();
    x.onload = function () {
      try {
        var j = JSON.parse(x.responseText), v = [tenths(j.current.temperature_2m), tenths(j.daily.temperature_2m_max[0]), tenths(j.daily.temperature_2m_min[0])];
        var b = []; for (var i = 0; i < 3; i++) { var u = v[i] & 0xFFFF; b.push(u & 255, u >> 8); }   // three little-endian int16
        send({ WX: b }, 2);
      } catch (e) { setTimeout(weather, 300000); }
    };
    x.onerror = function () { setTimeout(weather, 300000); };
    x.open('GET', url); x.send();
  }, function () { setTimeout(weather, 300000); }, { timeout: 20000, maximumAge: 900000 });
}
Pebble.addEventListener('ready', function () { send({ CFG: pack(stored()) }, 2); weather(); setInterval(weather, 45 * 60 * 1000); });
Pebble.addEventListener('showConfiguration', function () { Pebble.openURL(clay.generateUrl()); });
Pebble.addEventListener('webviewclosed', function (e) {
  if (!e || !e.response) return;
  var raw = clay.getSettings(e.response, false);          // (this also remembers them for the next time the page opens)
  send({ CFG: pack(raw) }, 2);
  weather();
});
