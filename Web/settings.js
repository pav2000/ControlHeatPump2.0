// Общие части страниц настроек: поля по описанию, панель «Применить / Записать в EEPROM»,
// календарь-кисть. Подключается после scripts.js.
//
// Поле настройки несёт data-k="heat.temp1" - ключ API. Значения и варианты списков берутся из /api/get и /api/meta
// (клиент api, scripts.js, блок API-CORE), правки копятся на странице и уходят одним пакетом по «Применить»
// (stApply) с проверкой прежнего значения (if.). Что именно изменено, видно по рамке поля. «Записать в EEPROM» -
// действие save.all: пишет профили, расписание и настройки целиком, поэтому кнопка общая для всех страниц.

// ---------- поля ----------
function stKid(key) { return 'k-' + key.replace(/\./g, '-').toLowerCase(); }     // id поля: heat.temp1 -> k-heat-temp1
function stEsc(s) { return esc(s); }

// text: pw - пароль (скрыт точками, показ по значку), pattern - проверка формата до отправки, mono - моноширинный шрифт
// f: {key: ключ API, t: подпись, h: пояснение под подписью, u: единица, k: 'num'|'sel'|'sw'|'ro'|'text',
//     min,max,step, n: [номер сноски, текст подсказки], cls: класс строки, now: применять сразу (действие, а не настройка)}
// Атрибуты числового поля. min - начало отсчёта шага в браузере: при min 0,01 и шаге 0,1 допустимы 1,91 и 2,01, а 2,00 нет (стрелки
// несимметричны, ввод «2.0» красный). Поэтому min ставится, только если он кратен шагу; иначе граница в data-mn, её проверяет stValid.
function stMinAligned(mn, st) { return !st || Math.abs(mn / st - Math.round(mn / st)) < 1e-6; }
function stNumAttrs(o) {
	var st = parseFloat(o.step), h = (o.dec != null ? ' data-dec="' + o.dec + '"' : '') + ' step="' + (o.step || 'any') + '"';
	if(o.min != null) h += stMinAligned(+o.min, st) ? ' min="' + o.min + '"' : ' data-mn="' + o.min + '"';
	return h + (o.max != null ? ' max="' + o.max + '"' : '');
}
function stField(f) {
	var id = stKid(f.key), a = 'id="' + id + '" data-k="' + f.key + '"' + (f.now ? ' data-now' : ''), ctl;
	if(f.k == 'sw') ctl = '<input type="checkbox" class="sw" ' + a + '>';
	else if(f.k == 'sel') ctl = '<label class="inp sel"><select ' + a + '></select></label>';
	else if(f.k == 'ro') ctl = '<span class="ro"><b id="' + id + '" data-kr="' + f.key + '">–</b>' + (f.u ? ' <u>' + f.u + '</u>' : '') + '</span>';
	else if(f.k == 'text') ctl = '<label class="inp wide' + (f.mono ? ' mono' : '') + '"><input type="' + (f.pw ? 'password' : 'text') + '" ' + a + (f.max ? ' maxlength="' + f.max + '"' : '') +
		(f.pattern ? ' pattern="' + f.pattern + '" title="' + stEsc(f.title || 'Неверный формат') + '"' : '') + ' autocomplete="off">' + (f.pw ? '<button type="button" class="eye" tabindex="-1" title="Показать / скрыть" onclick="stEye(this)">&#128065;</button>' : '') + '</label>';
	else ctl = '<label class="inp"><input type="number" ' + a + stNumAttrs(f) + '>' + (f.u ? '<u>' + f.u + '</u>' : '') + '</label>';
	var tip = f.n ? '<span class="hint" tabindex="0" data-tip="' + stEsc(f.n[1]) + '">' + f.n[0] + '</span>' : '';
	return '<div class="field' + (f.cls ? ' ' + f.cls : '') + '" id="f-' + id + '"><div class="l">' + f.t + tip + (f.h ? '<small>' + f.h + '</small>' : '') + '</div>' + ctl + '</div>';
}
function stEye(b) { var i = b.parentNode.querySelector('input'); i.type = i.type == 'password' ? 'text' : 'password'; }
function stFields(el, list) {
	if(typeof el == 'string') el = document.getElementById(el);
	el.innerHTML = list.map(stField).join('');
}

