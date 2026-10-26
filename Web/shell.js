// Оболочка веб-морды: левая колонка разделов, верхняя строка состояния, вкладки.
// Подключается из menu.js (роль admin) и menu-user.js (роль user): прошивка подменяет
// запрос menu.js на menu-user.js, поэтому сам код вынесен сюда (не .html - белый список
// файлов роли user на него не действует). Роль берётся из SHELL_ROLE.
// Страница отдаёт только <div id="menu" class="menu"><script src="menu.js"></script></div>
// и <div class="content">...</div>; шапка и вкладки вставляются в начало .content.

// Карта морды: раздел -> вкладки -> страницы (файл без .html, подпись).
// Вкладка с несколькими страницами получает второй ряд переключателя.
var SHELL_MAP = [
	{id: 'home', t: 'Главная', ic: 'home', tabs: [
		{t: 'Обзор', p: [['index', 'Обзор']]},
		{t: 'Схема', p: [['plan', 'Схема']]}]},
	{id: 'climate', t: 'Климат', ic: 'thermo', tabs: [
		{t: 'Отопление', p: [['heating', 'Отопление']]},
		{t: 'ГВС', p: [['gvs', 'ГВС']]},
		{t: 'Профили', p: [['profiles', 'Профили']]},
		{t: 'Расписание', p: [['scheduler', 'Расписание']]}]},
	{id: 'charts', t: 'Графики', ic: 'chart', gear: ['charts-set', 'Состав'], tabs: [
		{t: 'Онлайн', p: [['charts', 'Онлайн']]},
		{t: 'Статистика', p: [['stats', 'Статистика']]},
		{t: 'История', p: [['history', 'История']]}]},
	{id: 'equip', t: 'Оборудование', ic: 'cpu', tabs: [
		{t: 'Температура', p: [['sensorst', 'Температура']]},
		{t: 'Давление и расход', p: [['sensorsp', 'Давление и расход']]},
		{t: 'Привязка', p: [['setsensors', 'Привязка']]},
		{t: 'ЭРВ', p: [['eev', 'Работа'], ['eev-set', 'Настройки']]},
		{t: 'Инвертор', p: [['invertor', 'Работа'], ['invertor-set', 'Настройки']]},
		{t: 'Прочее', p: [['relay', 'Прочее']]}]},
	{id: 'system', t: 'Система', ic: 'sliders', tabs: [
		{t: 'Пуск и защиты', p: [['config', 'Пуск и защиты']]},
		{t: 'Связь', p: [['lan', 'Сеть'], ['mqtt', 'MQTT и мониторинг'], ['notice', 'Уведомления']]},
		{t: 'Время', p: [['time', 'Время']]},
		{t: 'Файлы и обновление', p: [['files', 'Файлы и обновление']]},
		{t: 'Состояние', p: [['system', 'Состояние'], ['const', 'Константы сборки']]},
		{t: 'Диагностика', p: [['log', 'Журнал'], ['test', 'Тест'], ['freertos', 'Задачи RTOS'], ['modbus', 'Modbus'], ['debug', 'Флаги отладки']]}]}
];
// Страницы, доступные роли user (WebServer.cpp, HTTP_GET)
var SHELL_USER_PAGES = {index: 1, plan: 1, about: 1};
// Страницы, у которых есть справка <имя>-help.html
var SHELL_HELP = {heating: 1, gvs: 1, profiles: 1, scheduler: 1, config: 1, lan: 1, mqtt: 1, notice: 1,
	time: 1, system: 1, files: 1, log: 1, test: 1, debug: 1, 'eev-set': 1,
	index: 1, plan: 'index-help', charts: 1, 'charts-set': 'charts-help', stats: 'charts-help', history: 'charts-help',
	sensorst: 'sensors-help', sensorsp: 'sensors-help', setsensors: 'sensors-help', eev: 1, invertor: 1, 'invertor-set': 'invertor-help',
	relay: 1, modbus: 1, freertos: 1};
