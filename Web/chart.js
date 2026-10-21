// Лёгкий SVG-график для морды: линии, ступеньки и столбцы, несколько дорожек (lanes) с отдельным
// автомасштабом по вертикали, шкала времени, окно просмотра (протяжка по графику), навигатор,
// курсор с подсказкой, экспорт (SVG, PNG, CSV, печать). Заменяет Highstock (265 КБ) и jQuery.
//
// Данные онлайн-графиков: get_Chart(N)=<тип>;точка;точка;... - кольцо до 400 точек (старые -> новые),
// шаг в секундах - get_oHP(TIME_CHART). Времени в ответе нет: время точки считается от последней (end)
// назад с шагом step. В режиме CWCO (график только при работе компрессора) шкала приблизительная.
// Данные истории и статистики приходят с метками времени (x в миллисекундах).
//
//   var c = new SvgChart(el, {lanes: [{title: 'Температуры', unit: '°C', h: 190, series: [
//       {name: 'Подача', color: '#f2762e', y: [...], x: [...], area: true}]}], nav: true});
//   ряд: type 'line' (по умолчанию) | 'step' | 'bar'; без x - равномерные точки (step, end), поле data = y
//   c.draw();   // после смены данных, окна или размера
//   c.o.range = 180;  c.draw();   // окно "последние 180 минут" (0 - все данные)

function SvgChart(el, opt) {
	this.el = el;
	this.o = opt || {};
	this.win = null;			// окно, выбранное протяжкой: [t0, t1] в миллисекундах
	var self = this;
	if(window.addEventListener) window.addEventListener('resize', function() { if(el.clientWidth && el.clientWidth != self.w) self.draw(); });
}

// "Красивый" шаг сетки для диапазона [lo, hi] примерно на n делений
SvgChart.niceStep = function(lo, hi, n) {
	var raw = (hi - lo) / n, p = Math.pow(10, Math.floor(Math.log(raw) / Math.LN10)), f = raw / p;
	return (f <= 1 ? 1 : f <= 2 ? 2 : f <= 5 ? 5 : 10) * p;
};
SvgChart.pad2 = function(n) { return n < 10 ? '0' + n : '' + n; };
SvgChart.fmtTime = function(t, full) {
	var d = new Date(t), p = SvgChart.pad2;
	return (full ? p(d.getDate()) + '.' + p(d.getMonth() + 1) + ' ' : '') + p(d.getHours()) + ':' + p(d.getMinutes()) + (full === 2 ? ':' + p(d.getSeconds()) : '');
};
SvgChart.fmtDate = function(t) { var d = new Date(t), p = SvgChart.pad2; return p(d.getDate()) + '.' + p(d.getMonth() + 1) + '.' + d.getFullYear(); };

// первый индекс, где x[i] >= t
SvgChart.lowerBound = function(x, t) {
	var lo = 0, hi = x.length;
	while(lo < hi) { var m = (lo + hi) >> 1; if(x[m] < t) lo = m + 1; else hi = m; }
	return lo;
};

// Привести ряды к виду x[] + y[]; найти границы данных по времени
SvgChart.prototype.prep = function() {
	var o = this.o, step = (o.step || 30) * 1000, end = o.end || Date.now(), d0 = Infinity, d1 = -Infinity;
	o.lanes.forEach(function(l) {
		l.series.forEach(function(s) {
			var y = s.y || s.data || [], n = y.length, x = s.x;
			if(!x) { x = new Array(n); for(var i = 0; i < n; i++) x[i] = end - (n - 1 - i) * step; }
			s._x = x; s._y = y;
			if(n && x[0] < d0) d0 = x[0];
			if(n && x[n - 1] > d1) d1 = x[n - 1];
		});
	});
	this.d0 = d0; this.d1 = d1;
};