// Загрузить значения полей (data-k и data-kr). Варианты списков и недоступные индексы - из meta.
// done(d, e, err) - когда значения применены (страница пересчитывает зависимости).
function stKMeta(key) {
	var a = key.split('.'), g = api.meta[a[0]];
	if(!g || !g.m) return null;
	return g.m[key] || (a.length == 3 ? g.m[a[0] + '.*.' + a[2]] : null);		// ключи устройств описаны один раз: temp.*.err
}
function stShow(val, el) {		// число в поле: знаков столько, сколько задаёт шаг поля (step=0.1 -> одна цифра), как показывали прежние геттеры; dec в описании поля - явное число знаков
	if(typeof val == 'number' && el.dataset.dec !== undefined) return val.toFixed(+el.dataset.dec);
	if(typeof val != 'number' || !el.step || el.step == 'any') return String(val);
	return val.toFixed((el.step.split('.')[1] || '').length);
}
function stSetK(el, key, val) {
	var m = stKMeta(key), i;
	// точное значение: по нему проверяется if. при записи. Вычисляемые ключи прошивка сверяет ПО ТЕКСТУ своего ответа, а JSON-число
	// теряет запись («110.00» -> 110, «0.5» -> 0.5): берём текст числа, как он пришёл (api.raw, scripts.js), а не строим его по числу или dec.
	el.dataset.x = val === null || val === undefined ? '' : typeof val == 'number' && api.raw[key] !== undefined && Number(api.raw[key]) === val ? api.raw[key] : String(val);
	if(el.hasAttribute('data-kr')) {		// только чтение: числа с тем числом знаков, что задаёт meta (сотые - две цифры), как показывали прежние геттеры
		el.textContent = val === null || val === undefined ? '–' : typeof val == 'boolean' ? (val ? 'да' : 'нет') : typeof val == 'number' && m && m.dec ? val.toFixed(m.dec) : val;
		return;
	}
	if(el.type == 'checkbox') el.checked = val === true || val === 1;
	else if(el.tagName == 'SELECT' && el.dataset.sv !== undefined) {
		for(i = 0; i < el.options.length; i++) if(el.options[i].value === String(val === null || val === undefined ? '' : val)) { el.selectedIndex = i; break; }
	} else if(el.tagName == 'SELECT') {
		if(m && m.o && el.options.length != m.o.length) el.innerHTML = m.o.map(function(t) { return '<option>' + stEsc(t) + '</option>'; }).join('');
		if(m && m.o) for(i = 0; i < el.options.length; i++) el.options[i].disabled = (m.na || []).indexOf(i) >= 0;
		el.selectedIndex = val;
	} else {
		stLimits(el, m);
		el.value = val === null || val === undefined ? '' : stShow(typeof val == 'boolean' ? (val ? 1 : 0) : val, el);		// флаг в числовом поле - 0/1, как в прежних полях
		// значение с контроллера, которое поле само считает неверным (шаг грубее точности прошивки, граница): красное поле при открытии; метку читает tools/check_web_pages.py
		if(el.type == 'number' && el.value !== '' && !stValid(el)) el.dataset.bad = el.value; else delete el.dataset.bad;
	}
	stBase(el);
}

