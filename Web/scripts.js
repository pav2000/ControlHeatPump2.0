// Copyright (c) 2016-2026 by Pavel Panfilov <firstlast2007@gmail.com>
var VER_WEB = "2.25";
var urlcontrol = ''; //  автоопределение (если адрес сервера совпадает с адресом контроллера)
// адрес и порт контроллера, если адрес сервера отличен от адреса контроллера (не рекомендуется)
//var urlcontrol = 'http://192.168.0.199';
//var urlcontrol = 'http://192.168.0.7';
//var urlcontrol = 'http://77.50.254.24:25402';
var urlupdate = 4000; // время обновления параметров в миллисекундах

/*API-CORE*/
// ---------------------------------------------------------------------------------------------
// Клиент нового API контроллера (/api/...). Протокол - tools/api/spec.md.
// Блок самодостаточен: tools/mk_min_web.py берёт его между маркерами API-CORE и вставляет в
// аварийную страницу (вариант C). Зависит только от urlcontrol, urlupdate, NeedLogin/LPString
// (есть ниже в scripts.js, в аварийной странице их нет - там и авторизации нет) и необязательных
// shLinkState(ok), shLogin(). Значения в страницу вставлять через textContent / value.
// ---------------------------------------------------------------------------------------------
var API_PROTO = 1;                   // версия протокола, с которой работает эта морда
var api = {
	timeout: 3500,                     // не короче серверного ожидания мьютекса (3000 мс), иначе повторы на занятый контроллер
	ver: null,                         // ответ /api/ver после api.init
	meta: {},                          // группа -> описание полей
	pollers: [], pollTimer: null, pollBusy: 0, pollAgain: 0,
	metaWait: {}, verWait: null          // запросы meta и ver в пути: очереди ожидающих
};

// Текст числа так, как он пришёл от контроллера: api.raw['heat.temp1'] = '20.5', api.raw['fc.max'] = '110.00'. Прежнее значение (if.) для
// вычисляемых ключей прошивка сверяет по ТЕКСТУ своего ответа, а JSON.parse теряет запись числа (110.00 -> 110, «0.5» и «0.500» одно и то же),
// поэтому формат нельзя угадать по числу и по meta.dec: у fc.* нули на конце есть, у eev.kp/ki/kd нет.
api.raw = {};
api.rawOf = function(text, d) {
	for(var k in d) if(typeof d[k] == 'number') {
		var m = new RegExp('"' + k.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + '":(-?[0-9][0-9.eE+-]*)').exec(text);
		if(m && Number(m[1]) === d[k]) api.raw[k] = m[1];
	}
};

// Один запрос. cb(json, err): err = {code, net}, json - разобранное тело. Ошибки по ключам внутри json.e.
api.call = function(verb, query, cb, tmo) {
	var r = new XMLHttpRequest(), auth = (typeof NeedLogin != 'undefined' && NeedLogin == 2) ? '&&' + LPString : '';
	r.open('GET', (typeof urlcontrol == 'string' ? urlcontrol : '') + '/api/' + verb + (query ? '?' + query : '') + auth, true);
	r.timeout = tmo || api.timeout;
	r.onload = function() {
		var j = null;
		if(window.shLinkState) shLinkState(true);
		if(r.status == 401) { if(window.shLogin) shLogin(); cb(null, {code: 401}); return; }
		try { j = JSON.parse(r.responseText); } catch(e) {}
		if(j && j.d && (verb == 'get' || verb == 'set')) api.rawOf(r.responseText, j.d);
		if(r.status != 200 || j === null) cb(null, {code: r.status});
		else cb(j, null);
	};
	r.onerror = r.ontimeout = function() { if(window.shLinkState) shLinkState(false); cb(null, {code: 0, net: true}); };
	r.send(null);
};

// Значение в строку запроса: булево - 1/0, пустое - пустая строка
api.enc = function(v) { return encodeURIComponent(v === true ? 1 : v === false ? 0 : v == null ? '' : v); };