// окно просмотра: протяжка > диапазон "последние N минут" > все данные
SvgChart.prototype.window = function() {
	if(this.win) return this.win;
	var r = this.o.range;
	return r ? [Math.max(this.d0, this.d1 - r * 60000), this.d1] : [this.d0, this.d1];
};
SvgChart.prototype.setWindow = function(a, b) {
	this.win = a == null ? null : [a, b];
	this.draw();
	if(this.o.onWindow) this.o.onWindow(this.window(), this.win == null);
};

SvgChart.prototype.points = function() {
	var n = 0;
	this.o.lanes.forEach(function(l) { l.series.forEach(function(s) { var k = (s.y || s.data || []).length; if(k > n) n = k; }); });
	return n;
};

// Метки времени: интервал подбирается под ширину окна; для суток и дольше метки выравниваются по местной полночи
SvgChart.ticks = function(t0, t1) {
	var span = (t1 - t0) / 60000, out = [], iv = [1, 2, 5, 10, 15, 30, 60, 120, 180, 360, 720], i, t, d;
	if(span <= 12 * 60 * 6) {
		var ivl = 720;
		for(i = 0; i < iv.length; i++) if(iv[i] >= span / 7) { ivl = iv[i]; break; }
		var tz = new Date(t0).getTimezoneOffset() * 60000;
		for(t = Math.ceil((t0 - tz) / (ivl * 60000)) * ivl * 60000 + tz; t <= t1; t += ivl * 60000) out.push(t);
		return {t: out, day: false};
	}
	var days = span / 1440, dv = days <= 10 ? 1 : days <= 21 ? 2 : days <= 70 ? 7 : days <= 200 ? 14 : 0;
	d = new Date(t0); d.setHours(0, 0, 0, 0);
	if(dv) {
		if(d.getTime() < t0) d.setDate(d.getDate() + 1);
		for(; d.getTime() <= t1; d.setDate(d.getDate() + dv)) out.push(d.getTime());
	} else {			// по месяцам
		d.setDate(1);
		if(d.getTime() < t0) d.setMonth(d.getMonth() + 1);
		for(; d.getTime() <= t1; d.setMonth(d.getMonth() + 1)) out.push(d.getTime());
	}
	return {t: out, day: true};
};

