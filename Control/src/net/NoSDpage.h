/*
 * Copyright (c) 2016-2026 by Pavel Panfilov <firstlast2007@gmail.com>  telegram @pav20000
 * "Народный контроллер 2.0" для тепловых насосов.
 * Данное программное обеспечение предназначено для управления
 * различными типами тепловых насосов для отопления и ГВС.
 *
 * This file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 3.0 of the License, or (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 */
// ---------------------------------------------------------------------------
// Встроенная (аварийная) веб-морда: лежит во flash прошивки и отдаётся, когда
// морду не удалось взять ни с SD карты, ни с SPI флеша, а также по короткому
// нажатию KEY1 при включении.
//
// ФАЙЛ СОБРАН АВТОМАТИЧЕСКИ, ПРАВИТЬ ЕГО НЕ НАДО.
//   источник:  Main-min/index.html
//   сборка:    tools/mk_min_web.py pack
//   проверка:  входит в tools/ci.sh
//
// Страница обязана быть самодостаточной: в аварии носитель сломан, поэтому
// внешних css, js и картинок у неё быть не может - только один документ.
// ---------------------------------------------------------------------------

#ifndef NoSDpage_h
#define NoSDpage_h

const char index_noSD[] =
"<!DOCTYPE html>\n<html lang=\"ru\">\n<head>\n<meta charset=\"utf-8\">\n<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n<title>Аварийный режим. Народный контроллер ТН</title>\n<!--\n  Встроен"
"ная (аварийная) веб-морда. Лежит в прошивке, отдаётся когда морду не\n  удалось взять ни с SD карты, ни с SPI флеша, а также по короткому нажатию KEY1\n  при включении.\n\n  ПРАВИТЬ ЗДЕСЬ, а не в Control/"
"src/net/NoSDpage.h: заголовок собирается из этого\n  файла командой tools/mk_min_web.py pack, соответствие проверяет tools/ci.sh.\n\n  Страница обязана быть самодостаточной: в аварии носитель сломан, поэ"
"тому\n  никаких внешних css, js и картинок - только один документ. Поэтому же здесь\n  нет ни jQuery, ни Highcharts: графики рисуются своим svg в десяток строк.\n\n  Работает с новым API (/api, протокол t"
"ools/api/spec.md). Клиентское ядро api.*\n  между маркерами API-CORE не пишется руками: tools/mk_min_web.py вставляет его\n  из Main/scripts.js при сборке, так что у полной и аварийной морды один клиент"
".\n  Заливка файлов морды (POST, заголовок Title:) вне API и от него не зависит.\n  Авторизации в странице нет: режим включается физически (KEY1), в safeNetwork\n  пароль не спрашивается совсем, а пароли"
" сети видны - так доступ восстанавливают,\n  когда пароль забыт.\n-->\n<style>\n*{box-sizing:border-box}\nbody{margin:0;padding:8px;font:14px/1.45 Arial,Helvetica,sans-serif;color:#222;min-height:100vh;\nba"
"ckground:linear-gradient(to bottom,#57819e 0,#a6bec8 38%,#d7cdb1 90%,#b0a196 100%);background-attachment:fixed}\nh1{margin:2px 4px 10px;font-size:19px;color:#fff;text-shadow:0 1px 2px rgba(0,0,0,.4)}\nh"
"1 span{font-size:13px;font-weight:normal}\nh2{margin:0 0 6px;font-size:15px;padding-bottom:4px;border-bottom:1px solid rgba(0,0,0,.15)}\n.g{display:grid;grid-template-columns:repeat(auto-fit,minmax(300p"
"x,1fr));gap:8px}\nsection{background:rgba(255,255,255,.72);border-radius:6px;padding:8px 10px}\n.r{display:flex;flex-wrap:wrap;justify-content:space-between;align-items:center;gap:6px;min-height:1.9em}\n"
".r>i{font-style:normal;color:#555}\n.r>b{text-align:right}\n.big{font-size:17px}\nhr{border:0;border-top:1px solid rgba(0,0,0,.15);margin:7px 0}\nbutton,input,select,a.b{font:inherit}\nbutton,a.b{display:i"
"nline-block;padding:4px 9px;margin:1px 0;border:1px solid rgba(0,0,0,.25);border-radius:4px;\nbackground:#fff;color:#222;cursor:pointer;text-decoration:none}\nbutton:hover,a.b:hover{background:#eef3f7}\n"
"button.d{background:#f8e3e3}\ninput[type=text]{width:140px;padding:2px 4px}\ninput[type=file]{max-width:100%}\n.e{color:#a00}\n.ok{color:#070}\n#m{position:sticky;top:0;z-index:9;margin:0 4px 8px;padding:6"
"px 9px;border-radius:5px;background:#fff;\nborder-left:4px solid #57819e;display:none}\n.note{font-size:12px;color:#444;font-style:italic}\n#m{white-space:pre-line}\nsvg{width:100%;height:auto;display:blo"
"ck}\n</style>\n</head>\n<body>\n<h1>Народный контроллер ТН <span id=\"hdr\">&mdash; аварийный веб-интерфейс</span></h1>\n<div id=\"m\"></div>\n<div class=\"g\">\n\n<section>\n<h2>Тепловой насос</h2>\n<div class=\"r\"><"
"i>Режим работы</i><b class=\"big\" data-k=\"state.mode\">-</b></div>\n<div class=\"r\"><i>Компрессор</i><b data-k=\"state.work\">-</b></div>\n<div class=\"r\"><i>Код ошибки</i><b data-k=\"state.errcode\">-</b></div"
">\n<div class=\"r\"><i>Последняя ошибка</i><b class=\"e\" data-k=\"state.error\">-</b></div>\n<div class=\"r\"><i>Наработка</i><b data-k=\"state.uptime\">-</b></div>\n<hr>\n<div class=\"r\"><button onclick=\"act('powe"
"r.on','Пуск теплового насоса?')\">Пуск</button>\n<button class=\"d\" onclick=\"act('power.off','Останов теплового насоса?')\">Стоп</button></div>\n<hr>\n<div id=\"t\"></div>\n</section>\n\n<section>\n<h2>Графики из"
" памяти</h2>\n<div class=\"r\"><select id=\"c0\" onchange=\"chart(0)\"></select><b id=\"v0\"></b></div>\n<div id=\"g0\"></div>\n<div class=\"r\"><select id=\"c1\" onchange=\"chart(1)\"></select><b id=\"v1\"></b></div>\n<di"
"v id=\"g1\"></div>\n<div class=\"r\"><button onclick=\"chart(0);chart(1)\">Обновить</button>\n<a class=\"b\" href=\"/chart.csv\" download>Скачать все, csv</a></div>\n</section>\n\n<section>\n<h2>Контроллер</h2>\n<div "
"class=\"r\"><i>Версия ПО</i><b data-k=\"ver.fw\">-</b></div>\n<div class=\"r\"><i>Конфигурация</i><b data-k=\"ver.config\">-</b></div>\n<div class=\"r\"><i>Свободно ОЗУ</i><b data-k=\"sys.free_ram\">-</b></div>\n<di"
"v class=\"r\"><i>Загрузка ЦП</i><b data-k=\"state.cpu\">-</b></div>\n<div class=\"r\"><i>Причина перезагрузки</i><b data-k=\"state.reset_cause\">-</b></div>\n<div class=\"r\"><i>Источник морды</i><b data-k=\"ver.w"
"eb_src\">-</b></div>\n<div class=\"r\"><i>Режим safeNetwork</i><b id=\"safe\">-</b></div>\n<hr>\n<div class=\"r\"><button class=\"d\" onclick=\"act('reset.controller','Перезагрузить контроллер?')\">Сброс контроллер"
"а</button></div>\n<div class=\"note\">Перед сбросом контроллер штатно останавливает ТН: гасит компрессор,\nдаёт насосам доработать и сохраняет счётчики и статистику. При выключенном ТН это\nоколо десяти се"
"кунд, при работающем - до минуты.</div>\n</section>\n\n<section>\n<h2>Журнал и состояние</h2>\n<div class=\"r\"><i>Системный журнал</i><a class=\"b\" href=\"/journal.txt\" download>Скачать</a></div>\n<div class=\""
"r\"><i>Текущее состояние</i><a class=\"b\" href=\"/state.txt\" download>Скачать</a></div>\n<div class=\"r\"><i>Настройки, текстом</i><a class=\"b\" href=\"/settings.txt\" download>Скачать</a></div>\n<hr>\n<div clas"
"s=\"note\">Журнал - первое, что нужно сохранить до любых действий: в нём\nнаписано, почему контроллер оказался в аварийном режиме.</div>\n</section>\n\n<section>\n<h2>Настройки</h2>\n<div class=\"r\"><i>Копия н"
"астроек, двоичная</i><a class=\"b\" href=\"/settings.bin\" download>Скачать</a></div>\n<div class=\"r\"><i>Вся память I2C</i><a class=\"b\" href=\"/settings_eeprom.bin\" download>Скачать</a></div>\n<hr>\n<div clas"
"s=\"r\"><i>Восстановить из файла</i><input type=\"file\" id=\"fs\"></div>\n<div class=\"r\"><button class=\"d\" onclick=\"restore()\">Восстановить настройки</button></div>\n<hr>\n<div class=\"r\"><button onclick=\"save"
"()\">Записать настройки в память</button></div>\n<div class=\"note\">Восстановление из файла пишет настройки, профили и расписание.\nПосле него сделайте сброс контроллера.</div>\n</section>\n\n<section>\n<h2>С"
"еть</h2>\n<div class=\"r\"><i>DHCP</i><b><input type=\"checkbox\" data-k=\"net.dhcp\"></b></div>\n<div class=\"r\"><i>Адрес</i><b><input type=\"text\" data-k=\"net.ip\"></b></div>\n<div class=\"r\"><i>Маска</i><b><inp"
"ut type=\"text\" data-k=\"net.subnet\"></b></div>\n<div class=\"r\"><i>Шлюз</i><b><input type=\"text\" data-k=\"net.gateway\"></b></div>\n<div class=\"r\"><i>DNS</i><b><input type=\"text\" data-k=\"net.dns\"></b></div>"
"\n<div class=\"r\"><i>Порт</i><b><input type=\"text\" data-k=\"net.port\" style=\"width:70px\"></b></div>\n<div class=\"r\"><i>MAC</i><b data-k=\"net.mac\">-</b></div>\n<div class=\"r\"><button onclick=\"netsave()\">При"
"менить</button>\n<button onclick=\"save()\">Записать в память</button></div>\n<hr>\n<div class=\"r\"><i>Проверка паролей</i><b><input type=\"checkbox\" data-k=\"net.pass\"></b></div>\n<div class=\"r\"><i>Пароль пол"
"ьзователя</i><b><input type=\"text\" data-k=\"net.passuser\" autocomplete=\"off\"></b></div>\n<div class=\"r\"><i>Пароль администратора</i><b><input type=\"text\" data-k=\"net.passadmin\" autocomplete=\"off\"></b></"
"div>\n<div class=\"r\"><button class=\"d\" onclick=\"nopass()\">Отключить пароли</button></div>\n<div class=\"note\">Новый адрес и пароли заработают после записи в память и сброса контроллера.\nПароли показаны о"
"ткрыто: в этом режиме страница нужна, чтобы вернуть доступ, если пароль забыт.</div>\n</section>\n\n<section>\n<h2>Веб-интерфейс</h2>\n<div class=\"r\"><i>Грузить морду с</i><b><select id=\"ws\" onchange=\"setw"
"s(this.value)\">\n<option value=\"1\">SPI Flash</option><option value=\"0\">SD карты</option></select></b></div>\n<div class=\"note\">Выбор действует до перезагрузки. Чтобы он сохранился, нажмите\n\"Записать нас"
"тройки в память\" в карточке \"Настройки\".</div>\n<hr>\n<div class=\"r\"><i>Файлы морды</i><input type=\"file\" id=\"fw\" multiple></div>\n<div class=\"r\"><button class=\"d\" onclick=\"upweb('SPI_FLASH')\">Записать в"
" SPI Flash</button></div>\n<div class=\"r\"><button onclick=\"upweb('SD_CARD')\">Записать на SD карту</button></div>\n<div class=\"note\">Запись в SPI Flash стирает весь диск целиком - выбирайте сразу\nвсе фай"
"лы морды. На SD карту файлы пишутся поверх существующих.<br>\nНе закрывайте страницу до сообщения об окончании.</div>\n</section>\n\n</div>\n<script>\nvar urlupdate=5000;\n/*API-CORE*/\n// -------------------"
"--------------------------------------------------------------------------\n// Клиент нового API контроллера (/api/...). Протокол - tools/api/spec.md.\n// Блок самодостаточен: tools/mk_min_web.py берёт "
"его между маркерами API-CORE и вставляет в\n// аварийную страницу (вариант C). Зависит только от urlcontrol, urlupdate, NeedLogin/LPString\n// (есть ниже в scripts.js, в аварийной странице их нет - там "
"и авторизации нет) и необязательных\n// shLinkState(ok), shLogin(). Значения в страницу вставлять через textContent / value.\n// -------------------------------------------------------------------------"
"--------------------\nvar API_PROTO = 1;                   // версия протокола, с которой работает эта морда\nvar api = {\n\ttimeout: 3500,                     // не короче серверного ожидания мьютекса (3"
"000 мс), иначе повторы на занятый контроллер\n\tver: null,                         // ответ /api/ver после api.init\n\tmeta: {},                          // группа -> описание полей\n\tpollers: [], pollTime"
"r: null, pollBusy: 0, pollAgain: 0\n};\n\n// Один запрос. cb(json, err): err = {code, net}, json - разобранное тело. Ошибки по ключам внутри json.e.\napi.call = function(verb, query, cb, tmo) {\n\tvar r = n"
"ew XMLHttpRequest(), auth = (typeof NeedLogin != 'undefined' && NeedLogin == 2) ? '&&' + LPString : '';\n\tr.open('GET', (typeof urlcontrol == 'string' ? urlcontrol : '') + '/api/' + verb + (query ? '?'"
" + query : '') + auth, true);\n\tr.timeout = tmo || api.timeout;\n\tr.onload = function() {\n\t\tvar j = null;\n\t\tif(window.shLinkState) shLinkState(true);\n\t\tif(r.status == 401) { if(window.shLogin) shLogin()"
"; cb(null, {code: 401}); return; }\n\t\ttry { j = JSON.parse(r.responseText); } catch(e) {}\n\t\tif(r.status != 200 || j === null) cb(null, {code: r.status});\n\t\telse cb(j, null);\n\t};\n\tr.onerror = r.ontimeou"
"t = function() { if(window.shLinkState) shLinkState(false); cb(null, {code: 0, net: true}); };\n\tr.send(null);\n};\n\n// Значение в строку запроса: булево - 1/0, пустое - пустая строка\napi.enc = function("
"v) { return encodeURIComponent(v === true ? 1 : v === false ? 0 : v == null ? '' : v); };\n\n// Чтение: o.k - массив ключей, o.g - группа (необязательно), o.a - атрибуты группы устройств.\n// Длинный спи"
"сок режется на запросы по 40 ключей / 1200 знаков. cb(d, e, err).\napi.get = function(o, cb) {\n\tvar parts = [], cur = [], len = 0, D = {}, E = {}, fail = null, left;\n\t(o.k || []).forEach(function(k) {\n"
"\t\tif(cur.length && (cur.length >= 40 || len + k.length > 1200)) { parts.push(cur); cur = []; len = 0; }\n\t\tcur.push(k); len += k.length + 1;\n\t});\n\tif(cur.length) parts.push(cur);\n\tif(o.g && !parts.leng"
"th) parts.push([]);\n\tif(!(left = parts.length)) { cb(D, E, null); return; }\n\tparts.forEach(function(ks, i) {\n\t\tvar q = (ks.length ? 'k=' + ks.join(',') : '') + (o.g && i == 0 ? (ks.length ? '&' : '') "
"+ 'g=' + o.g + (o.a ? '&a=' + o.a : '') : '');\n\t\tapi.call('get', q, function(j, err) {\n\t\t\tif(err) fail = err;\n\t\t\telse { var x; for(x in j.d) D[x] = j.d[x]; for(x in j.e) E[x] = j.e[x]; }\n\t\t\tif(--left "
"== 0) cb(D, E, fail);\n\t\t});\n\t});\n};\n\n// Запись: pairs - {ключ: значение}, olds - {ключ: ожидаемое прежнее значение} (E3: при расхождении ключ даст conflict).\n// Пачки по 20 пар / 1200 знаков уходят по"
" очереди. cb(d, e, err): d - значения, прочитанные ПОСЛЕ записи.\napi.set = function(pairs, cb, olds) {\n\tvar groups = [], cur = [], len = 0, D = {}, E = {}, fail = null;\n\tObject.keys(pairs).forEach(fun"
"ction(k) {\n\t\tvar s = k + '=' + api.enc(pairs[k]) + (olds && olds[k] !== undefined ? '&if.' + k + '=' + api.enc(olds[k]) : '');\n\t\tif(cur.length && (cur.length >= 20 || len + s.length > 1200)) { groups."
"push(cur); cur = []; len = 0; }\n\t\tcur.push(s); len += s.length + 1;\n\t});\n\tif(cur.length) groups.push(cur);\n\t(function next() {\n\t\tvar g = groups.shift();\n\t\tif(!g) { cb(D, E, fail); return; }\n\t\tapi.call"
"('set', g.join('&'), function(j, err) {\n\t\t\tif(err) { fail = err; Object.keys(pairs).forEach(function(k) { if(!(k in D) && !(k in E)) E[k] = 'net'; }); groups = []; }\n\t\t\telse { var x; for(x in j.d) D[x"
"] = j.d[x]; for(x in j.e) E[x] = j.e[x]; }\n\t\t\tnext();\n\t\t});\n\t})();\n};\n\n// Действие: cb(результат, код ошибки или '', err)\napi['do'] = function(action, cb, p, tmo) {\t\t// tmo - свой тайм-аут для долгих "
"действий (скан шины OneWire до 9 с)\n\tapi.call('do', 'a=' + action + (p !== undefined ? '&p=' + api.enc(p) : ''), function(j, err) {\n\t\tif(err) { cb(null, 'net', err); return; }\n\t\tcb(j.d[action], j.e[ac"
"tion] || '', null);\n\t}, tmo);\n};\n\n// Описание полей группы: api.meta[g] = {sch, m: {ключ: {t, dec, min, max, o, na, w}}, devs}\napi.metaOf = function(g, cb) {\n\tif(api.meta[g]) { cb(api.meta[g]); return"
"; }\n\tvar ck = null;\n\ttry { if(api.ver) ck = sessionStorage.getItem('am.' + g + '.' + api.ver.sch); } catch(e) {}\n\tif(ck) { try { api.meta[g] = JSON.parse(ck); cb(api.meta[g]); return; } catch(e) {} }\n"
"\tapi.call('meta', 'g=' + g, function(j, err) {\n\t\tif(j) {\n\t\t\tapi.meta[g] = j;\n\t\t\ttry { if(api.ver) sessionStorage.setItem('am.' + g + '.' + api.ver.sch, JSON.stringify(j)); } catch(e) {}\n\t\t}\n\t\tcb(j);\n\t"
"});\n};\n\n// Проверка версии протокола и состава сборки. cb(ver). При несовпадении версии - сообщение вместо тихих сбоев.\napi.init = function(cb) {\n\tif(api.ver) { cb(api.ver); return; }\n\tapi.call('ver',"
" '', function(j, err) {\n\t\tif(!j) { cb(null); return; }\n\t\tapi.ver = j;\n\t\tif(j.proto != API_PROTO) {\n\t\t\tvar m = 'Версия протокола контроллера (' + j.proto + ') не совпадает с версией морды (' + API_PROT"
"O + '): ' +\n\t\t\t\t(j.proto > API_PROTO ? 'обновите файлы веб-морды.' : 'обновите прошивку контроллера.');\n\t\t\tif(window.stToast) stToast(m, true); else alert(m);\n\t\t}\n\t\tcb(j);\n\t});\n};\n\n// Опрос: единый та"
"ймер на страницу. spec.k - ключи, cb(d, e) получает значения своих ключей.\n// Один запрос за тик (ключи всех подписчиков), скрытая вкладка не опрашивает и обновляется сразу при возвращении.\napi.poll ="
" function(spec, cb) {\n\tapi.pollers.push({k: spec.k || [], cb: cb});\n\tif(api.pollBusy) api.pollAgain = 1; else api.pollTick();\n};\napi.pollTick = function() {\n\tclearTimeout(api.pollTimer);\n\tapi.pollTime"
"r = setTimeout(api.pollTick, urlupdate);\n\tif(document.hidden || api.pollBusy || !api.pollers.length) return;\n\tvar seen = {}, keys = [];\n\tapi.pollers.forEach(function(p) { p.k.forEach(function(k) { if("
"!seen[k]) { seen[k] = 1; keys.push(k); } }); });\n\tapi.pollBusy = 1;\n\tapi.get({k: keys}, function(D, E, err) {\n\t\tapi.pollBusy = 0;\n\t\tif(!err) api.pollers.forEach(function(p) {\n\t\t\tvar d = {}, e = {};\n\t\t"
"\tp.k.forEach(function(k) { if(k in D) d[k] = D[k]; if(k in E) e[k] = E[k]; });\n\t\t\tp.cb(d, e);\n\t\t});\n\t\tif(api.pollAgain) { api.pollAgain = 0; api.pollTick(); }\n\t});\n};\ndocument.addEventListener('visibi"
"litychange', function() { if(!document.hidden && api.pollers.length) api.pollTick(); });\n/*END API-CORE*/\n</script>\n<script>\nvar E=function(i){return document.getElementById(i)},T=60,CH=[0,0],TS=[];\nv"
"ar ERR={conflict:\"значение изменилось\",range:\"вне допустимых границ\",bad_value:\"неверное значение\",forbidden:\"нет прав\",\nbusy:\"недоступно: ТН работает\",na:\"недоступно в этой сборке\",unknown:\"неизвестн"
"ый параметр\",too_big:\"слишком длинное значение\",\nfail:\"контроллер не смог выполнить\",net:\"нет связи\"};\nvar FMT={\"state.work\":function(v){return v?\"включён\":\"остановлен\"}};\n\n// Сообщения - только текст"
"ом, строки через перевод строки (#m{white-space:pre-line})\nfunction say(s,bad){var e=E(\"m\");e.textContent=s;e.style.borderLeftColor=bad?\"#a00\":\"#57819e\";e.style.display=\"block\"}\nfunction lost(){say(\"К"
"онтроллер не отвечает\",1)}\n\n// Значения в элементы с data-k: поля ввода получают value, остальные textContent; null и ошибка - прочерк\nfunction fill(d,e){var k,a,i,v,f;\nfor(k in d){v=d[k];a=document.q"
"uerySelectorAll('[data-k=\"'+k+'\"]');\nfor(i=0;i<a.length;i++){if(a[i].tagName==\"INPUT\"){if(a[i].type==\"checkbox\")a[i].defaultChecked=a[i].checked=v===true;else a[i].defaultValue=a[i].value=v===null?\"\":"
"v}\nelse a[i].textContent=v===null?\"-\":FMT[k]?FMT[k](v):v}}\nfor(k in e||{}){a=document.querySelectorAll('[data-k=\"'+k+'\"]');for(i=0;i<a.length;i++)if(a[i].tagName!=\"INPUT\")a[i].textContent=\"-\"}}\n\n// Де"
"йствие: подтверждение, затем api.do; cb(результат) после успеха\nfunction act(a,ask,cb){if(ask&&!confirm(ask))return;\napi[\"do\"](a,function(v,e,err){if(err||e)say(\"Не выполнено: \"+(ERR[e]||e||\"нет связи"
"\"),1);else{say(\"Выполнено\");if(cb)cb(v)}})}\n\n// Запись настроек в память: save.all отдаёт число записанных байт, при отказе - ошибку\nfunction save(){if(!confirm(\"Записать текущие настройки в память ко"
"нтроллера?\"))return;\napi[\"do\"](\"save.all\",function(v,e,err){say(!err&&!e&&v>=0?\"Настройки записаны в память\":\"Ошибка записи настроек\"+(e?\": \"+(ERR[e]||e):\"\"),err||e||!(v>=0))})}\n\n// Датчики: набор зав"
"исит от конфигурации, поэтому список и подписи берём один раз (g=temp, атрибуты value и note)\nfunction sensors(){api.get({g:\"temp\",a:\"value,note\"},function(d,e,err){var k,m,h=\"\";\nif(err)return;\nfor(k "
"in d)if((m=/^temp\\.(.+)\\.value$/.exec(k))&&TS.length<12)TS.push(m[1]);\nfor(k=0;k<TS.length;k++){h+='<div class=\"r\"><i></i><b data-k=\"temp.'+TS[k]+'.value\">-</b></div>'}\nE(\"t\").innerHTML=h;\nvar r=E(\"t\""
").children;\nfor(k=0;k<TS.length;k++)r[k].firstChild.textContent=d[\"temp.\"+TS[k]+\".note\"]||TS[k];\nfill(d,e);poll()})}\n\nfunction poll(){var c=[\"state.mode\",\"state.work\",\"state.errcode\",\"state.error\",\"st"
"ate.uptime\",\"state.cpu\",\"sys.free_ram\"],i;\nfor(i=0;i<TS.length;i++)c.push(\"temp.\"+TS[i]+\".value\");\napi.poll({k:c},fill)}\n\n// Графики: ключ chart.list (названия), точки - /api/chart?id=<номер с 0>. Рис"
"уем своим svg.\nfunction charts(){api.get({k:[\"chart.list\",\"ohp.time_chart\"]},function(d,e,err){\nif(err)return;T=parseInt(d[\"ohp.time_chart\"])||60;\nvar l=d[\"chart.list\"]||[],h='<option value=\"\">---</op"
"tion>',i;\nfor(i=0;i<l.length;i++){var o=document.createElement(\"option\");o.value=i;o.textContent=l[i];h+=o.outerHTML}\nE(\"c0\").innerHTML=h;E(\"c1\").innerHTML=h;\nif(l.length>1){E(\"c0\").value=0;E(\"c1\").va"
"lue=1}\nchart(0);chart(1)})}\n\nfunction chart(k){var n=E(\"c\"+k).value;CH[k]=n;\nif(n===\"\"){E(\"g\"+k).innerHTML=\"\";E(\"v\"+k).textContent=\"\";return}\napi.call(\"chart\",\"id=\"+n,function(j,err){var v=[],a=(j&&j."
"d)||[],i;\nfor(i=0;i<a.length;i++)if(typeof a[i]==\"number\")v.push(a[i]);\nE(\"v\"+k).textContent=v.length?v[v.length-1]:\"\";E(\"g\"+k).innerHTML=draw(v)})}\n\nfunction draw(v){var W=320,H=100,L=30,B=14,n=v.len"
"gth,i,mn,mx,p=\"\";\nif(!n)return '<svg viewBox=\"0 0 320 40\"><text x=\"4\" y=\"24\" font-size=\"12\" fill=\"#666\">нет данных</text></svg>';\nmn=mx=v[0];for(i=1;i<n;i++){if(v[i]<mn)mn=v[i];if(v[i]>mx)mx=v[i]}\nif("
"mx-mn<.01){mx+=.5;mn-=.5}\nfor(i=0;i<n;i++)p+=(L+(W-L-4)*(n<2?0:i/(n-1))).toFixed(1)+\",\"+\n((H-B)-(H-B-6)*(v[i]-mn)/(mx-mn)).toFixed(1)+\" \";\nvar g=\"\",y,j;\nfor(j=0;j<3;j++){y=6+(H-B-6)*j/2;g+='<line x1=\""
"'+L+'\" y1=\"'+y+'\" x2=\"'+(W-4)+'\" y2=\"'+y+\n'\" stroke=\"#000\" stroke-opacity=\".12\"/><text x=\"'+(L-3)+'\" y=\"'+(y+4)+\n'\" font-size=\"9\" fill=\"#555\" text-anchor=\"end\">'+(mx-(mx-mn)*j/2).toFixed(1)+\"</text>\"}"
"\nreturn '<svg viewBox=\"0 0 '+W+\" \"+(H+3)+'\">'+g+'<polyline fill=\"none\" stroke=\"#1b5e8c\" stroke-width=\"1.6\" points=\"'+p+\n'\"/><text x=\"'+L+'\" y=\"'+H+'\" font-size=\"9\" fill=\"#555\">-'+Math.round((n-1)*T/60"
")+\n' мин</text><text x=\"'+(W-4)+'\" y=\"'+H+'\" font-size=\"9\" fill=\"#555\" text-anchor=\"end\">сейчас</text></svg>'}\n\n// Сеть и пароли: поля с data-k=\"net.*\". Границы и длины - из meta группы net\nfunction n"
"etload(){var els=document.querySelectorAll(\"input[data-k^='net.']\"),ks=[\"net.mac\"],i;\nfor(i=0;i<els.length;i++)ks.push(els[i].getAttribute(\"data-k\"));\napi.get({k:ks},function(d,e,err){if(err){lost();r"
"eturn}fill(d,e)});\napi.get({k:[\"ohp.wspif\"]},function(d){if(d[\"ohp.wspif\"]!=null)E(\"ws\").value=d[\"ohp.wspif\"]?1:0});\napi.metaOf(\"net\",function(m){if(!m)return;\nfor(i=0;i<els.length;i++){var f=m.m[els["
"i].getAttribute(\"data-k\")];if(!f)continue;\nif(f.t==\"str\"&&f.max)els[i].maxLength=f.max}})}\n\n// Отправляются только изменённые поля (defaultValue хранит прочитанное значение); ответ - по ключам\nfunctio"
"n netsave(){var els=document.querySelectorAll(\"input[data-k^='net.']\"),p={},n=0,i,el,v;\nfor(i=0;i<els.length;i++){el=els[i];v=el.type==\"checkbox\"?el.checked:el.value;\nif(el.type==\"checkbox\"?(v===el.de"
"faultChecked):(v===el.defaultValue))continue;p[el.getAttribute(\"data-k\")]=v;n++}\nif(!n){say(\"Изменений нет\");return}\nif(!confirm(\"Применить сетевые настройки?\"))return;\napi.set(p,function(d,e,err){var"
" s=[],k;\nif(err){lost();return}\nfor(k in p)s.push(k.substr(4)+(e[k]?\": \"+(ERR[e[k]]||e[k]):\" = \"+d[k]));\nsay(s.join(\"\\n\")+\"\\nЧтобы настройки сохранились, нажмите \\\"Записать в память\\\", затем сброс кон"
"троллера.\",Object.keys(e).length);\nnetload()})}\n\nfunction nopass(){if(!confirm(\"Отключить проверку паролей?\"))return;\napi.set({\"net.pass\":0},function(d,e,err){say(err||e[\"net.pass\"]?\"Не выполнено\":\"Пр"
"оверка паролей отключена\",err||e[\"net.pass\"]);netload()})}\n\nfunction setws(v){api.set({\"ohp.wspif\":v},function(d,e,err){say(err||e[\"ohp.wspif\"]?\"Не выполнено\":\"Источник морды выбран\",err||e[\"ohp.wspif"
"\"])})}\n\n// Загрузка файлов. Title: имя файла или метка начала/конца записи морды. Вне API, формат прежний.\nfunction post(ttl,body,ok,bad){var x=new XMLHttpRequest();x.open(\"POST\",\"/\",true);\nx.setReque"
"stHeader(\"Title\",ttl);\nx.onload=function(){if(x.responseText){say(x.responseText,1);if(bad)bad()}else ok()};\nx.onerror=function(){say(\"Обрыв связи при записи\",1);if(bad)bad()};x.send(body)}\n\nfunction "
"restore(){var f=E(\"fs\").files;if(!f.length){say(\"Файл не выбран\",1);return}\nif(!confirm(\"Записать настройки из файла в память контроллера?\"))return;\nsay(\"Запись настроек . . .\");\npost(\"*SETTINGS*\",f[0"
"],function(){say(\"Настройки записаны. Сделайте сброс контроллера.\")})}\n\nfunction upweb(dst){var f=E(\"fw\").files;if(!f.length){say(\"Файлы не выбраны\",1);return}\nif(!confirm(\"Записать \"+f.length+\" файл("
"ов)? \"+(dst==\"SPI_FLASH\"?\"Диск SPI Flash будет стёрт целиком.\":\"Файлы на SD будут перезаписаны.\")))return;\nsay(\"Подготовка . . .\");\npost(\"*\"+dst+\"*\",\"\",function(){one(f,0,dst)})}\n\nfunction one(f,i,dst"
"){if(i>=f.length){post(\"*\"+dst+\"_END*\",\"\",function(){\nsay(\"Готово, записано файлов: \"+f.length+\". Сделайте сброс контроллера.\")});return}\nsay(\"Запись \"+(i+1)+\" из \"+f.length+\": \"+f[i].name);\npost(enco"
"deURIComponent(f[i].name),f[i],function(){one(f,i+1,dst)})}\n\n// Версия протокола и режим safeNetwork приходят в /api/ver; остальное разовое - ключами ver.*, state.reset_cause\napi.init(function(v){\nif("
"!v){lost();return}\nE(\"safe\").textContent=v.safe_net?\"да\":\"нет\";\napi.get({k:[\"ver.fw\",\"ver.config\",\"ver.web_src\",\"state.reset_cause\"]},fill);\nsensors();charts();netload()});\n</script>\n</body>\n</html>\n"
;

#endif
