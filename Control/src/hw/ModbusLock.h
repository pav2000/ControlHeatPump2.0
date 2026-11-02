// Решение о замке шины Modbus: чистая функция без железа, проверяется на ПК (tests/test_modbuslock.cpp).
// Замок создаётся в setup() после HP.initHeatPump(), а initFC/initSDM ходят по шине раньше (doc/Modbus-fix.txt).
// До старта планировщика задач нет и SemaphoreTake замок не трогает, поэтому пустой замок там допустим;
// после старта пустой замок - assert ядра FreeRTOS, обмен отвергается (ERR_NO_MODBUS).
#ifndef ModbusLock_h
#define ModbusLock_h

inline bool Modbus_LockRefused(bool haveLock, bool schedulerStarted)
{
	return !haveLock && schedulerStarted;
}

#endif