SvgChart.prototype.draw = function() {
	var o = this.o, el = this.el, W = el.clientWidth || 600, self = this;
	var pad = o.pad || {l: 44, r: 8, t: 20, b: 20}, gap = o.gap || 30;
	this.prep();
	this.w = W;
	if(!this.cid) this.cid = 'cp' + (SvgChart.n = (SvgChart.n || 0) + 1);
	var CP = ' clip-path="url(#' + this.cid + ')"';       // линии рисуются от точки за левым краем окна до точки за правым: за поле графика они выходить не должны
	if(!(this.d1 > this.d0) || this.points() < 2) { el.innerHTML = '<div class="cht-empty">Нет данных</div>'; return; }
	var win = this.window(), t0 = win[0], t1 = win[1] > win[0] ? win[1] : win[0] + 1;
	var x0 = pad.l, x1 = W - pad.r, pw = x1 - x0, h = '', defs = '', y = pad.t;
	this.x0 = x0; this.x1 = x1; this.t0 = t0; this.t1 = t1; this.laneBox = [];
	var X = function(t) { return x0 + pw * (t - t0) / (t1 - t0); };
	o.lanes.forEach(function(lane, li) {
		var lo = Infinity, hi = -Infinity, hh = lane.h || 120, bars = 0, bi = 0;
		if(lane.title && !o.noAxis) { h += '<text x="' + x0 + '" y="' + (y + 4) + '" class="lt">' + lane.title + '</text>'; y += 14; }
		lane.series.forEach(function(s) {
			if(s.hidden) return;
			if(s.type == 'bar') bars++;
			var a = Math.max(0, SvgChart.lowerBound(s._x, t0) - 1), b = Math.min(s._y.length, SvgChart.lowerBound(s._x, t1) + 1);
			for(var i = a; i < b; i++) { var v = s._y[i]; if(v != null) { if(v < lo) lo = v; if(v > hi) hi = v; } }
			if(s.type == 'bar') { if(lo > 0) lo = 0; if(hi < 0) hi = 0; }
		});
		if(lo === Infinity) { lo = 0; hi = 1; }
		if(lane.ref && isFinite(lane.ref.v)) { if(lane.ref.v < lo) lo = lane.ref.v; if(lane.ref.v > hi) hi = lane.ref.v; }
		var span = lane.minSpan || 1;
		if(hi - lo < span) { var m = (hi + lo) / 2; lo = m - span / 2; hi = m + span / 2; }
		var st = SvgChart.niceStep(lo, hi, 4);
		lo = Math.floor(lo / st + 1e-9) * st;
		hi = Math.ceil(hi / st - 1e-9) * st;
		if(lane.min != null) lo = lane.min;
		if(lane.max != null) hi = lane.max;
		var Y = function(v) { return y + hh - hh * (v - lo) / (hi - lo); };
		if(lane.unit && !o.noAxis) h += '<text x="' + (x0 - 6) + '" y="' + (y - 12) + '" text-anchor="end" class="u">' + lane.unit + '</text>';
		for(var t = Math.ceil(lo / st - 1e-9) * st; !o.noAxis && t <= hi + st / 1000; t += st) {
			h += '<line x1="' + x0 + '" x2="' + x1 + '" y1="' + Y(t).toFixed(1) + '" y2="' + Y(t).toFixed(1) + '" class="g"/>' +
				'<text x="' + (x0 - 6) + '" y="' + (Y(t) + 4).toFixed(1) + '" text-anchor="end">' + (Math.round(t * 100) / 100) + '</text>';
		}
		if(lane.ref && isFinite(lane.ref.v)) h += '<line x1="' + x0 + '" x2="' + x1 + '" y1="' + Y(lane.ref.v).toFixed(1) + '" y2="' + Y(lane.ref.v).toFixed(1) + '" stroke="' + lane.ref.color + '" stroke-dasharray="5 4"/><text x="' + (x1 - 4) + '" y="' + (Y(lane.ref.v) - 4).toFixed(1) + '" text-anchor="end" style="fill:' + lane.ref.color + '">' + lane.ref.label + '</text>';
		lane.series.forEach(function(s, si) {
			if(s.hidden || !s._y.length) return;
			var d = self.shape(s, X, Y, t0, t1, pw, bars, bi, hh, y);
			if(s.type == 'bar') bi++;
			if(!d) return;
			if(s.type == 'bar') { h += '<g fill="' + s.color + '"' + CP + '>' + d + '</g>'; return; }
			if(s.area) {
				var id = 'cg' + li + '_' + si;
				defs += '<linearGradient id="' + id + '" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="' + s.color + '" stop-opacity=".25"/><stop offset="1" stop-color="' + s.color + '" stop-opacity="0"/></linearGradient>';
				h += '<path d="' + d.area + '" fill="url(#' + id + ')"' + CP + '/>';
			}
			h += '<path d="' + d.line + '" fill="none" stroke="' + s.color + '" stroke-width="' + (s.w || 2) + '" stroke-linejoin="round"' + CP + (s.dash ? ' stroke-dasharray="' + s.dash + '"' : '') + '/>';
		});
		self.laneBox.push([y, y + hh, lo, hi]);
		y += hh + gap;
	});
	y -= gap;
	// шкала времени
	if(!o.noAxis && !o.noTime) {
		var tk = SvgChart.ticks(t0, t1), full = (t1 - t0) > 36 * 3600000;
		tk.t.forEach(function(tm) {
			var xx = X(tm), d = new Date(tm);
			h += '<line x1="' + xx.toFixed(1) + '" x2="' + xx.toFixed(1) + '" y1="' + pad.t + '" y2="' + y + '" class="g v"/>' +
				'<text x="' + xx.toFixed(1) + '" y="' + (y + 15) + '" text-anchor="middle">' + (tk.day ? SvgChart.pad2(d.getDate()) + '.' + SvgChart.pad2(d.getMonth() + 1) : SvgChart.fmtTime(tm, false)) + '</text>';
		});
	}
	this.plotBottom = y;
	var H = y + pad.b;
	// навигатор: весь диапазон данных, выбранное окно подсвечено
	if(o.nav) {
		var nh = o.navH || 44, ny = H + 16, s0 = this.navSeries();
		this.navBox = [ny, ny + nh];
		h += '<text x="' + x0 + '" y="' + (ny - 1) + '" class="lt">Навигатор — весь диапазон данных</text>'; ny += 6;
		this.navBox = [ny, ny + nh];
		if(s0) {
			var lo2 = Infinity, hi2 = -Infinity;
			s0._y.forEach(function(v) { if(v != null) { if(v < lo2) lo2 = v; if(v > hi2) hi2 = v; } });
			if(hi2 - lo2 < 1e-9) { lo2 -= 1; hi2 += 1; }
			var XN = function(t) { return x0 + pw * (t - self.d0) / (self.d1 - self.d0); }, YN = function(v) { return ny + nh - nh * (v - lo2) / (hi2 - lo2); };
			var nd = self.shape({_x: s0._x, _y: s0._y, type: 'line', color: s0.color}, XN, YN, self.d0, self.d1, pw, 0, 0, nh, ny);
			if(nd) h += '<path d="' + nd.line + '" fill="none" stroke="#8a97a6" stroke-width="1.4"/>';
		}
		var sx0 = x0 + pw * (t0 - this.d0) / (this.d1 - this.d0), sx1 = x0 + pw * (t1 - this.d0) / (this.d1 - this.d0);
		h += '<rect x="' + x0 + '" y="' + ny + '" width="' + pw + '" height="' + nh + '" class="nv"/><rect x="' + sx0.toFixed(1) + '" y="' + ny + '" width="' + Math.max(2, sx1 - sx0).toFixed(1) + '" height="' + nh + '" class="ns"/>' +
			'<rect x="' + (sx0 - 3).toFixed(1) + '" y="' + ny + '" width="6" height="' + nh + '" class="nh"/><rect x="' + (sx1 - 3).toFixed(1) + '" y="' + ny + '" width="6" height="' + nh + '" class="nh"/>';
		this.nav = [sx0, sx1];
		H = ny + nh + 6;
	}
	this.H = H;
	var cur = '<line class="cur" y1="' + pad.t + '" y2="' + y + '" style="display:none"/><g class="dots"></g><rect class="zs" y="' + pad.t + '" height="' + (y - pad.t) + '" style="display:none"/>';
	el.innerHTML = '<svg class="cht" width="' + W + '" height="' + H + '" viewBox="0 0 ' + W + ' ' + H + '"><defs>' + defs + '<clipPath id="' + this.cid + '"><rect x="' + x0 + '" y="0" width="' + pw + '" height="' + this.plotBottom + '"/></clipPath></defs>' + h + cur +
		'<rect x="' + x0 + '" y="0" width="' + pw + '" height="' + this.plotBottom + '" fill="transparent" class="hit" style="touch-action:pan-y"/></svg><div class="cht-tip" style="display:none"></div>';
	el.style.position = 'relative';
	this.bind();
};