// Чтение: o.k - массив ключей, o.g - группа (необязательно), o.a - атрибуты группы устройств.
// Длинный список режется на запросы по 40 ключей / 1200 знаков. cb(d, e, err): значения тех пачек, что пришли, приходят и при сбое другой
// (err - последний сбой). Ответ «не поместилось» (прошивка: e["*"] = too_big, весь ответ; e[ключ] = too_big, один ключ) не теряется:
// пачка делится пополам, а ключ из пачки повторяется один; когда делить уже нечего, ошибка остаётся в e.
api.get = function(o, cb) {
	var parts = [], cur = [], len = 0, D = {}, E = {}, fail = null, left = 0;
	(o.k || []).forEach(function(k) {
		if(cur.length && (cur.length >= 40 || len + k.length > 1200)) { parts.push(cur); cur = []; len = 0; }
		cur.push(k); len += k.length + 1;
	});
	if(cur.length) parts.push(cur);
	if(o.g && !parts.length) parts.push([]);
	if(!parts.length) { cb(D, E, null); return; }
	function run(ks, withG) {		// withG - пачка несёт группу (g, a): только первая
		var q = (ks.length ? 'k=' + ks.join(',') : '') + (withG ? (ks.length ? '&' : '') + 'g=' + o.g + (o.a ? '&a=' + o.a : '') : '');
		left++;
		api.call('get', q, function(j, err) {
			var x, big = [];
			if(err) fail = err;
			else if(j.e && j.e['*'] == 'too_big' && ks.length > 1) {
				var h = ks.length >> 1;
				run(ks.slice(0, h), withG); run(ks.slice(h), false);
			} else {
				for(x in j.d) D[x] = j.d[x];
				for(x in j.e) { if(j.e[x] == 'too_big' && x != '*' && ks.length > 1) big.push(x); else E[x] = j.e[x]; }
				big.forEach(function(k) { run([k], false); });
			}
			if(--left == 0) cb(D, E, fail);
		});
	}
	parts.forEach(function(ks, i) { run(ks, !!o.g && i == 0); });
};

// Запись: pairs - {ключ: значение}, olds - {ключ: ожидаемое прежнее значение} (E3: при расхождении ключ даст conflict).
// Пачки по 20 пар / 1200 знаков уходят по очереди. cb(d, e, err): d - значения, прочитанные ПОСЛЕ записи.
api.set = function(pairs, cb, olds) {
	var groups = [], cur = [], len = 0, D = {}, E = {}, fail = null;
	Object.keys(pairs).forEach(function(k) {
		var s = k + '=' + api.enc(pairs[k]) + (olds && olds[k] !== undefined ? '&if.' + k + '=' + api.enc(olds[k]) : '');
		if(cur.length && (cur.length >= 20 || len + s.length > 1200)) { groups.push(cur); cur = []; len = 0; }
		cur.push(s); len += s.length + 1;
	});
	if(cur.length) groups.push(cur);
	(function next() {
		var g = groups.shift();
		if(!g) { cb(D, E, fail); return; }
		api.call('set', g.join('&'), function(j, err) {
			if(err) { fail = err; Object.keys(pairs).forEach(function(k) { if(!(k in D) && !(k in E)) E[k] = 'net'; }); groups = []; }
			else { var x; for(x in j.d) D[x] = j.d[x]; for(x in j.e) E[x] = j.e[x]; }
			next();
		});
	})();
};

// Действие: cb(результат, код ошибки или '', err)
api['do'] = function(action, cb, p, tmo) {		// tmo - свой тайм-аут для долгих действий (скан шины OneWire до 9 с)
	api.call('do', 'a=' + action + (p !== undefined ? '&p=' + api.enc(p) : ''), function(j, err) {
		if(err) { cb(null, 'net', err); return; }
		cb(j.d[action], j.e[action] || '', null);
	}, tmo);
};