var SHELL_ICONS = {
	home: 'M3 11l9-8 9 8M5 10v10h14V10M10 20v-6h4v6',
	thermo: 'M10 14V5a2 2 0 0 1 4 0v9a4 4 0 1 1-4 0z',
	chart: 'M4 20V4M4 20h16M8 15l3-4 3 3 5-7',
	cpu: 'M7 7h10v10H7zM10 3v4M14 3v4M10 17v4M14 17v4M3 10h4M3 14h4M17 10h4M17 14h4',
	sliders: 'M4 7h10M18 7h2M4 17h2M10 17h10M14 4v6M6 14v6',
	user: 'M12 12a4 4 0 1 0 0-8 4 4 0 0 0 0 8zM4 21c0-4 4-6 8-6s8 2 8 6',
	close: 'M6 6l12 12M18 6L6 18',
	more: 'M12 21a9 9 0 1 0 0-18 9 9 0 0 0 0 18zM12 11v6M12 7.5h.01',
	chev: 'M9 6l6 6-6 6',
	scheme: 'M3 6h5v4H3zM16 6h5v4h-5zM9.5 14h5v4h-5zM8 8h8M12 8v6',
	phone: 'M8 3h8a1 1 0 0 1 1 1v16a1 1 0 0 1-1 1H8a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1zM11 18h2',
	monitor: 'M3 5h18v11H3zM8 20h8M12 16v4',
	cal: 'M4 6h16v14H4zM4 10h16M9 3v4M15 3v4',
	gear: 'M12 15a3 3 0 1 0 0-6 3 3 0 0 0 0 6zM19 12a7 7 0 0 0-.1-1.2l2-1.5-2-3.5-2.3 1a7 7 0 0 0-2.1-1.2L14.2 3h-4l-.4 2.6a7 7 0 0 0-2.1 1.2l-2.3-1-2 3.5 2 1.5A7 7 0 0 0 5 12a7 7 0 0 0 .1 1.2l-2 1.5 2 3.5 2.3-1a7 7 0 0 0 2.1 1.2l.4 2.6h4l.4-2.6a7 7 0 0 0 2.1-1.2l2.3 1 2-3.5-2-1.5c.1-.4.1-.8.1-1.2z'
};
var SHELL_DAYS = ['Воскресенье', 'Понедельник', 'Вторник', 'Среда', 'Четверг', 'Пятница', 'Суббота'];
var SHELL_MONTHS = ['января', 'февраля', 'марта', 'апреля', 'мая', 'июня', 'июля', 'августа', 'сентября', 'октября', 'ноября', 'декабря'];

// Раскладка: мобильная или десктопная. Выбор в переключателе под логотипом (sessionStorage 'ui' = 'm' | 'd'),
// без выбора - по ширине окна (< 760 px). Класс m на <html> включает мобильные правила style.css.
var shUi = null;
try { shUi = sessionStorage.getItem('ui'); } catch(e) {}
function shMobile() { return shUi == 'm' || (shUi != 'd' && window.innerWidth < 760); }
function shApplyUi() { document.documentElement.classList.toggle('m', shMobile()); }
function shSetUi(v) {
	try { if(v) sessionStorage.setItem('ui', v); else sessionStorage.removeItem('ui'); } catch(e) {}
	location.reload();
}
shApplyUi();
window.addEventListener('resize', shApplyUi);
// «Десктопная версия» на телефоне: страница рисуется в ширину 1100 px и масштабируется под экран
if(shUi == 'd' && Math.min(screen.width, screen.height) < 760) {
	var shVp = document.querySelector('meta[name=viewport]');
	if(shVp) shVp.setAttribute('content', 'width=1100');
}

// Плотность: компактная по умолчанию, «просторная» (класс cozy на <html>) - выбор в «Оборудование → Прочее → Интерфейс», хранится в браузере. На телефоне не действует.
var shDens = null;
try { shDens = localStorage.getItem('dens'); } catch(e) {}
function shApplyDens() { document.documentElement.classList.toggle('cozy', shDens == 'n'); }
function shSetDens(v) {
	shDens = v;
	try { localStorage.setItem('dens', v); } catch(e) {}
	shApplyDens();
	window.dispatchEvent(new Event('resize'));		// графики и схема пересчитывают размеры
	return false;
}
shApplyDens();

// Цветовая схема: светлая (по умолчанию), dark, sand, ocean - класс theme-* на <html>; выбор в «Оборудование → Прочее → Интерфейс», хранится в браузере
var SH_THEMES = ['light', 'dark', 'sand', 'ocean'], shTheme = null;
try { shTheme = localStorage.getItem('theme'); } catch(e) {}
function shApplyTheme() {
	var c = document.documentElement.classList;
	SH_THEMES.forEach(function(t) { c.remove('theme-' + t); });
	if(SH_THEMES.indexOf(shTheme) > 0) c.add('theme-' + shTheme);
}
function shSetTheme(v) {
	shTheme = v;
	try { localStorage.setItem('theme', v); } catch(e) {}
	shApplyTheme();
	return false;
}
shApplyTheme();

