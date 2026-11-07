// Вспомогательные функции для чтения/записи в файловые дескрипторы с тайм-аутом.
#pragma once

#include <cstddef>
#include <string>

// Тайм-аут всех ожиданий (семафоры, чтение, запись). По умолчанию 5000 мс.
// Только для автотестов его можно уменьшить: SetIoTimeoutMs() или переменная окружения WOLF_IO_TIMEOUT_MS.
int IoTimeoutMs();
void SetIoTimeoutMs(int ms);

// RAII-обёртка над дескриптором.
class UniqueFd {
public:
    UniqueFd() = default;
    explicit UniqueFd(int fd) : fd_(fd) {}
    ~UniqueFd() { reset(); }
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;
    UniqueFd(UniqueFd&& o) noexcept : fd_(o.release()) {}
    UniqueFd& operator=(UniqueFd&& o) noexcept {
        if (this != &o) reset(o.release());
        return *this;
    }

    int get() const { return fd_; }
    bool valid() const { return fd_ >= 0; }
    int release() {
        int f = fd_;
        fd_ = -1;
        return f;
    }
    void reset(int fd = -1);

private:
    int fd_ = -1;
};

// Читает ровно count байт, ожидая данные не дольше timeoutMs. false - тайм-аут/закрытие/ошибка (err).
bool ReadFdExact(int fd, void* buf, size_t count, int timeoutMs, std::string& err);

// Пишет ровно count байт. Для сокетов isSocket = true (используется MSG_NOSIGNAL).
bool WriteFdAll(int fd, const void* buf, size_t count, int timeoutMs, bool isSocket, std::string& err);
