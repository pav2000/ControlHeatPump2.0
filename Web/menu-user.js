// Меню роли user: прошивка отдаёт этот файл вместо menu.js (WebServer.cpp, HTTP_GET).
// Какие страницы доступны, определяет SHELL_USER_PAGES в shell.js.
var SHELL_ROLE = 'user';
document.write('<script src="shell.js"><\/script>');