var shellUser = (typeof SHELL_ROLE != 'undefined' && SHELL_ROLE == 'user');
var shellState = {work: null, mode: '', status: ''};

function shIcon(name) {
	return '<svg class="ic" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="' + SHELL_ICONS[name] + '"/></svg>';
}
function shAllowed(page) { return !shellUser || SHELL_USER_PAGES[page]; }

// Текущая страница по имени файла: справки (<имя>-help, eev-sethelp) относятся к своей странице
var shPage = (function() {
	var m = location.pathname.match(/([a-z0-9-]+)\.html$/);
	var p = m ? m[1] : (document.body.className || 'index');
	return p.replace(/-?help$/, '');
})();

// Найти раздел/вкладку/страницу в карте
function shFind(page) {
	for(var i = 0; i < SHELL_MAP.length; i++) {
		var s = SHELL_MAP[i];
		if(s.gear && s.gear[0] == page) return {s: s, t: null};
		for(var j = 0; j < s.tabs.length; j++)
			for(var k = 0; k < s.tabs[j].p.length; k++)
				if(s.tabs[j].p[k][0] == page) return {s: s, t: s.tabs[j]};
	}
	return null;
}

// Переключатель версий: в колонке - два значка, в листе «Настройки» на телефоне - подписи
function shUiSwitch(txt) {
	var m = shMobile();
	return '<div class="uisw' + (txt ? ' txt' : '') + '"><a href="#" class="' + (m ? 'on' : '') + '" onclick="return shSetUi(\'m\')" title="Мобильная версия">' + shIcon('phone') + (txt ? 'Мобильная' : '') +
		'</a><a href="#" class="' + (m ? '' : 'on') + '" onclick="return shSetUi(\'d\')" title="Десктопная версия">' + shIcon('monitor') + (txt ? 'Десктопная' : '') + '</a></div>';
}

// Нижняя панель телефона: Главная / Схема / Графики / Настройки (лист со всеми разделами)
function shBar(cur) {
	var items = [['index', 'home', 'Главная'], ['plan', 'scheme', 'Схема'], ['charts', 'chart', 'Графики']], h = '<nav class="mbar">';
	for(var i = 0; i < items.length; i++) {
		if(!shAllowed(items[i][0])) continue;
		var on = shPage == items[i][0] || (items[i][0] == 'charts' && cur && cur.s.id == 'charts');
		h += '<a class="' + (on ? 'on' : '') + '" href="' + items[i][0] + '.html">' + shIcon(items[i][1]) + items[i][2] + '</a>';
	}
	var onMore = cur ? cur.s.id != 'home' && cur.s.id != 'charts' : shPage == 'about';
	return h + '<a class="' + (onMore ? 'on' : '') + '" href="#" onclick="return shMore(true)">' + shIcon('sliders') + 'Настройки</a></nav>';
}
function shMore(open) {
	var d = document.getElementById('sh-more');
	if(!d) {
		var h = '<div class="mh"><b>Настройки</b><a href="#" class="x" onclick="return shMore(false)">' + shIcon('close') + '</a></div>';
		for(var i = 0; i < SHELL_MAP.length; i++) {
			var s = SHELL_MAP[i], rows = '';
			if(s.id == 'home' || s.id == 'charts') continue;
			for(var j = 0; j < s.tabs.length; j++) {
				var t = s.tabs[j];
				if(shAllowed(t.p[0][0])) rows += '<a href="' + t.p[0][0] + '.html">' + t.t + shIcon('chev') + '</a>';
			}
			if(rows) h += '<h4>' + s.t + '</h4><div class="card ml">' + rows + '</div>';
		}
		h += '<div class="card ml"><a href="about.html">О контроллере' + shIcon('chev') + '</a>' +
			'<a href="#" onclick="return shLogin(true)">' + (shellUser ? 'Войти как администратор' : 'Учётная запись · admin') + shIcon('chev') + '</a></div>' +
			'<div class="mv"><span id="sh-fw2"></span><span>web ' + VER_WEB + '</span><span class="sh-up"></span></div>' + shUiSwitch(true);
		document.body.insertAdjacentHTML('beforeend', '<div id="sh-more" onclick="if(event.target==this)shMore(false)"><div class="sheet">' + h + '</div></div>');
		d = document.getElementById('sh-more');
		var fw = document.getElementById('sh-fw');
		if(fw) document.getElementById('sh-fw2').innerHTML = fw.innerHTML;
	}
	d.classList.toggle('open', !!open);
	document.documentElement.classList.toggle('noscroll', !!open);
	return false;
}