// ---------- границы полей по meta прошивки ----------
// Поле не должно пропускать значение, которое прошивка отвергнет (range при «Применить»). Берётся ПЕРЕСЕЧЕНИЕ границ
// страницы и meta: у части ключей в прошивке стоят пределы типа (пороги уведомлений −327…327 °C), а на странице -
// разумные 0…30, их терять нельзя. Строки: длина из meta в байтах UTF-8 (data-bytes), в символах - не больше.
// Числа в подсказке («0…30 °C», «до 20 знаков») переписываются на действующие границы, в том числе в копии
// подсказки data-h, из которой страница её перерисовывает при погашенном поле.
function stNumTxt(v, plus) { var s = String(+(+v).toFixed(3)).replace('.', ','); return v < 0 ? '−' + s.slice(1) : (plus && v > 0 ? '+' : '') + s; }
var ST_RANGE = /([−+-]?\d+(?:,\d+)?)(\s*…\s*)([−+-]?\d+(?:,\d+)?)/, ST_LEN = /(до )(\d+)( (?:знак|символ))/;
function stLimits(el, m) {
	if(!m || el.dataset.ml) return;
	el.dataset.ml = 1;
	var re = null, fix = null;
	// data-wide - страница объявила границу ШИРЕ прошивочной (границу, которой нет, не считаем): рантайм сужает поле, но объявление
	// надо поправить; tools/check_web_pages.py читает эту метку из DOM и сообщает о ней.
	if(el.type == 'number' && m.max > m.min) {
		var pm = el.min !== '' ? el.min : el.dataset.mn !== undefined ? el.dataset.mn : '';		// нижняя граница страницы: в min или (если не кратна шагу) в data-mn
		if((pm !== '' && +pm < m.min) || (el.max !== '' && +el.max > m.max))
			el.dataset.wide = 'страница ' + (pm === '' ? 'без нижней' : pm) + ' … ' + (el.max === '' ? 'без верхней' : el.max) + ', прошивка ' + m.min + ' … ' + m.max;
		var lo = pm === '' ? m.min : Math.max(+pm, m.min), hi = el.max === '' ? m.max : Math.min(+el.max, m.max), st = parseFloat(el.step);
		el.max = hi;
		// граница прошивки −327,68 при шаге 0,1 браковала бы любое «1,0» (см. stNumAttrs): не кратную шагу в min не ставим, её проверяет stValid
		if(stMinAligned(lo, st)) { el.min = lo; el.removeAttribute('data-mn'); } else { el.removeAttribute('min'); el.dataset.mn = lo; }
		re = ST_RANGE;
		fix = function(a, x, s, y) { return stNumTxt(lo) + s + stNumTxt(hi, y.charAt(0) == '+'); };
	} else if((el.type == 'text' || el.type == 'password') && m.t == 'str' && m.max) {
		if(el.maxLength > 0 && el.maxLength > m.max) el.dataset.wide = 'страница ' + el.maxLength + ' знаков, прошивка ' + m.max;
		var n = el.maxLength > 0 ? Math.min(el.maxLength, m.max) : m.max;
		el.maxLength = n;
		el.dataset.bytes = m.max;
		re = ST_LEN;
		fix = function(a, x, y, z) { return x + n + z; };
	}
	var f = re && el.closest('.field'), s = f && f.querySelector('.l small');
	if(!s) return;
	if(s.dataset.h !== undefined) s.dataset.h = s.dataset.h.replace(re, fix);
	var w = document.createTreeWalker(s, NodeFilter.SHOW_TEXT), t;		// по текстовым узлам: элементы внутри подсказки (IP сервера, кнопка DNS) остаются теми же
	while((t = w.nextNode())) if(re.test(t.nodeValue)) { t.nodeValue = t.nodeValue.replace(re, fix); break; }
}
// Проверка поля перед отправкой: правила браузера и нижняя граница, которую нельзя было поставить в min (см. stLimits)
function stValid(el) { return el.checkValidity() && !(el.dataset.mn !== undefined && el.value !== '' && +el.value < +el.dataset.mn); }
function stLoadK(done, root) {		// root - загрузить только поля внутри элемента (таблица, подгруженная позже), остальные правки не затираются
	var els = [].slice.call((root || document).querySelectorAll('[data-k],[data-kr]')).filter(function(e) { return !e.hasAttribute('data-nol'); }), keys = [], gs = {};
	els.forEach(function(e) { var k = e.getAttribute('data-k') || e.getAttribute('data-kr'); keys.push(k); gs[k.split('.')[0]] = 1; });
	var names = Object.keys(gs), left = names.length;
	var tries = 0, D0 = {}, E0 = {};
	var go = function() {
		api.get({k: keys}, function(D, E, err) {
			var x;
			for(x in D0) if(!(x in D)) D[x] = D0[x];		// ответы повторных попыток складываем с прежними: done получает всё разом
			for(x in E0) if(!(x in E)) E[x] = E0[x];
			D0 = D; E0 = E;
			els.forEach(function(e) {		// принимаем и частичный ответ: упавшая пачка не должна лишать значений остальные
				var k = e.getAttribute('data-k') || e.getAttribute('data-kr'), row = e.closest('.field');
				if(E[k] == 'na' || E[k] == 'unknown') { if(row) row.style.display = 'none'; return; }		// параметра нет в этой сборке
				if(k in D) stSetK(e, k, D[k]);
			});
			if(err && ++tries < 5) {		// пустые поля без «Применить» хуже задержки: повторяем, пока связь не вернётся
				stToast('Не удалось загрузить значения (нет связи), повтор…', true);
				keys = keys.filter(function(k) { return !(k in D) && !(k in E); });
				els = els.filter(function(e) { var k = e.getAttribute('data-k') || e.getAttribute('data-kr'); return !(k in D) && !(k in E); });
				setTimeout(go, 3000);
				return;
			}
			setTimeout(function() { stRefresh(); if(window.stChanged) stChanged(); if(done) done(D, E, err); }, 0);
		});
	};
	api.init(function() {
		if(!left) { go(); return; }
		names.forEach(function(g) { api.metaOf(g, function() { if(--left == 0) go(); }); });
	});
}

// Поля только для чтения (data-kr): значения обновляются общим опросом страницы
function stPollRo() {
	var els = [].slice.call(document.querySelectorAll('[data-kr]')), keys = els.map(function(e) { return e.getAttribute('data-kr'); });
	if(keys.length) api.poll({k: keys}, function(d) {
		els.forEach(function(e) { var k = e.getAttribute('data-kr'); if(k in d) stSetK(e, k, d[k]); });
	});
}

