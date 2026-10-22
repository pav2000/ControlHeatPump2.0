// Прогноз погоды на главной странице. Запросы к Open-Meteo делает браузер (у контроллера нет TLS), поэтому место, число дней
// и сам ответ хранятся только в этом браузере (localStorage), как плотность и цветовая схема. Подключается на главной (карточка)
// и на «Оборудование → Прочее» (тумблер и выбор места в карточке «Интерфейс»). Данные: Open-Meteo.com, лицензия CC BY 4.0.

var WX = {on: 0, name: '', lat: NaN, lon: NaN, days: 7};
try {
	var wxS = JSON.parse(localStorage.getItem('wx') || '{}');
	WX.on = wxS.on ? 1 : 0;
	WX.name = String(wxS.name || '');
	WX.lat = parseFloat(wxS.lat);
	WX.lon = parseFloat(wxS.lon);
	WX.days = wxS.days == 5 ? 5 : 7;
} catch(e) {}

var WX_DAYS = ['Вс', 'Пн', 'Вт', 'Ср', 'Чт', 'Пт', 'Сб'];
var WX_REFRESH = 2 * 3600 * 1000;			// не чаще, чем раз в 2 часа

function wxSave() {
	try { localStorage.setItem('wx', JSON.stringify({on: WX.on, name: WX.name, lat: WX.lat, lon: WX.lon, days: WX.days})); } catch(e) {}
}
function wxEsc(s) { return esc(s); }
function wxHasPlace() { return isFinite(WX.lat) && isFinite(WX.lon); }
function wxGet(url, ok, fail) {
	var x = new XMLHttpRequest();
	x.open('GET', url);
	x.timeout = 10000;
	x.onload = function() {
		var d = null;
		try { d = JSON.parse(x.responseText); } catch(e) {}
		if(x.status == 200 && d) ok(d); else fail();
	};
	x.onerror = x.ontimeout = fail;
	x.send();
}

// ---- поиск места по названию: ok([{name, sub, lat, lon}]) ----
function wxSearch(text, ok, fail) {
	wxGet('https://geocoding-api.open-meteo.com/v1/search?count=6&language=ru&name=' + encodeURIComponent(text), function(d) {
		ok((d.results || []).map(function(r) {
			return {name: r.name, sub: [r.admin1, r.country].filter(Boolean).join(', '), lat: r.latitude, lon: r.longitude};
		}));
	}, fail);
}

// ---- значки: по группам кодов WMO; цвет задают классы карточки (.s солнце, .c облако, .r осадки) ----
var WX_CLOUD = 'M7 19h10a4 4 0 0 0 .6-7.95A6 6 0 0 0 6.1 12.4 3.8 3.8 0 0 0 7 19z';
var WX_SUN = '<circle cx="12" cy="12" r="4"/><path d="M12 3v2M12 19v2M3 12h2M19 12h2M5.6 5.6L7 7M17 17l1.4 1.4M5.6 18.4L7 17M17 7l1.4-1.4"/>';
function wxIcon(code) {
	var cl = function(dy) { return '<path class="c" transform="translate(0 ' + dy + ')" d="' + WX_CLOUD + '"/>'; }, g;
	if(code <= 1) g = '<g class="s">' + WX_SUN + '</g>';
	else if(code == 2) g = '<g class="s" transform="translate(1 1) scale(.6)">' + WX_SUN + '</g><path class="c cf" d="' + WX_CLOUD + '" transform="translate(2 -1)"/>';
	else if(code == 3) g = cl(0);
	else if(code == 45 || code == 48) g = cl(-3) + '<path class="c" d="M5 20h14M8 23h8"/>';
	else if((code >= 71 && code <= 77) || code == 85 || code == 86) g = cl(-3) + '<path class="r" d="M8 19.5v.1M12 21v.1M16 19.5v.1M10 22.5v.1M14 22.5v.1" stroke-width="2.4"/>';
	else if(code >= 95) g = cl(-4) + '<path class="r" d="M12.5 14l-2.5 4h4l-2.5 4"/>';
	else g = cl(-3) + '<path class="r" d="M8 18l-1 3M12 18l-1 3M16 18l-1 3"/>';		// морось, дождь, ливень
	return '<svg viewBox="0 0 24 24" width="32" height="32" fill="none" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">' + g + '</svg>';
}

// ---- данные: кэш в localStorage, ключ - место и число дней ----
function wxKey() { return WX.lat.toFixed(3) + ',' + WX.lon.toFixed(3) + ',' + WX.days; }
function wxCache() {
	try { var c = JSON.parse(localStorage.getItem('wxc') || 'null'); return c && c.key == wxKey() ? c : null; } catch(e) { return null; }
}
function wxLoad(cb) {
	var c = wxCache();
	if(c && Date.now() - c.t < WX_REFRESH) { cb(c, false); return; }
	wxGet('https://api.open-meteo.com/v1/forecast?daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_sum&timezone=auto&forecast_days=' + WX.days +
			'&latitude=' + WX.lat.toFixed(4) + '&longitude=' + WX.lon.toFixed(4), function(d) {
		var n = {key: wxKey(), t: Date.now(), d: d.daily};
		try { localStorage.setItem('wxc', JSON.stringify(n)); } catch(e) {}
		cb(n, false);
	}, function() { cb(c, true); });
}