function shRail(cur) {
	var h = '<a href="index.html" class="logo2" title="Народный контроллер ТН"></a>' +
		'<div class="ver"><span id="sh-fw">fw</span><span>web ' + VER_WEB + '</span><span id="sh-up"></span></div>' + shUiSwitch(false);
	for(var i = 0; i < SHELL_MAP.length; i++) {
		var s = SHELL_MAP[i], first = null;
		for(var j = 0; j < s.tabs.length && !first; j++)
			for(var k = 0; k < s.tabs[j].p.length && !first; k++)
				if(shAllowed(s.tabs[j].p[k][0])) first = s.tabs[j].p[k][0];
		if(!first) continue;
		h += '<a class="nav' + (cur && cur.s == s ? ' on' : '') + '" href="' + first + '.html">' + shIcon(s.ic) + s.t + '</a>';
	}
	h += '<div class="sp"></div><div class="foot"><a class="av" href="#" onclick="return shLogin(true)" title="' +
		(shellUser ? 'Войти как администратор' : 'Учётная запись') + '">' + shIcon('user') + '</a>' +
		(shellUser ? 'user' : 'admin') +
		'<a class="abt' + (shPage == 'about' ? ' on' : '') + '" href="about.html">' + shIcon('more') + '<em>О контр.</em></a></div>';
	return h + shBar(cur);
}

function shTop(cur) {
	var title = cur ? cur.s.t : (shPage == 'about' ? 'О контроллере' : document.title.replace(/\..*$/, ''));
	var sched = shellUser ? '<span class="pill sel">' : '<a class="pill sel" href="scheduler.html">';
	var schedEnd = shellUser ? '</span>' : '</a>';
	return '<header class="top"><div><h1>' + title + '</h1><div class="sub" id="sh-date">&nbsp;</div></div><div class="grow"></div>' +
		'<span class="pill" id="sh-state"><span class="dot"></span><b id="sh-st">…</b><span class="m" id="sh-time"></span></span>' +
		'<span class="pill sel"><span class="m">Профиль</span><select id="get_listprof" aria-label="Профиль"></select></span>' +
		sched + shIcon('cal') + ' Расписание: <span id="get_schdlr-on-ONOFF">-</span><span class="m" id="sh-san"></span>' + schedEnd +
		'<label class="row"><b>ТН</b><input type="checkbox" class="sw" id="onoffswitch"></label></header>';
}

function shTabs(cur) {
	if(!cur || !cur.t && !cur.s.gear) return '';
	var h = '<nav class="tabs">';
	for(var j = 0; j < cur.s.tabs.length; j++) {
		var t = cur.s.tabs[j];
		if(!shAllowed(t.p[0][0])) continue;
		h += '<a class="tab' + (t == cur.t ? ' on' : '') + '" href="' + t.p[0][0] + '.html">' + t.t + '</a>';
	}
	h += '<span class="sp"></span>';
	if(cur.s.gear && shAllowed(cur.s.gear[0]))
		h += '<a class="tab gear' + (cur.t ? '' : ' on') + '" href="' + cur.s.gear[0] + '.html">' + shIcon('gear') + cur.s.gear[1] + '</a>';
	var help = SHELL_HELP[shPage];
	if(help && !shellUser) h += '<a class="tab gear" href="' + (help === 1 ? shPage + '-help' : help) + '.html">' + shIcon('gear') + 'Справка</a>';
	return h + '</nav>';
}

// Второй ряд: страницы одной вкладки (Сеть / MQTT / Уведомления и т.п.)
function shSub(cur) {
	if(!cur || !cur.t || cur.t.p.length < 2) return '';
	var h = '<div class="subnav"><div class="seg">';
	for(var k = 0; k < cur.t.p.length; k++) {
		var p = cur.t.p[k];
		h += '<a href="' + p[0] + '.html"' + (p[0] == shPage ? ' class="on"' : '') + '>' + p[1] + '</a>';
	}
	return h + '</div></div>';
}

// Строка пояснения справа в карточке переключателя подстраниц (название устройства и т.п.)
function shSubNote(html) {
	var s = document.querySelector('.subnav'), old = s && s.querySelector('.subnote');
	if(old) s.removeChild(old);		// заметку обновляют повторно (после «Применить»), дубли не копим
	if(s) s.insertAdjacentHTML('beforeend', '<span class="subnote">' + html + '</span>');
}