// Описание полей группы: api.meta[g] = {sch, m: {ключ: {t, dec, min, max, o, na, w}}, devs}
api.metaOf = function(g, cb) {
	if(api.meta[g]) { cb(api.meta[g]); return; }
	var ck = null;
	try { if(api.ver) ck = sessionStorage.getItem('am.' + g + '.' + api.ver.sch); } catch(e) {}
	if(ck) { try { api.meta[g] = JSON.parse(ck); cb(api.meta[g]); return; } catch(e) {} }
	var w = api.metaWait[g];		// запрос уже в пути - не повторяем, ждём его ответа
	if(w) { w.push(cb); return; }
	w = api.metaWait[g] = [cb];
	api.call('meta', 'g=' + g, function(j, err) {
		delete api.metaWait[g];
		if(j) {
			api.meta[g] = j;
			try { if(api.ver) sessionStorage.setItem('am.' + g + '.' + api.ver.sch, JSON.stringify(j)); } catch(e) {}
		}
		w.forEach(function(f) { f(j); });
	});
};

// Проверка версии протокола и состава сборки. cb(ver). При несовпадении версии - сообщение вместо тихих сбоев.
api.init = function(cb) {
	if(api.ver) { cb(api.ver); return; }
	if(api.verWait) { api.verWait.push(cb); return; }		// запрос в пути: шапка, страница и stLoadK зовут init почти одновременно
	var w = api.verWait = [cb];
	api.call('ver', '', function(j, err) {
		api.verWait = null;
		if(j) {
			api.ver = j;
			if(j.proto != API_PROTO) {
				var m = 'Версия протокола контроллера (' + j.proto + ') не совпадает с версией морды (' + API_PROTO + '): ' +
					(j.proto > API_PROTO ? 'обновите файлы веб-морды.' : 'обновите прошивку контроллера.');
				if(window.stToast) stToast(m, true); else alert(m);
			}
		}
		w.forEach(function(f) { f(j || null); });
	});
};

// Опрос: единый таймер на страницу. spec.k - ключи, cb(d, e) получает значения своих ключей.
// Один запрос за тик (ключи всех подписчиков), скрытая вкладка не опрашивает и обновляется сразу при возвращении.
api.poll = function(spec, cb) {
	api.pollers.push({k: spec.k || [], cb: cb});
	if(api.pollBusy) api.pollAgain = 1; else api.pollTick();
};
api.pollTick = function() {
	clearTimeout(api.pollTimer);
	api.pollTimer = setTimeout(api.pollTick, urlupdate);
	if(document.hidden || api.pollBusy || !api.pollers.length) return;
	var seen = {}, keys = [];
	api.pollers.forEach(function(p) { p.k.forEach(function(k) { if(!seen[k]) { seen[k] = 1; keys.push(k); } }); });
	api.pollBusy = 1;
	api.get({k: keys}, function(D, E, err) {
		api.pollBusy = 0;
		if(!err || Object.keys(D).length) api.pollers.forEach(function(p) {		// сбой одной пачки не отбрасывает значения остальных (шапка, страница)
			var d = {}, e = {};
			p.k.forEach(function(k) { if(k in D) d[k] = D[k]; if(k in E) e[k] = E[k]; });
			try { p.cb(d, e); } catch(x) { if(window.console) console.error(x); }		// сбой одной страницы не должен обрывать шапку и остальных
		});
		if(api.pollAgain) { api.pollAgain = 0; api.pollTick(); }
	});
};
document.addEventListener('visibilitychange', function() { if(!document.hidden && api.pollers.length) api.pollTick(); });
/*END API-CORE*/

// Вход администратора: shell.js кладёт пару логин:пароль в sessionStorage, api.call и выгрузки файлов дописывают её к адресу
var NeedLogin = sessionStorage.getItem("NeedLogin");
var LPString = sessionStorage.getItem("LPString");

// ---- общие помощники страниц (раньше копировались в каждую страницу) ----
function $(id) { return document.getElementById(id); }
function put(id, html) { var e = $(id); if(e) e.innerHTML = html; }
function num(v, d) { var f = parseFloat(v); return isNaN(f) ? '–' : f.toFixed(d); }          // null, undefined и текст дают «–»
function esc(s) { return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;'); }     // для текста и для значений атрибутов
// Код ошибки датчика/ЭРВ: 0 - «OK», пусто - «–», иначе «E<код>»
function stCode(v) { return v === null || v === undefined || v === '' ? '–' : v == 0 ? '<span class="okc">OK</span>' : '<span class="errc">E' + esc(v) + '</span>'; }