// ряд для навигатора: первый видимый ряд первой дорожки
SvgChart.prototype.navSeries = function() {
	var l = this.o.lanes[0], r = null;
	l.series.forEach(function(s) { if(!r && !s.hidden && s._y.length) r = s; });
	return r;
};

// Геометрия одного ряда в пределах окна [t0, t1]: линия/ступеньки (прореживаются до ~2 точек на пиксель)
// или столбцы. Возвращает {line, area} либо строку с прямоугольниками.
SvgChart.prototype.shape = function(s, X, Y, t0, t1, pw, nbars, bi, hh, top) {
	var x = s._x, yv = s._y, a = Math.max(0, SvgChart.lowerBound(x, t0) - 1), b = Math.min(yv.length, SvgChart.lowerBound(x, t1) + 1), i, d = '';
	if(b - a < 1) return null;
	if(s.type == 'bar') {
		var dx = Infinity;
		for(i = a + 1; i < b; i++) if(x[i] - x[i - 1] < dx) dx = x[i] - x[i - 1];
		if(dx === Infinity) dx = 86400000;
		var bw = Math.max(1, pw * dx / (t1 - t0) * 0.8 / Math.max(1, nbars)), base = Y(0), out = '';
		for(i = a; i < b; i++) {
			if(yv[i] == null) continue;
			var xx = X(x[i]) + (bi - (nbars - 1) / 2) * bw - bw / 2, yy = Y(yv[i]);
			out += '<rect x="' + xx.toFixed(1) + '" y="' + Math.min(yy, base).toFixed(1) + '" width="' + bw.toFixed(1) + '" height="' + Math.max(0.5, Math.abs(base - yy)).toFixed(1) + '"/>';
		}
		return out;
	}
	// порог разрыва линии: пропуск данных больше 5 обычных шагов
	var gapMs = s.gap || 0;
	if(!gapMs && b - a > 3) { var q = []; for(i = a + 1; i < Math.min(b, a + 40); i++) q.push(x[i] - x[i - 1]); q.sort(function(u, v) { return u - v; }); gapMs = q[q.length >> 1] * 5; }
	var cols = b - a > pw * 2, pts = [];
	if(!cols) { for(i = a; i < b; i++) if(yv[i] != null) pts.push(i); }
	else {			// на каждый пиксель - минимум и максимум в порядке следования
		var c0 = -1, mn = 0, mx = 0, cnt = 0, flush = function() { if(c0 < 0) return; if(mn == mx) pts.push(mn); else if(mn < mx) pts.push(mn, mx); else pts.push(mx, mn); };
		for(i = a; i < b; i++) {
			if(yv[i] == null) continue;
			var c = Math.floor(X(x[i]));
			if(c != c0) { flush(); c0 = c; mn = mx = i; }
			else { if(yv[i] < yv[mn]) mn = i; if(yv[i] > yv[mx]) mx = i; }
		}
		flush();
	}
	if(!pts.length) return null;
	// сегменты между разрывами данных: линия по каждому, заливка тоже по каждому (не по прямой через пропуск)
	var segs = [], seg = null, prevI = -1;
	for(var k = 0; k < pts.length; k++) {
		i = pts[k];
		if(!seg || (prevI >= 0 && gapMs && x[i] - x[prevI] > gapMs)) { seg = []; segs.push(seg); }
		seg.push([X(x[i]), Y(yv[i])]);
		prevI = i;
	}
	var area = '', bottom = top + hh;
	segs.forEach(function(g) {
		g.forEach(function(p, n) { d += (n == 0 ? 'M' : s.type == 'step' ? 'H' + p[0].toFixed(1) + 'V' : 'L') + (n == 0 || s.type != 'step' ? p[0].toFixed(1) + ',' : '') + p[1].toFixed(1); });
		if(s.area) area += 'M' + g[0][0].toFixed(1) + ',' + bottom + g.map(function(p) { return 'L' + p[0].toFixed(1) + ',' + p[1].toFixed(1); }).join('') + 'L' + g[g.length - 1][0].toFixed(1) + ',' + bottom + 'Z';
	});
	return {line: d, area: area};
};