// ---- подсказки «i»: одна плавающая подсказка у края значка, не обрезается прокручиваемыми карточками и таблицами ----
(function() {
	var tip = null;
	function show(h) {
		if(!tip) { tip = document.createElement('div'); tip.id = 'sh-tip'; document.body.appendChild(tip); }
		tip.textContent = h.getAttribute('data-tip');
		tip.style.display = 'block';
		var r = h.getBoundingClientRect(), w = tip.offsetWidth, hh = tip.offsetHeight;
		var x = Math.max(8, Math.min(window.innerWidth - w - 8, r.left + r.width / 2 - w / 2));
		var y = r.top - hh - 8;
		if(y < 8) y = r.bottom + 8;
		tip.style.left = x + 'px';
		tip.style.top = y + 'px';
	}
	function hide() { if(tip) tip.style.display = 'none'; }
	function hint(e) { var t = e.target; return t && t.closest ? t.closest('.hint[data-tip]') : null; }
	document.addEventListener('mouseover', function(e) { var h = hint(e); if(h) show(h); });
	document.addEventListener('mouseout', function(e) { if(hint(e)) hide(); });
	document.addEventListener('focusin', function(e) { var h = hint(e); if(h) show(h); });
	document.addEventListener('focusout', function(e) { if(hint(e)) hide(); });
	// тач: касание значка показывает подсказку, касание в другом месте убирает
	document.addEventListener('click', function(e) { var h = hint(e); if(h) show(h); else hide(); });
	window.addEventListener('scroll', hide, true);
})();

// ---- вход / выход ----
// manual - нажатие на значок учётной записи; без него shLogin зовёт клиент api на ответ 401.
// Пара логин:пароль проверяется запросом /api/ver до сохранения: при неверной прошивка отвечает 401 на каждый запрос,
// и раньше морда на каждом опросе спрашивала «Выйти из учётной записи?», не говоря, что пароль неверен.
var shLoginOff = false;		// окно закрыли «Отменой» - опрос больше его не открывает, только значок учётной записи
function shLogin(manual) {
	var d = document.getElementById('sh-login');
	if(!d) {
		document.body.insertAdjacentHTML('beforeend',
			'<dialog id="sh-login"><form method="dialog" onsubmit="return shLoginDo(this)"><h3>Вход администратора</h3>' +
			'<p class="err" id="sh-login-e" style="display:none;color:var(--bad);font-weight:700;margin:0 0 8px"></p>' +
			'<label>Логин<input name="l" autocomplete="username"></label>' +
			'<label>Пароль<input name="p" type="password" autocomplete="current-password"></label>' +
			'<div class="btns"><button type="button" class="btn2" onclick="shLoginOff = true; this.form.parentNode.close()">Отмена</button>' +
			'<button class="btn2 p" id="sh-login-b">Войти</button></div></form></dialog>');
		d = document.getElementById('sh-login');
	}
	if(d.open) return false;
	if(NeedLogin == 2) {
		if(manual) {		// уже вошли - выход
			if(confirm('Выйти из учётной записи?')) { shLoginClear(); location.href = 'index.html'; }
			return false;
		}
		shLoginClear();		// сохранённый вход больше не принимается (пароль сменили)
		shLoginErr('Сохранённый вход не принят контроллером: войдите заново.');
	} else if(!manual && shLoginOff) return false;
	else shLoginErr('');
	d.showModal();
	return false;
}
function shLoginClear() {
	sessionStorage.removeItem('NeedLogin');
	sessionStorage.removeItem('LPString');
	NeedLogin = LPString = null;
}
function shLoginErr(t) {
	var e = document.getElementById('sh-login-e');
	if(e) { e.textContent = t; e.style.display = t ? '' : 'none'; }
}
function shLoginDo(f) {
	var lp = '!Z' + window.btoa(unescape(encodeURIComponent(f.l.value + ':' + f.p.value))), b = document.getElementById('sh-login-b');
	var r = new XMLHttpRequest();		// адрес как у api.call: хвост &&!Z... после пути
	r.open('GET', (typeof urlcontrol == 'string' ? urlcontrol : '') + '/api/ver&&' + lp, true);
	r.timeout = api.timeout;
	b.disabled = true;
	shLoginErr('');
	r.onload = function() {
		var j = null;
		b.disabled = false;
		try { j = JSON.parse(r.responseText); } catch(e) {}
		// при пустом пароле пользователя неверный пароль администратора не даёт 401, а впускает с ролью user
		if(r.status == 401 || (j && j.role != 'admin')) { shLoginErr('Неверный логин или пароль.'); f.p.value = ''; f.p.focus(); return; }
		if(r.status != 200 || !j) { shLoginErr('Контроллер не ответил (' + r.status + '), повторите.'); return; }
		sessionStorage.setItem('NeedLogin', 2);
		sessionStorage.setItem('LPString', lp);
		// роль user: страницы грузятся с Basic-авторизацией браузера, а не с этим входом. Переход на страницу администратора
		// вызывает окно входа браузера - без него страница осталась бы с меню роли user
		location.href = shellUser ? 'lan.html' : location.href;
	};
	r.onerror = r.ontimeout = function() { b.disabled = false; shLoginErr('Нет связи с контроллером, повторите.'); };
	r.send(null);
	return false;
}

