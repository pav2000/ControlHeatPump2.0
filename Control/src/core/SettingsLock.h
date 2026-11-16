#ifndef SettingsLock_h
#define SettingsLock_h

#include <stdint.h>

// Сериализует операции над областями настроек между вебом и задачей ТН.
class SettingsLock {
public:
	// Пытается занять общий флаг flag на время жизни объекта.
	explicit SettingsLock(volatile uint8_t &flag) : flag(flag), owned(!__atomic_exchange_n(&flag, 1, __ATOMIC_SEQ_CST)) {}
	// Освобождает флаг, если объект успел его занять.
	~SettingsLock() { if(owned) __atomic_store_n(&flag, 0, __ATOMIC_SEQ_CST); }
	// Возвращает true, если флаг принадлежит этому объекту.
	explicit operator bool() const { return owned; }

	SettingsLock(const SettingsLock &) = delete;
	SettingsLock &operator=(const SettingsLock &) = delete;

private:
	volatile uint8_t &flag;
	const bool owned;
};

#endif