// ---- мышь и касание: курсор, протяжка (окно), навигатор ----
SvgChart.prototype.bind = function() {
	var self = this, svg = this.el.firstChild, hit = svg.querySelector('.hit'), zs = svg.querySelector('.zs'), drag = null;
	var px = function(ev) { var r = svg.getBoundingClientRect(), p = ev.touches ? ev.touches[0] : ev; return p.clientX - r.left; };
	var tOf = function(p) { return self.t0 + (p - self.x0) / (self.x1 - self.x0) * (self.t1 - self.t0); };
	hit.addEventListener('pointerdown', function(ev) { if(self.o.zoom === false) return; drag = px(ev); hit.setPointerCapture(ev.pointerId); });
	hit.addEventListener('pointermove', function(ev) {
		var p = px(ev);
		if(drag !== null && Math.abs(p - drag) > 4) {
			zs.style.display = '';
			zs.setAttribute('x', Math.min(drag, p)); zs.setAttribute('width', Math.abs(p - drag));
			self.hover(-1);
		} else if(drag === null || Math.abs(p - drag) <= 4) self.hover(p);
	});
	hit.addEventListener('pointerup', function(ev) {
		var p = px(ev);
		zs.style.display = 'none';
		if(drag !== null && Math.abs(p - drag) > 6) self.setWindow(tOf(Math.max(self.x0, Math.min(drag, p))), tOf(Math.min(self.x1, Math.max(drag, p))));
		drag = null;
	});
	hit.addEventListener('mouseleave', function() { if(drag === null) self.hover(-1); });
	hit.addEventListener('dblclick', function() { self.setWindow(null); });
	if(this.o.nav) this.bindNav(svg, px);
};
// Навигатор: перетаскивание окна, его краёв и выбор нового места. График перерисовывается на каждом шаге,
// поэтому состояние держим в самом объекте, а движение мыши ловим на документе (элемент при этом заменяется).
SvgChart.prototype.bindNav = function(svg, px) {
	var self = this, nv = svg.querySelector('.nv');
	nv.style.cursor = 'pointer';
	nv.addEventListener('pointerdown', function(ev) {
		var p = px(ev), a = self.nav[0], b = self.nav[1], w = self.window().slice(0), mode = Math.abs(p - a) < 7 ? 'l' : Math.abs(p - b) < 7 ? 'r' : (p > a && p < b) ? 'm' : 'n';
		var left = svg.getBoundingClientRect().left;
		self.drag = {mode: mode == 'n' ? 'm' : mode, from: p, w: w, left: left};
		if(mode == 'n') {		// щелчок вне окна - окно переезжает центром на это место
			var half = (w[1] - w[0]) / 2, c = self.d0 + (p - self.x0) / (self.x1 - self.x0) * (self.d1 - self.d0);
			self.drag.w = [c - half, c + half];
			self.navApply(0);
		}
		if(!self.dragBound) {
			self.dragBound = true;
			document.addEventListener('pointermove', function(e) { if(self.drag) self.navApply((e.clientX - self.drag.left - self.drag.from) / (self.x1 - self.x0) * (self.d1 - self.d0)); });
			document.addEventListener('pointerup', function() { self.drag = null; });
		}
		ev.preventDefault();
	});
	nv.addEventListener('dblclick', function() { self.setWindow(null); });
};
SvgChart.prototype.navApply = function(dt) {
	var self = this, d = this.drag, a = d.w[0], b = d.w[1];
	if(d.mode == 'm') { a += dt; b += dt; var sh = Math.max(this.d0 - a, 0) - Math.max(b - this.d1, 0); a += sh; b += sh; }
	else if(d.mode == 'l') a = Math.min(a + dt, b - 60000);
	else if(d.mode == 'r') b = Math.max(b + dt, a + 60000);
	this.win = [Math.max(this.d0, a), Math.min(this.d1, b)];
	if(!this.raf) this.raf = requestAnimationFrame(function() { self.raf = 0; self.draw(); if(self.o.onWindow) self.o.onWindow(self.window(), false); });
};