// ---- связь с контроллером: плашка «Нет связи» после трёх неудач подряд (или неудачи, когда удачных ответов не было дольше 15 с) ----
// shLinkState(ok) зовёт клиент api (scripts.js) на каждый завершённый запрос.
var shLink = {fails: 0, lastOk: Date.now(), off: false};
function shLinkState(ok) {
	if(ok) {
		shLink.fails = 0;
		shLink.lastOk = Date.now();
		if(shLink.off) shLinkShow(false);
	} else {
		shLink.fails++;
		if(!shLink.off && (shLink.fails >= 3 || Date.now() - shLink.lastOk > 15000)) shLinkShow(true);
	}
}
function shLinkShow(off) {
	shLink.off = off;
	document.documentElement.classList.toggle('off', off);
	var b = document.getElementById('sh-off');
	if(off && !b) {
		var top = document.querySelector('.content > .top');
		if(top) top.insertAdjacentHTML('afterend', '<div class="offl" id="sh-off"><span>⚠ <b>Нет связи с контроллером</b></span><small id="sh-off-t"></small>' +
			'<span class="sp2"><i></i>переподключение…</span><button type="button" class="rb" onclick="shLinkRetry()">Повторить</button></div>');
		b = document.getElementById('sh-off');
	}
	if(b) {
		b.style.display = off ? '' : 'none';
		var d = new Date(shLink.lastOk), p = function(n) { return (n < 10 ? '0' : '') + n; };
		document.getElementById('sh-off-t').innerHTML = 'показаны последние полученные значения (' + p(d.getHours()) + ':' + p(d.getMinutes()) + ':' + p(d.getSeconds()) + ')';
	}
	shUpdate();
}
function shLinkRetry() { api.pollTick(); }

// ---- сообщения: всплывающее внизу страницы. Элемент создаётся при первом сообщении - работает на любой странице,
// а не только там, где есть панель «Применить» (stPanel), и до неё: шапка и api.init зовут его раньше window.onload ----
var stToastTimer = null;
function stToast(text, bad) {
	var t = document.getElementById('toast');
	if(!t) {
		if(!document.body) return;
		document.body.insertAdjacentHTML('beforeend', '<div id="toast" class="toast"></div>');
		t = document.getElementById('toast');
	}
	t.className = 'toast show' + (bad ? ' bad' : '');
	t.innerHTML = text;
	clearTimeout(stToastTimer);
	stToastTimer = setTimeout(function() { t.className = 'toast'; }, bad ? 7000 : 3500);
}
// Несовпадение версии протокола - постоянная плашка под шапкой: сообщение на 7 с легко пропустить, а сбои из-за него будут на каждой странице
function shProtoCheck(v) {
	if(!v || v.proto == API_PROTO || document.getElementById('sh-proto')) return;
	var top = document.querySelector('.content > .top');
	if(top) top.insertAdjacentHTML('afterend', '<div class="offl" id="sh-proto"><span>⚠ <b>Версия протокола контроллера (' + esc(v.proto) + ') не совпадает с версией морды (' + API_PROTO + ')</b></span><small>' +
		(v.proto > API_PROTO ? 'обновите файлы веб-морды' : 'обновите прошивку контроллера') + ', иначе часть настроек может не читаться и не записываться</small></div>');
}