// ---------- таблицы устройств (E4): датчики, реле, давление, расход, входы ----------
// Состав устройств и свойства колонок берутся из /api/meta?g=<группа>: devs - присутствующие устройства, ключи temp.*.err - описание
// атрибута для любого из них. Страница задаёт только колонки и вёрстку строки; поля получают data-k="temp.TIN.err" и работают
// с общей панелью «Применить» так же, как обычные поля.
// stDevices('temp', function(devs, meta) {...}) - дождаться списка устройств группы.
function stDevices(group, cb) {
	api.init(function() { api.metaOf(group, function(m) { cb(m && m.devs ? m.devs : [], m); }); });
}
// Ячейка таблицы для ключа group.DEV.attr. o: k - вид ('num' по умолчанию, 'sw', 'ro', 'text', 'sel'), step/min/max/u для чисел.
function stCell(key, o) {
	o = o || {};
	var id = stKid(key), a = 'id="' + id + '" ' + (o.k == 'ro' ? 'data-kr' : 'data-k') + '="' + key + '"' + (o.now ? ' data-now' : '') + (o.nol ? ' data-nol' : '') + (o.sv ? ' data-sv' : '') + (o.noecho ? ' data-noecho' : '');
	if(o.k == 'ro') return '<span ' + a + '>' + (o.init || '–') + '</span>';
	if(o.k == 'sw') return '<input type="checkbox" class="sw sm" ' + a + '>';
	if(o.k == 'sel') return '<label class="inp sel"><select ' + a + '>' + (o.opts || '') + '</select></label>';
	if(o.k == 'text') return '<label class="inp"><input type="text" ' + a + (o.max ? ' maxlength="' + o.max + '"' : '') + '></label>';
	return '<label class="inp"><input type="number" ' + a + stNumAttrs(o) + '>' + (o.u ? '<u>' + o.u + '</u>' : '') + '</label>';
}
// ---------- документация инвертора («Инвертор») ----------
// Файл выбирается по типу инвертора из /api/ver (caps.fc_type), а не по имени: имя «Omron MX2» давало ссылку «Omron%20MX2.pdf»
// на несуществующий файл. Скачивание - через shDownload: контроллер отдаёт любой файл как text/html, прямая ссылка показала бы
// двоичный мусор. Для типа без описания в морде (Vacon) - пустая строка.
var ST_FC_DOC = {omron: ['OmronMX2.pdf', 'Описание настройки Omron MX2']};
function stFcDoc(fcType) {
	var d = ST_FC_DOC[fcType];
	return d ? '<a href="' + (typeof urlcontrol == 'string' ? urlcontrol : '') + '/' + d[0] + '" download="' + d[0] + '" onclick="return shDownload(this, \'application/pdf\')">' + d[1] + ' (PDF)</a>' : '';
}

// ---------- ручное включение компрессора и ТЭНа («Тест», «Прочее») ----------
// Компрессор и ТЭН отопления без протока можно повредить, а включаются они одним щелчком. Спрашиваем, только когда прошивка
// включит устройство по-настоящему (Sensors.cpp set_Relay, OmronFC/VaconFC start_FC): компрессор - в режимах NORMAL и
// HARD_TEST, ТЭН - во всех, кроме SAFE_TEST. В тексте - какие насосы сейчас выключены. what: 'FC' (команда «Ход» инвертору)
// или имя реле; mode - test.mode (0 NORMAL, 1 SAFE_TEST, 2 TEST, 3 HARD_TEST); on - {имя реле: включено}.
var ST_RISK = {FC: ['компрессор (команда «Ход» инвертору)', ['RPUMPI', 'RPUMPO'], [0, 3]], RCOMP: ['компрессор (реле RCOMP)', ['RPUMPI', 'RPUMPO'], [0, 3]],
	RHEAT: ['ТЭН отопления (реле RHEAT)', ['RPUMPO'], [0, 2, 3]]};
function stRiskOk(what, mode, on) {
	var r = ST_RISK[what];
	if(!r || r[2].indexOf(+mode) < 0) return true;
	var off = r[1].filter(function(p) { return p in on && !on[p]; });
	return confirm('Включить ' + r[0] + ' вручную?\n' + (off.length ? (off.length > 1 ? 'Насосы ' : 'Насос ') + off.join(', ') + (off.length > 1 ? ' выключены' : ' выключен') + ': без протока оборудование можно повредить.' :
		'Без протока по контурам оборудование можно повредить.'));
}
// Что включено из реле: {имя: true/false} по полям relay.<ИМЯ>.on на странице
function stRelayOn() {
	var on = {};
	[].forEach.call(document.querySelectorAll('[data-k$=".on"][data-k^="relay."]'), function(e) { on[e.dataset.k.split('.')[1]] = e.checked; });
	return on;
}

// Все ключи таблицы: devs x attrs
function stDevKeys(group, devs, attrs) {
	var out = [];
	devs.forEach(function(d) { attrs.forEach(function(a) { out.push(group + '.' + d + '.' + a); }); });
	return out;
}