SvgChart.prototype.hover = function(p) {
	var svg = this.el.firstChild, cur = svg.querySelector('.cur'), dots = svg.querySelector('.dots'), tip = this.el.lastChild;
	if(p < this.x0 || p > this.x1) { cur.style.display = 'none'; dots.innerHTML = ''; tip.style.display = 'none'; return; }
	var t = this.t0 + (p - this.x0) / (this.x1 - this.x0) * (this.t1 - this.t0), tol = (this.t1 - this.t0) / (this.x1 - this.x0) * 12, rows = '', d = '', tt = null, x = p;
	this.o.lanes.forEach(function(lane, li) {
		var b = this.laneBox[li];
		lane.series.forEach(function(s) {
			if(s.hidden || !s._y.length) return;
			var i = SvgChart.lowerBound(s._x, t);
			if(i > 0 && (i >= s._x.length || t - s._x[i - 1] < s._x[i] - t)) i--;
			if(i >= s._y.length || s._y[i] == null) return;
			var gap = s.gap || 0, near = Math.abs(s._x[i] - t);
			if(near > Math.max(tol, gap || 0, (s._x[Math.min(i + 1, s._x.length - 1)] - s._x[Math.max(i - 1, 0)]) || 0)) return;
			var v = s._y[i], yy = b[1] - (b[1] - b[0]) * (v - b[2]) / (b[3] - b[2]);
			if(tt === null) { tt = s._x[i]; x = this.x0 + (this.x1 - this.x0) * (tt - this.t0) / (this.t1 - this.t0); }
			if(s.type != 'bar') d += '<circle cx="' + x.toFixed(1) + '" cy="' + yy.toFixed(1) + '" r="3.5" fill="' + s.color + '" stroke="#fff" stroke-width="1.5"/>';
			rows += '<div><i style="background:' + s.color + '"></i>' + s.name + '<b>' + (s.fmt ? v.toFixed(s.fmt) : Math.round(v * 100) / 100) + (lane.unit ? ' ' + lane.unit : '') + '</b></div>';
		}, this);
	}, this);
	if(tt === null) { cur.style.display = 'none'; dots.innerHTML = ''; tip.style.display = 'none'; return; }
	cur.setAttribute('x1', x); cur.setAttribute('x2', x); cur.style.display = '';
	dots.innerHTML = d;
	var long = this.t1 - this.t0 > 36 * 3600000;
	tip.innerHTML = '<u>' + (this.o.dayData ? SvgChart.fmtDate(tt) : SvgChart.fmtTime(tt, long ? 1 : 2)) + '</u>' + rows;
	tip.style.display = 'block';
	tip.style.left = Math.min(Math.max(x + 12, 0), this.w - tip.offsetWidth - 4) + 'px';
	tip.style.top = '8px';
};