// ---- верхняя строка: данные приходят из опроса shApi ----
function shUpdate() {
	var st = document.getElementById('sh-st'), dot = document.querySelector('#sh-state .dot'), tm = document.getElementById('sh-time');
	if(!st) return;
	var err = /^Error/.test(shellState.status);
	if(shLink.off) { st.innerHTML = 'Нет связи'; dot.className = 'dot bad'; tm.innerHTML = ''; return; }
	st.innerHTML = err ? esc(shellState.status) : shellState.work == null ? '…' : shellState.work ? 'Работает · ' + esc(shellState.mode.toLowerCase()) : 'Остановлен';
	dot.className = 'dot' + (err ? ' bad' : shellState.work ? '' : ' off');
	tm.textContent = shellState.work ? shellState.since || '' : '';
}
// ---- подписи для скринридера: переключателям, полям и значкам «i» без текстовой подписи; обновляются при перерисовке страницы ----
function shLabelOf(e) {
	var f = e.closest('.field,tr,.ctl-mode,.sw-row,label.row,.dl,.fl');
	var l = f && (f.querySelector('.l') || f.querySelector('td,span,b') || f);
	if(l) {
		var c = l.cloneNode(true);
		[].forEach.call(c.querySelectorAll('small,.hint,input,select,button,u'), function(x) { x.parentNode.removeChild(x); });
		var t = c.textContent.replace(/\s+/g, ' ').trim();
		if(t) return t;
	}
	var h = e.closest('.card') && e.closest('.card').querySelector('h3');
	return h ? h.firstChild.textContent.trim() : '';
}
// Скачивание файла с заданным именем и типом: контроллер отдаёт любой файл как text/html,
// из-за чего браузер дописывает расширение .html. Тип и имя задаём сами.
function shDownload(a, type) {
	var name = a.getAttribute('download') || 'file';
	fetch(a.href).then(function(r) {
		if(!r.ok) throw new Error(r.status);
		return r.arrayBuffer();
	}).then(function(b) {
		var u = URL.createObjectURL(new Blob([b], {type: type}));
		var l = document.createElement('a');
		l.href = u; l.download = name;
		document.body.appendChild(l); l.click(); l.remove();
		setTimeout(function() { URL.revokeObjectURL(u); }, 10000);
	}).catch(function() { alert('Не удалось загрузить файл ' + name); });
	return false;
}
// Кликабельные <a onclick> без href, span и div с onclick внутри страницы (не подложки окон): фокус с клавиатуры, роль «кнопка», Enter и пробел нажимают
function shButtons() {
	[].forEach.call(document.querySelectorAll('.content a[onclick]:not([href]),.content span[onclick],.content div[onclick]'), function(e) {
		if(!e.hasAttribute('role')) e.setAttribute('role', 'button');
		if(!e.hasAttribute('tabindex')) e.setAttribute('tabindex', '0');
	});
}
document.addEventListener('keydown', function(e) {
	var t = e.target;
	if((e.key == 'Enter' || e.key == ' ') && t && t.getAttribute && t.getAttribute('role') == 'button' && t.tagName != 'BUTTON' && t.hasAttribute('onclick')) { e.preventDefault(); t.click(); }
});
function shAria() {
	shButtons();
	[].forEach.call(document.querySelectorAll('.hint[data-tip]:not([aria-label])'), function(h) { h.setAttribute('role', 'note'); h.setAttribute('aria-label', h.getAttribute('data-tip')); });
	[].forEach.call(document.querySelectorAll('input.sw:not([aria-label]),.content input[type=number]:not([aria-label]),.content input[type=text]:not([aria-label]),.content input[type=password]:not([aria-label]),.content select:not([aria-label])'), function(e) {
		var t = shLabelOf(e);
		if(t) e.setAttribute('aria-label', t);
	});
}
document.addEventListener('DOMContentLoaded', function() {
	var c = document.querySelector('.content'), tm = null;
	if(!c || !window.MutationObserver) return;
	new MutationObserver(function() { clearTimeout(tm); tm = setTimeout(shAria, 300); }).observe(c, {childList: true, subtree: true});
	shAria();
});

