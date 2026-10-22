// Общие части страниц «Графики» (онлайн, статистика, история): список рядов с галочками, панель
// инструментов, экспорт. Сам график - SvgChart из chart.js. Подключается после scripts.js.

// Цвета рядов: ряд сохраняет цвет по своему номеру
var GCOL = ['#f2762e', '#e0a21b', '#2f7fd1', '#72b4ee', '#8a6fd1', '#d6598a', '#0f8b8d', '#5c9f3a', '#c0643a', '#16202a', '#2a9d8f', '#b07a1a',
	'#6b7785', '#a259c4', '#e0568a', '#4f9d69', '#3a6fb0', '#c97b1f', '#7d8f3a', '#9b5de5'];
function gColor(i) { return GCOL[i % GCOL.length]; }

// Выбор рядов помним между визитами
function gLoad(key, def) {
	try { var v = localStorage.getItem(key); return v ? JSON.parse(v) : def; } catch(e) { return def; }
}
function gSave(key, v) { try { localStorage.setItem(key, JSON.stringify(v)); } catch(e) {} }

// Список рядов слева. groups: [{t: 'Температуры', items: [{k, name, sub, unit, color}]}]; sel: {k: true}
// onToggle(k, включён). Группы сворачиваются; значения справа обновляет gVal(k, текст).
function gSide(el, groups, sel, onToggle) {
	var h = '';
	groups.forEach(function(g, gi) {
		if(!g.items.length) return;
		var on = g.items.filter(function(i) { return sel[i.k]; }).length;
		h += '<div class="gg' + (on || gi == 0 || !g.t ? ' open' : '') + '">' + (g.t ? '<div class="gh" onclick="this.parentNode.classList.toggle(\'open\')">' + g.t + ' · ' + g.items.length + '<i></i></div>' : '') + '<div class="gb">';
		g.items.forEach(function(i) {
			h += '<div class="cb' + (sel[i.k] ? ' on' : '') + '" data-k="' + i.k + '" style="--c:' + i.color + '"><i></i><span>' + esc(i.name) + (i.sub ? '<small>' + esc(i.sub) + '</small>' : '') + '</span>' +
				(i.unit ? '<em>' + i.unit + '</em>' : '') + '<b id="gv-' + i.k + '"></b></div>';
		});
		h += '</div></div>';
	});
	el.innerHTML = h;
	el.onclick = function(ev) {
		var c = ev.target.closest('.cb');
		if(!c) return;
		var k = c.getAttribute('data-k'), on = !c.classList.contains('on');
		c.classList.toggle('on', on);
		onToggle(k, on);
	};
}
function gVal(k, text) { var e = document.getElementById('gv-' + k); if(e) e.innerHTML = text; }
// «Скрыть все» / «Показать все» для длинных списков
function gAll(el, on, onToggle) {
	[].forEach.call(el.querySelectorAll('.cb'), function(c) {
		if(c.classList.contains('on') != on) { c.classList.toggle('on', on); onToggle(c.getAttribute('data-k'), on, true); }
	});
	onToggle(null, on);
}

// Кнопка «Экспорт»: PNG, SVG, CSV, печать
function gExportMenu() {
	return '<details class="dd"><summary class="chip">Экспорт ▾</summary><div class="ddm"><a data-e="png">Картинка PNG</a><a data-e="svg">Вектор SVG</a><a data-e="csv">Таблица CSV</a><a data-e="print">Печать</a></div></details>';
}
function gExportInit(root, getChart, name) {
	root.addEventListener('click', function(ev) {
		var a = ev.target.closest('[data-e]');
		if(!a) return;
		var c = getChart(), dd = a.closest('details'), e = a.getAttribute('data-e');
		if(dd) dd.open = false;
		if(!c || !c.w) return;
		if(e == 'png') c.exportPng(name);
		else if(e == 'svg') c.exportSvg(name);
		else if(e == 'csv') SvgChart.download(name + '.csv', 'text/csv', '﻿' + c.csv(';'));
		else window.print();
	});
}

// Подпись окна: «11:00 — 14:20 · 06.10.2026» или две даты, если окно длиннее суток
function gWinText(w) {
	var f = SvgChart.fmtTime, d = SvgChart.fmtDate;
	if(!w || !isFinite(w[0]) || !isFinite(w[1])) return '—';		// рядов ещё нет: окно времени не определено
	if(w[1] - w[0] > 24 * 3600000) return d(w[0]) + ' — ' + d(w[1]);
	return f(w[0]) + ' — ' + f(w[1]) + ' · ' + d(w[1]);
}

// ---- файлы истории и статистики с SD-карты ----
// Загрузить файл с контроллера: ok(текст) или fail(код)
function gFetch(url, ok, fail) {
	var r = new XMLHttpRequest();
	r.open('GET', urlcontrol + '/' + url, true);
	r.timeout = 60000;
	r.onload = function() { if(r.status == 200) ok(r.responseText); else fail(r.status); };
	r.onerror = r.ontimeout = function() { fail(0); };
	r.send(null);
}
// Шапка *_head.csv: «Время;<ось><имя>;<ось><имя>...», первая буква - тип оси (T °C, P бар, V В, W кВт, C COP, R %, S шаг, F м³/ч, H Гц, M моточасы)
function gHead(text) {
	var a = text.replace(/[\r\n]+$/, '').split(';');
	return a.slice(1).map(function(s) { return {axis: s.charAt(0), name: s.substr(1).replace(/[\r\n]/g, '')}; });
}
