// Раскладка буферов передачи сокетов W5500 и порядок обхода веб-сокетов. Чистая логика, без железа: тест tests/test_netlayout.cpp.
//
// ЗАЧЕМ. Скорость одного потока по каналу с большим RTT равна размеру буфера передачи сокета, делённому на RTT (2 КБ при 35 мс - около 52 КБ/с, 4 КБ - около 100 КБ/с).
// Всего у W5500 16 КБ передачи на 8 сокетов, так что больший буфер одному сокету отнимается у других. Две раскладки:
//   NL_MODE_OLD   2,2,2,2,2,2,2,2 КБ - прежняя, 7 веб-сокетов (0..6) и служебный 7. По умолчанию.
//   NL_MODE_FAST0 4,2,2,2,2,2,0,2 КБ - сокет 0 с буфером 4 КБ, 6 веб-сокетов (0..5), сокет 6 не используется, служебный 7.
// Приём всегда 2 КБ на сокет, здесь не рассматривается. Служебный сокет (W5200_SOCK_SYS = 7: почта, SMS, MQTT) раскладкой не затрагивается.
//
// Обход (WebScan). Чип отдаёт входящее соединение первому свободному слушающему сокету, то есть сокету 0, если он свободен: выбирает железо, а не прошивка.
// Поэтому быстрый сокет надо обслуживать как можно раньше. Проход web_server: сокет 0 в начале прохода и после каждого обслуженного запроса, остальные веб-сокеты
// по кругу 1..n-1 с общим указателем (каждый проход продолжает с того места, где остановился прошлый). Сокет 0, только что обслуженный, второй раз подряд не
// предлагается, поэтому быстрый клиент не морит остальные: между двумя обслуживаниями сокета 0 есть хотя бы одно обслуживание другого.
// Защита от повторного включения макросом: библиотека и код Control могут подключать этот файл разными путями.
#ifndef NETLAYOUT_H_INCLUDED
#define NETLAYOUT_H_INCLUDED
#include <stdint.h>

#define NL_SOCKETS 8        // сокетов у W5500
#define NL_TOTAL_KB 16      // всего памяти передачи, КБ
#define NL_SOCK_SYS 7       // служебный сокет, его буфер не нулевой

enum { NL_MODE_OLD = 0, NL_MODE_FAST0 = 1 };

// Размер буфера передачи каждого сокета в КБ. Неизвестный режим даёт прежнюю раскладку.
static inline void NetLayout_Fill(uint8_t mode, uint8_t tx_kb[NL_SOCKETS])
{
	for(uint8_t i = 0; i < NL_SOCKETS; i++) tx_kb[i] = 2;
	if(mode == NL_MODE_FAST0) { tx_kb[0] = 4; tx_kb[6] = 0; }
}

// Допустимая раскладка: размеры из набора {0,1,2,4,8,16}, сумма не больше 16 КБ, служебный сокет не нулевой.
static inline bool NetLayout_Valid(const uint8_t tx_kb[NL_SOCKETS])
{
	uint16_t sum = 0;
	for(uint8_t i = 0; i < NL_SOCKETS; i++) {
		const uint8_t k = tx_kb[i];
		if(k != 0 && k != 1 && k != 2 && k != 4 && k != 8 && k != 16) return false;
		sum += k;
	}
	return sum <= NL_TOTAL_KB && tx_kb[NL_SOCK_SYS] != 0;
}

// Сколько веб-сокетов (0..n-1) в режиме.
static inline uint8_t NetLayout_WebCount(uint8_t mode)
{
	return mode == NL_MODE_FAST0 ? 6 : 7;
}

// Используется ли сокет: буфер передачи не нулевой.
static inline bool NetLayout_Usable(uint8_t mode, uint8_t sock)
{
	if(sock >= NL_SOCKETS) return false;
	uint8_t tx[NL_SOCKETS];
	NetLayout_Fill(mode, tx);
	return tx[sock] != 0;
}

// ---- Обход веб-сокетов одного прохода web_server ----
// rr - указатель круга, общий для потоков (меняется под замком сети), значения 1..n_web-1.
struct WebScan {
	uint8_t  n_web;        // число веб-сокетов
	uint8_t  left;         // сколько сокетов круга ещё не предложено в этом проходе
	bool     zero_due;     // сокет 0 пора предложить
	uint8_t *rr;
};

static inline void WebScan_Begin(WebScan *s, uint8_t n_web, uint8_t *rr)
{
	s->n_web = n_web ? n_web : 1;
	s->rr = rr;
	s->left = (uint8_t) (s->n_web - 1);
	s->zero_due = true;
	if(s->n_web > 1 && (*rr < 1 || *rr > s->n_web - 1)) *rr = 1;
}

// Следующий сокет для проверки, -1 - проход закончен. Занятые другим потоком и сокеты без данных пропускает вызывающий.
static inline int8_t WebScan_Next(WebScan *s)
{
	if(s->zero_due) { s->zero_due = false; return 0; }
	if(s->left == 0) return -1;
	const uint8_t sock = *s->rr;
	*s->rr = (uint8_t) (sock + 1 > s->n_web - 1 ? 1 : sock + 1);
	s->left--;
	return (int8_t) sock;
}

// Сокет sock обслужен: сокет 0 снова пора проверить, кроме случая, когда обслужен он сам (чередование, без голодания остальных).
static inline void WebScan_Served(WebScan *s, uint8_t sock)
{
	s->zero_due = sock != 0;
}

#endif // NETLAYOUT_H_INCLUDED