// ---- карточка на главной ----
function wxTemp(v) { var r = Math.round(v); return (r > 0 ? '+' : r < 0 ? '−' : '') + Math.abs(r) + '°'; }
function wxRain(v) { return v < 0.05 ? '0 мм' : v.toFixed(1).replace('.', ',') + ' мм'; }
function wxDay(s, i) {
	var p = s.split('-'), d = new Date(+p[0], +p[1] - 1, +p[2]);
	return {w: i ? WX_DAYS[d.getDay()] : 'Сегодня', n: i ? String(d.getDate()) : ''};
}
function wxHead(right) {
	return '<h3><span>Погода · ' + wxEsc(WX.name || 'место не выбрано') + '</span><span class="r" style="color:var(--mute);font-weight:600">' + right + '</span></h3>';
}
function wxRender(el) {
	if(!el) return;
	if(!WX.on) { el.hidden = true; return; }
	el.hidden = false;
	if(!wxHasPlace()) {
		el.innerHTML = wxHead('') + '<div class="ev-empty">Выберите местность: <a class="lnk" href="relay.html">Оборудование → Прочее → Интерфейс</a></div>';
		return;
	}
	wxLoad(function(c, offline) {
		if(!c || !c.d) { el.innerHTML = wxHead('') + '<div class="ev-empty">Нет связи с сервером прогноза. Нужен интернет в браузере.</div>'; return; }
		var d = c.d, t = new Date(c.t), hm = ('0' + t.getHours()).slice(-2) + ':' + ('0' + t.getMinutes()).slice(-2), h = '';
		for(var i = 0; i < d.time.length; i++) {
			var w = wxDay(d.time[i], i);
			h += '<div class="wxd' + (i ? '' : ' today') + '"><b>' + w.w + (w.n ? ' <i>' + w.n + '</i>' : '') + '</b>' + wxIcon(d.weather_code[i]) + '<span class="mx">' + wxTemp(d.temperature_2m_max[i]) +
				'</span><span class="mn">' + wxTemp(d.temperature_2m_min[i]) + '</span><span class="pr">' + wxRain(d.precipitation_sum[i]) + '</span></div>';
		}
		el.innerHTML = wxHead((offline ? 'нет связи, данные от ' : 'обновлено ') + hm + ' · <a href="https://open-meteo.com/" target="_blank" rel="noopener" style="color:inherit">Open-Meteo</a>') +
			'<div class="wxbody"><div class="wxdays" style="--n:' + d.time.length + '">' + h + '</div><div class="wxchart"></div></div>';
		wxDraw(el, d);
	});
}

// ---- график температур: максимум (днём) и минимум (ночью) по дням; на телефоне скрыт (CSS). Рисуется в реальных пикселях ширины блока,
// чтобы шрифт не растягивался вместе с окном ----
var wxLast = null, wxTimer = null;
function wxDraw(el, d) {
	wxLast = [el, d];
	var c = el.querySelector('.wxchart');
	if(!c) return;
	c.innerHTML = '';		// ширину меряем у пустого блока: svg фиксированной ширины иначе не давал колонке ни сузиться, ни расшириться
	if(c.clientWidth > 0) c.innerHTML = wxChart(d, c.clientWidth);
}
window.addEventListener('resize', function() {
	if(!wxLast) return;
	clearTimeout(wxTimer);
	wxTimer = setTimeout(function() { if(wxLast[0].clientWidth) wxDraw(wxLast[0], wxLast[1]); }, 150);
});
function wxChart(d, W) {
	var n = d.time.length, H = 92, pl = 4, pr = 4, top = 20, bot = 58;
	var hi = Math.max.apply(null, d.temperature_2m_max), lo = Math.min.apply(null, d.temperature_2m_min);
	if(hi - lo < 4) { hi += 2; lo -= 2; }
	var X = function(i) { return pl + (i + .5) * (W - pl - pr) / n; }, Y = function(v) { return top + (hi - v) * (bot - top) / (hi - lo); };
	var line = function(a, cls) { return '<polyline class="' + cls + '" points="' + a.map(function(v, i) { return X(i).toFixed(1) + ',' + Y(v).toFixed(1); }).join(' ') + '"/>'; };
	var h = '<svg viewBox="0 0 ' + W + ' ' + H + '" width="' + W + '" height="' + H + '" role="img" aria-label="Температура днём и ночью">';
	if(lo < 0 && hi > 0) h += '<rect class="neg" x="' + pl + '" y="' + Y(0).toFixed(1) + '" width="' + (W - pl - pr) + '" height="' + (bot - Y(0) + 8).toFixed(1) + '"/><line class="zero" x1="' + pl + '" x2="' + (W - pr) + '" y1="' + Y(0).toFixed(1) + '" y2="' + Y(0).toFixed(1) + '"/>';
	h += line(d.temperature_2m_max, 'lh') + line(d.temperature_2m_min, 'll');
	for(var i = 0; i < n; i++) {
		var a = d.temperature_2m_max[i], b = d.temperature_2m_min[i], x = X(i).toFixed(1);
		h += '<circle class="dh" cx="' + x + '" cy="' + Y(a).toFixed(1) + '" r="2.5"/><circle class="dl" cx="' + x + '" cy="' + Y(b).toFixed(1) + '" r="2.5"/>' +
			'<text class="th" x="' + x + '" y="' + (Y(a) - 6).toFixed(1) + '">' + wxTemp(a) + '</text><text class="tl" x="' + x + '" y="' + (Y(b) + 12).toFixed(1) + '">' + wxTemp(b) + '</text>' +
			'<text class="td" x="' + x + '" y="' + (H - 3) + '">' + wxDay(d.time[i], i).w + '</text>';
	}
	return h + '</svg>';
}