// ---- экспорт ----
SvgChart.download = function(name, mime, data) {
	var a = document.createElement('a');
	a.href = typeof data == 'string' ? 'data:' + mime + ';charset=utf-8,' + encodeURIComponent(data) : URL.createObjectURL(data);
	a.download = name;
	document.body.appendChild(a); a.click(); document.body.removeChild(a);
};
// SVG с подставленными стилями (вне страницы классы и переменные темы не работают)
SvgChart.prototype.svgText = function() {
	var s = this.el.querySelector('svg').cloneNode(true);
	[].forEach.call(s.querySelectorAll('.cur,.dots,.zs,.hit,.nh,.nv'), function(e) { e.parentNode.removeChild(e); });
	var st = document.createElementNS('http://www.w3.org/2000/svg', 'style');
	st.textContent = 'text{fill:#6b7785;font:11px sans-serif}text.lt{font-weight:700}.g{stroke:#e6eaef;fill:none}.g.v{stroke-dasharray:2 4}.ns{fill:rgba(15,139,141,.18)}';
	s.insertBefore(st, s.firstChild);
	s.setAttribute('xmlns', 'http://www.w3.org/2000/svg');
	var bg = document.createElementNS('http://www.w3.org/2000/svg', 'rect');
	bg.setAttribute('width', '100%'); bg.setAttribute('height', '100%'); bg.setAttribute('fill', '#fff');
	s.insertBefore(bg, st);
	return new XMLSerializer().serializeToString(s);
};
SvgChart.prototype.exportSvg = function(name) { SvgChart.download((name || 'chart') + '.svg', 'image/svg+xml', this.svgText()); };
SvgChart.prototype.exportPng = function(name) {
	var self = this, img = new Image(), sc = 2;
	img.onload = function() {
		var cv = document.createElement('canvas');
		cv.width = self.w * sc; cv.height = self.H * sc;
		var cx = cv.getContext('2d');
		cx.scale(sc, sc); cx.drawImage(img, 0, 0);
		cv.toBlob(function(b) { SvgChart.download((name || 'chart') + '.png', 'image/png', b); });
	};
	img.src = 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(this.svgText());
};
// CSV: время; ряд1; ряд2... - по всем видимым рядам, ряды без общей оси времени сливаются по меткам
SvgChart.prototype.csv = function(delim) {
	delim = delim || ';';
	var cols = [], times = {}, rows = [];
	this.prep();
	this.o.lanes.forEach(function(l) { l.series.forEach(function(s) { if(!s.hidden) cols.push(s); }); });
	cols.forEach(function(s, c) { s._x.forEach(function(t, i) { if(!times[t]) times[t] = []; times[t][c] = s._y[i]; }); });
	var ts = Object.keys(times).map(Number).sort(function(a, b) { return a - b; }), p = SvgChart.pad2;
	rows.push(['Время'].concat(cols.map(function(s) { return s.name; })).join(delim));
	ts.forEach(function(t) {
		var d = new Date(t), r = [d.getFullYear() + '.' + p(d.getMonth() + 1) + '.' + p(d.getDate()) + ' ' + p(d.getHours()) + ':' + p(d.getMinutes()) + ':' + p(d.getSeconds())];
		for(var c = 0; c < cols.length; c++) r.push(times[t][c] == null ? '' : times[t][c]);
		rows.push(r.join(delim));
	});
	return rows.join('\n');
};