// ---------- состояние поля и отслеживание правок ----------
function stState(el) {
	if(el.type == 'checkbox') return el.checked ? '1' : '0';
	if(el.tagName == 'SELECT') return el.dataset.sv !== undefined ? el.value : String(el.selectedIndex);		// data-sv: значение - value пункта (имя, номер из скана)
	return el.value;
}
// Пункт «---» (не менять) списка привязки датчика (data-sv + data-noecho, значение «») - не правка: при отправке такое поле пропускается,
// и раньше оно считалось изменённым, а пустое значение давало «Проверьте значения, выделенные красным».
function stKeep(el) { return el.dataset.sv !== undefined && el.hasAttribute('data-noecho') && stState(el) === ''; }
function stMark(el) {
	var d = el.dataset.o !== undefined && stState(el) !== el.dataset.o && !stKeep(el);
	el.classList.toggle('chg', d);
	if(el.parentNode.classList.contains('inp') || el.parentNode.classList.contains('tg')) el.parentNode.classList.toggle('chg', d);
	return d;
}
function stBase(el) { el.dataset.o = stState(el); el.classList.remove('err'); stMark(el); }
// Изменённые поля. По умолчанию только доступные: серое (disabled) поле не уходит на контроллер и не считается правкой к применению;
// all=true - и серые тоже (для «Отмены» и предупреждения о несохранённых правках: пользователь их менял, они вернутся при включении поля).
function stDirtyList(all) { return [].slice.call(document.querySelectorAll('[data-k]')).filter(function(e) { return (all || !e.disabled) && !e.hasAttribute('data-act') && e.dataset.o !== undefined && stState(e) !== e.dataset.o && !stKeep(e); }); }

document.addEventListener('input', function(e) { stEdited(e.target, e.type); });
document.addEventListener('change', function(e) { stEdited(e.target, e.type); });
// Переключатель и список с data-now применяются сразу, но только по change: клик по ним даёт и input, и change,
// и запись уходила на контроллер дважды. stBeforeNow(el) страницы может отказать (подтверждение опасного включения) -
// тогда поле возвращается к прежнему значению.
function stEdited(el, type) {
	if(!el.dataset || el.dataset.k === undefined || el.hasAttribute('data-act')) return;
	if(el.hasAttribute('data-now') && (el.type == 'checkbox' || el.tagName == 'SELECT')) {
		if(type == 'input') return;
		if(window.stBeforeNow && stBeforeNow(el) === false) {
			if(el.type == 'checkbox') el.checked = el.dataset.o == '1'; else if(el.dataset.sv === undefined) el.selectedIndex = +el.dataset.o; else el.value = el.dataset.o;
			return;
		}
		stApply(null, [el]);
		return;
	}
	if(type == 'change') stRound(el); else stRoundNote(el);
	stMark(el);
	stRefresh();
	if(window.stChanged) stChanged(el);
}
// Число, введённое мельче шага поля (2,11 при шаге 0,1), браузер считает ошибкой, хотя прошивка десятых и сотых не различает в
// нужной пользователю точности: при выходе из поля оно округляется до шага, а рядом с полем пишется, до чего. Поля, где сотые нужны, объявляют step 0.01.
function stRoundNote(el, txt) {
	var l = el.closest && el.closest('label.inp'), n = l && l.nextElementSibling;
	if(n && n.classList.contains('rnd')) n.remove();
	if(txt && l) { n = document.createElement('span'); n.className = 'rnd'; n.textContent = txt; l.parentNode.insertBefore(n, l.nextSibling); }
}
function stRound(el) {
	stRoundNote(el);
	var st = el.type == 'number' ? parseFloat(el.step) : 0, v = parseFloat(el.value), r;
	if(!st || isNaN(v)) return;
	r = +(Math.round(v / st) * st).toFixed(6);
	if(Math.abs(r - v) < st * 1e-6) return;
	el.value = stShow(r, el);
	stRoundNote(el, 'округлено до ' + stNumTxt(st));
}

// ---------- поля с ограничением в байтах ----------
// Прошивка считает длину строки в байтах UTF-8 (кириллица - 2 байта), а maxlength - в символах: имя из русских букв
// длиннее половины лимита получало бы отказ range. Поле с data-bytes="N" обрезается по байтам при вводе.
function stBytes(s) { return unescape(encodeURIComponent(s)).length; }
document.addEventListener('input', function(e) {
	var el = e.target, n = el.dataset && el.dataset.bytes ? +el.dataset.bytes : 0, v;
	if(!n || stBytes(el.value) <= n) return;
	v = el.value;
	while(v && stBytes(v) > n) v = v.slice(0, -1);
	el.value = v;
});