// ---- шапка: один опрос на страницу ----
// Те же элементы, что у прежней шапки, но значения идут из /api/get одним опросом на страницу:
// state.* (состояние, время, аптайм), sched.* (расписание), profile.* (список и текущий профиль).
function shApi() {
	var sel = document.getElementById('get_listprof'), sw = document.getElementById('onoffswitch');
	if(sel) sel.onchange = function() {
		api.set({'profile.cur': sel.value}, function(D, E) {
			if(E['profile.cur']) { stToast('Профиль не выбран (' + shErr(E['profile.cur']) + ').', true); api.pollTick(); }
			else location.reload();		// значения страницы относились к прежнему профилю
		});
	};
	if(sw) sw.onchange = function() {
		var on = sw.checked;
		if(!on && !confirm('Остановить тепловой насос? Компрессор будет остановлен.')) { sw.checked = true; return; }
		api['do'](on ? 'power.on' : 'power.off', function(v, e) {
			if(e) stToast('Команда не выполнена (' + shErr(e) + ').', true);
			api.pollTick();
		});
	};
	api.init(function(v) {
		if(!v) return;
		['sh-fw', 'sh-fw2'].forEach(function(id) { var el = document.getElementById(id); if(el) el.innerHTML = 'fw ' + v.fw; });
		shProtoCheck(v);
	});
	api.poll({k: ['state.work', 'state.mode', 'state.time', 'state.date', 'state.uptime', 'state.last_cmd', 'state.status',
		'sched.on', 'sched.active_name', 'profile.list', 'profile.cur']}, shApiValues);
}
function shEsc(s) { return esc(s); }
function shErr(e) { return window.ST_ERR && ST_ERR[e] || e; }		// тексты ошибок - в settings.js, на страницах без него (about, stats) - код
function shApiValues(d) {
	var el, k;
	if('state.work' in d) {
		shellState.work = d['state.work'];
		if((el = document.getElementById('onoffswitch'))) el.checked = shellState.work;
	}
	if('state.mode' in d) shellState.mode = d['state.mode'] || '';
	if('state.status' in d) shellState.status = d['state.status'] || '';
	if('state.last_cmd' in d) shellState.since = (d['state.last_cmd'] || '').replace(/(\d+)d/, '$1 д ').replace(/(\d+)h/, '$1 ч ').replace(/(\d+)m/, '$1 мин').replace(/(\d+)s/, '$1 с').replace(/^-$/, '');
	var dt = (d['state.date'] || '').match(/^(\d+)\.(\d+)\.(\d+)$/);
	if(dt && (el = document.getElementById('sh-date'))) el.innerHTML = SHELL_DAYS[new Date(dt[3], dt[2] - 1, dt[1]).getDay()] + ', ' + Number(dt[1]) + ' ' + SHELL_MONTHS[dt[2] - 1] + ' · ' + (d['state.time'] || '');
	if('state.uptime' in d) {		// аптайм: «3 5:07m» (дни час:мин), «5:07m», «7m» или «1d 05h 07m » при SENSOR_IP
		var us = d['state.uptime'] || '', up, pad = function(n) { return ('0' + n).slice(-2); };
		if(us.indexOf(':') < 0) up = [0, /(\d+)d/.exec(us), /(\d+)h/.exec(us), /(\d+)m/.exec(us)];		// формат удалённых датчиков (SENSOR_IP): «1d 05h 07m »
		else { up = /^\s*(?:(\d+) )?(?:(\d+):)?(?:(\d+)m)?/.exec(us); up = [0, up[1] && [0, up[1]], up[2] && [0, up[2]], up[3] && [0, up[3]]]; }		// компактный: «3 5:07m»
		var ud = Number(up[1] ? up[1][1] : 0), uh = Number(up[2] ? up[2][1] : 0), um = Number(up[3] ? up[3][1] : 0);
		var ut = ud ? ud + 'd' + pad(uh) + 'h' : uh + 'h' + pad(um) + 'm', ue = document.querySelectorAll('#sh-up,.sh-up');
		for(k = 0; k < ue.length; k++) ue[k].textContent = 'up ' + ut;
	}
	if('sched.on' in d && (el = document.getElementById('get_schdlr-on-ONOFF'))) el.textContent = d['sched.on'] ? 'Вкл' : 'Выкл';
	if('sched.active_name' in d && (el = document.getElementById('sh-san'))) el.textContent = d['sched.active_name'] ? ' · ' + d['sched.active_name'] : '';
	if('profile.list' in d && (el = document.getElementById('get_listprof'))) {
		var list = d['profile.list'], cur = d['profile.cur'];
		var html = list.map(function(p) { return '<option value="' + p.i + '">' + (p.i + 1) + '. ' + shEsc(p.n) + '</option>'; }).join('');
		if(el.dataset.h !== html) { el.innerHTML = html; el.dataset.h = html; }		// список не трогаем, пока он не изменился: открытый выпадающий список закрывался на каждом опросе
		if(document.activeElement !== el) for(k = 0; k < list.length; k++) if(list[k].i === cur) el.selectedIndex = k;
	}
	shUpdate();
}

(function() {
	var cur = shFind(shPage);
	document.getElementById('menu').innerHTML = shRail(cur);
	document.addEventListener('DOMContentLoaded', function() {
		var c = document.querySelector('.content');
		if(!c) return;
		c.insertAdjacentHTML('afterbegin', shTop(cur) + shTabs(cur) + shSub(cur));
		shApi();
	});
})();