// ---- получение данных графиков с контроллера ----

// Ответ get_listChart ("имя:выбран;имя:выбран;...") -> массив имён, номер = индекс (0 = "---")
function chartNames(list) {
	var a = list.split(';'), out = [];
	for(var i = 0; i < a.length; i++) if(a[i] !== '') out.push(a[i].substr(0, a[i].lastIndexOf(':')));
	return out;
}
// Номер графика по имени (регулярное выражение); 0 - нет такого в этой конфигурации
function chartFind(names, re) {
	for(var i = 1; i < names.length; i++) if(re.test(names[i])) return i;
	return 0;
}
// Загрузить ряд: cb(массив чисел или null, тип объекта STATS_OBJ_*). n - номер с 1 по списку из chartNames/chartNamesApi
// (нулевой пункт «---» - заглушка) или имя (EEV, OH, OH2, TT); запрос /api/chart?id=<номер с 0 | имя>.
// Запросы идут по одному (очередь): на контроллере ответ с графиком - самый длинный.
var chartQueue = [], chartBusy = false;
// Ответ ключа chart.list (массив названий) -> массив имён в прежнем виде: номер = индекс, нулевой пункт «---»
function chartNamesApi(arr) { return ['---'].concat(arr || []); }
function chartLoad(n, cb) {
	chartQueue.push([n, cb]);
	if(!chartBusy) chartNext();
}
function chartNext() {
	var q = chartQueue.shift();
	if(!q) { chartBusy = false; return; }
	chartBusy = true;
	api.call('chart', 'id=' + api.enc(typeof q[0] == 'number' ? q[0] - 1 : q[0]), function(j, err) {
		var ok = !err && j && j.d;
		q[1](ok ? j.d : [], ok ? j.obj : NaN);
		chartNext();
	});
}