// ---------- панель внизу страницы ----------
var stBar = null, stOpt = {};
function stPanel(opt) {
	stOpt = opt || {};
	var c = document.querySelector('.content');
	c.classList.add('hasbar');
	c.insertAdjacentHTML('beforeend', '<div class="savebar" id="st-bar"><span class="tag" id="st-tag">Изменений нет</span><span class="note" id="st-note"></span><span class="grow"></span>' +
		(stOpt.apply === false ? '' : '<button class="btn2" id="st-cancel" onclick="stCancel()">Отмена</button><button class="btn2 p" id="st-apply" onclick="stApply()">' + (stOpt.applyText || 'Применить') + '</button>') +
		'<button class="btn2" onclick="stSave()"><svg class="ic" style="width:16px;height:16px" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M5 4h12l3 3v13H5zM8 4v5h8V4M8 20v-6h8v6"/></svg> Записать в EEPROM</button></div>');
	stBar = true;
	stRefresh();
}
function stUnsaved(v) {
	try { if(v === undefined) return sessionStorage.getItem('unsaved') == '1'; if(v) sessionStorage.setItem('unsaved', '1'); else sessionStorage.removeItem('unsaved'); } catch(e) {}
	return false;
}
function stPlural(n) { return n + (n % 10 == 1 && n % 100 != 11 ? ' параметр' : (n % 10 >= 2 && n % 10 <= 4 && (n % 100 < 10 || n % 100 >= 20)) ? ' параметра' : ' параметров'); }
function stRefresh() {
	if(!stBar) return;
	var n = stDirtyList().length + (stOpt.extraDirty ? stOpt.extraDirty() : 0), tag = document.getElementById('st-tag'), ap = document.getElementById('st-apply');
	tag.className = 'tag' + (n || stUnsaved() ? ' w' : '');
	tag.innerHTML = n ? (typeof stOpt.dirtyText == 'function' ? stOpt.dirtyText(n) : stOpt.dirtyText || 'Изменено ' + stPlural(n)) : stUnsaved() ? 'Не записано в EEPROM' : 'Изменений нет';
	document.getElementById('st-note').innerHTML = stOpt.note || 'Применить — отправить на ТН, «Записать в EEPROM» — сохранить после перезагрузки';
	if(ap) ap.disabled = !n;
	var cn = document.getElementById('st-cancel');
	if(cn) cn.disabled = !n;
}
// ---------- «Обновить IP» у адреса сервера ----------
// Любая запись имени сервера поднимает в прошивке флаг «разрешить имя через DNS» (и MQTT.cpp, и Message.cpp), даже если имя то же.
// Кнопка отправляет текущее имя заново и читает IP каждые 1,5 с (разрешение идёт в веб-потоке контроллера): при неудаче DNS
// прошивка ставит 0.0.0.0. Страница описывает адреса в stDnsCfg: имя -> {key: ключ поля, ip: id элемента с IP, get: ключ чтения IP}.
var stDnsCfg = {}, stDnsBusy = {};
function stDnsBtn(key) {
	return ' <button type="button" class="dns" id="dns-' + key + '" onclick="stDns(\'' + key + '\')">Обновить IP</button><span class="dnsr" id="dnsr-' + key + '"></span>';
}
function stDnsShow(key, cls, html) {
	var b = document.getElementById('dnsr-' + key);
	b.className = 'dnsr ' + cls;
	b.innerHTML = html;
}
function stDns(key) {
	var d = stDnsCfg[key], el = document.querySelector('[data-k="' + d.key + '"]'), ip = document.getElementById(d.ip), btn = document.getElementById('dns-' + key);
	if(stDnsBusy[key] || !el || !ip) return;
	if(el.dataset.o !== undefined && stState(el) !== el.dataset.o) { stToast('Сначала примените изменённый адрес, потом обновляйте IP.', true); return; }
	var before = ip.textContent.trim(), n = 0;
	var end = function(cls, html) { stDnsBusy[key] = 0; btn.disabled = false; stDnsShow(key, cls, html); };
	// после отправки имени читаем IP каждые 1,5 с (разрешение идёт в веб-потоке контроллера): при неудаче DNS прошивка ставит 0.0.0.0
	var watch = function(v) {
		v = String(v).trim();
		ip.textContent = v;
		n++;
		if(v == '0.0.0.0' && before != '0.0.0.0') end('bad', '✕ DNS не отвечает');
		else if(v != before && v != '0.0.0.0') { stUnsaved(true); stRefresh(); end('ok', '✓ адрес обновился'); }
		else if(n >= 7) { if(v == '0.0.0.0') end('bad', '✕ DNS не отвечает'); else end('ok', '✓ адрес подтверждён, не изменился'); }
		else return false;
		return true;
	};
	stDnsBusy[key] = 1;
	btn.disabled = true;
	stDnsShow(key, 'busy', '<i class="spin"></i>Запрос к DNS…');
	var pr = {}; pr[d.key] = el.tagName == 'SELECT' ? el.selectedIndex : el.value;
	api.set(pr, function(D, E) {
		if(E[d.key]) { end('bad', '✕ контроллер не принял команду (' + (ST_ERR[E[d.key]] || E[d.key]) + ')'); return; }
		var miss = 0, tick = function() {
			api.get({k: [d.get]}, function(D2) {
				if(D2[d.get] === undefined) {		// нет ответа (обрыв связи) - не считать значением
					if(++miss > 7) end('bad', '✕ нет связи с контроллером'); else setTimeout(tick, 1500);
					return;
				}
				if(!watch(D2[d.get])) setTimeout(tick, 1500);
			});
		};
		setTimeout(tick, 1500);
	});
}

// Тексты ошибок по ключам API
var ST_ERR = {conflict: 'значение изменилось на контроллере, обновите страницу', range: 'вне допустимых границ', bad_value: 'неверное значение',
	forbidden: 'нет прав', busy: 'недоступно: ТН работает', na: 'недоступно в этой сборке', unknown: 'неизвестный параметр', too_big: 'слишком длинное значение',
	fail: 'контроллер не смог выполнить', net: 'нет связи'};
function stErrText(e) { return ST_ERR[e] || e; }
// Сообщение об отказе: «<что не удалось> (<причина>).»; code - код из E[ключ] или 'net'
function stFail(what, code) { stToast(what + ' (' + stErrText(code) + ').', true); }
// Записать поля пакетом; к каждому ключу прикладывается прежнее значение (if.), чтобы не затереть чужую правку.
// cb({ok: число принятых, errs: тексты ошибок})
function stSendK(els, cb) {
	var pairs = {}, olds = {}, by = {};
	els.forEach(function(el) {
		var k = el.dataset.k;
		var v = el.type == 'checkbox' ? (el.checked ? 1 : 0) : el.tagName == 'SELECT' ? (el.dataset.sv !== undefined ? el.value : el.selectedIndex) : el.value;
		if(el.dataset.sv !== undefined && el.hasAttribute('data-noecho') && v === '') return;		// пункт «не менять» (привязка датчика к адресу)
		pairs[k] = v;
		if(el.dataset.x && !el.hasAttribute('data-noecho')) olds[k] = el.dataset.x;
		by[k] = el;
	});
	api.set(pairs, function(D, E, err) {
		var errs = [], ok = 0;
		Object.keys(by).forEach(function(k) {
			var el = by[k];
			if(E[k]) { el.classList.add('err'); errs.push(stLabel(el) + ' (' + (ST_ERR[E[k]] || E[k]) + ')'); }
			else if(k in D) { if(el.hasAttribute('data-noecho')) stBase(el); else stSetK(el, k, D[k]); ok++; }		// noecho: ответ - не значение поля (адрес датчика)
		});
		cb({ok: ok, errs: errs});
	}, olds);
}

function stLabel(el) {
	var f = el.closest('.field'), l = f ? f.querySelector('.l') : null, tr = el.closest('tr');
	if(l) return l.firstChild.textContent.trim();
	return tr && tr.cells.length ? tr.cells[0].firstChild.textContent.trim() : el.id;		// таблицы датчиков: имя в первом столбце
}

// ---------- защита от потери правок ----------
function stHasDirty() { return stDirtyList(true).length || (stOpt && stOpt.extraDirty && stOpt.extraDirty()); }
// Переход по ссылкам сайта (колонка, вкладки, справка): своё окно «Покинуть страницу?». Текст окна браузера при закрытии
// вкладки и обновлении страницы (beforeunload) задать нельзя, поэтому после нашего подтверждения это окно не показываем.
var stLeaving = false;
document.addEventListener('click', function(e) {
	var a = e.target.closest ? e.target.closest('a[href]') : null;
	if(!a || e.defaultPrevented || e.button || e.ctrlKey || e.metaKey || e.shiftKey || a.target == '_blank' || a.hasAttribute('download')) return;
	var h = a.getAttribute('href');
	if(!h || h.charAt(0) == '#' || /^javascript:/i.test(h) || !stHasDirty()) return;
	if(confirm('Покинуть страницу? Есть неприменённые изменения, они будут потеряны.')) stLeaving = true;
	else { e.preventDefault(); e.stopPropagation(); }
}, true);
window.addEventListener('beforeunload', function(e) {
	if(stLeaving) return;
	if(stHasDirty()) { e.preventDefault(); e.returnValue = ''; }
});

function stApply(then, only) {
	// страница со своим содержимым (календарь расписания): onApply сохраняет его и сам вызывает then
	if(!only && stOpt.onApply && stOpt.extraDirty && stOpt.extraDirty()) { stOpt.onApply(then); return; }
	var els = only || stDirtyList(), bad = [];
	els.forEach(function(el) {
		if(!stValid(el) || (el.type != 'checkbox' && el.value === '')) { bad.push(el); el.classList.add('err'); }
	});
	if(bad.length) { stToast('Проверьте значения, выделенные красным: ' + bad.map(stLabel).join(', '), true); return; }
	if(!els.length) { if(then) then(); return; }
	stSendK(els, function(r) {
		if(r.ok) stUnsaved(true);
		stRefresh();
		if(window.stAfterApply) stAfterApply();
		if(r.errs.length) stToast('Не принято: ' + r.errs.join(', ') + '.', true);
		else if(r.ok) stToast('Применено: ' + stPlural(r.ok) + '. Записать в EEPROM — чтобы сохранить после перезагрузки.');
		if(then && !r.errs.length) then();
	});
}
function stCancel() {
	stDirtyList(true).forEach(function(el) {
		var o = el.dataset.o;
		if(el.type == 'checkbox') el.checked = o == '1';
		else if(el.tagName == 'SELECT') { if(el.dataset.sv !== undefined) el.value = o; else el.selectedIndex = Number(o); }
		else el.value = o;
		el.classList.remove('err');
		stMark(el);
	});
	if(stOpt.onCancel) stOpt.onCancel();
	stRefresh();
	if(window.stAfterApply) stAfterApply();
}
// «Записать в EEPROM»: если есть неприменённые правки, сначала применяем их
function stSave() {
	var go = function() {
		api['do']('save.all', function(v, e) {
			if(!e && v >= 0) { stUnsaved(false); stRefresh(); stToast('Настройки записаны в EEPROM.'); }
			else stToast('Запись в EEPROM не удалась (' + (ST_ERR[e] || e) + ').', true);
		});
	};
	if(stDirtyList().length || (stOpt.extraDirty && stOpt.extraDirty())) stApply(go); else go();
}

// ---------- календарь-кисть ----------
// Сетка 7 дней x 24 часа. Ячейки ищутся по id="calendar" и классу clc (непустое содержимое - включено),
// разметка держит эти имена (scheduler.html кодирует календарь для do-действий по ним же).
var ST_DAYS = ['Пн', 'Вт', 'Ср', 'Чт', 'Пт', 'Сб', 'Вс'];
// opt.paint(cell, erase) - записать в ячейку; opt.changed() - после правки.
// opt.toggle - протяжка, начатая с закрашенной ячейки, стирает: на телефоне нет ни Shift, ни правой кнопки (календарь ГВС,
// где одна кисть). В «Расписании» кистей несколько, там протяжка по закрашенному перекрашивает - toggle не включается.
function stCalendar(el, opt) {
	var h = '<span></span>';
	for(var i = 0; i < 24; i++) h += '<b>' + (i < 10 ? '0' : '') + i + '</b>';
	for(var d = 0; d < 7; d++) {
		h += '<span class="d">' + ST_DAYS[d] + '</span>';
		for(i = 0; i < 24; i++) h += '<i class="clc" data-n="' + (d * 24 + i) + '"></i>';
	}
	el.innerHTML = h;
	el.id = 'calendar';
	el.classList.add('cal');
	var start = null, startOn = false, cells = [].slice.call(el.querySelectorAll('.clc'));
	function box(a, b) {		// прямоугольник между двумя ячейками
		var x0 = Math.min(a % 24, b % 24), x1 = Math.max(a % 24, b % 24), y0 = Math.min(a / 24 | 0, b / 24 | 0), y1 = Math.max(a / 24 | 0, b / 24 | 0);
		return cells.filter(function(c) { var n = +c.dataset.n; return n % 24 >= x0 && n % 24 <= x1 && (n / 24 | 0) >= y0 && (n / 24 | 0) <= y1; });
	}
	function mark(cur) { cells.forEach(function(c) { c.classList.remove('sel'); }); if(start !== null) box(start, cur).forEach(function(c) { c.classList.add('sel'); }); }
	function under(ev) {
		var t = document.elementFromPoint(ev.clientX, ev.clientY);
		return t && t.classList && t.classList.contains('clc') ? +t.dataset.n : null;
	}
	el.addEventListener('pointerdown', function(ev) {
		var n = under(ev);
		if(n === null) return;
		start = n;
		startOn = cells[n].innerHTML !== '';
		mark(n);
		ev.preventDefault();
	});
	el.addEventListener('pointermove', function(ev) {
		if(start === null) return;
		var n = under(ev);
		if(n !== null) mark(n);
	});
	document.addEventListener('pointerup', function(ev) {
		if(start === null) return;
		var n = under(ev), erase = ev.shiftKey || ev.button == 2 || (opt.toggle && startOn);
		if(n === null) n = start;
		box(start, n).forEach(function(c) { opt.paint(c, erase); });
		start = null;
		mark(null);
		if(opt.changed) opt.changed();
	});
	el.addEventListener('contextmenu', function(ev) { ev.preventDefault(); });
	return cells;
}

// ---------- ночной тариф: полоса суток с подсвеченным интервалом ----------
// p - префикс идентификаторов ('get_heat-', 'get_boiler-'): поля adt, adh, adeh; el - контейнер полосы
function stNightBar(el, p) {
	var g = function(n) { return document.getElementById(p + n); }, t = parseFloat(g('adt').value), a = parseInt(g('adh').value), b = parseInt(g('adeh').value), seg = [], txt;
	if(isNaN(t) || isNaN(a) || isNaN(b)) return;
	if(a < b) seg.push([a, b]); else if(a > b) seg.push([a, 24], [0, b]);
	var pad = function(n) { return (n < 10 ? '0' : '') + n; };
	txt = t == 0 ? 'добавка не работает (0 °C)' : (t > 0 ? '+' : '') + t + ' °C с ' + pad(a) + ':00 до ' + pad(b) + ':00';
	el.innerHTML = '<div class="nbar">' + (t == 0 ? '' : seg.map(function(s) { return '<i style="left:' + s[0] / 24 * 100 + '%;width:' + (s[1] - s[0]) / 24 * 100 + '%"></i>'; }).join('')) +
		'<span>' + txt + '</span></div><div class="nax"><span>00</span><span>06</span><span>12</span><span>18</span><span>24</span></div>';
}
