// Единый интерфейс всех типов межпроцессного взаимодействия.
// Каждый тип (pipe, fifo, sock, ...) реализован в своём файле conn_<тип>.cpp и определяет класс Conn
// с ОДИНАКОВЫМ интерфейсом - поэтому host.cpp и client.cpp собираются с любой реализацией.
#pragma once

#include <sys/types.h>

#include <cstddef>
#include <memory>
#include <string>

// Идентификатор соединения: номер клиента в сессии (выдаёт хост при знакомстве) и pid хоста.
struct ConnId {
    int id;
    pid_t hostPid;
};

class Conn {
public:
    // create == true  - сторона хоста: создаёт все ресурсы (семафоры, каналы, сокет...);
    // create == false - сторона клиента: открывает уже созданные хостом ресурсы.
    // При ошибке бросает std::runtime_error с описанием.
    Conn(const ConnId& id, bool create);
    ~Conn();

    Conn(const Conn&) = delete;
    Conn& operator=(const Conn&) = delete;

    // Читает ровно count байт. Ожидание ограничено тайм-аутом (5 c, см. IoTimeoutMs()).
    // Возвращает false при тайм-ауте, закрытии соединения или ошибке (описание - LastError()).
    bool Read(void* buf, size_t count);

    // Записывает ровно count байт и сообщает партнёру о данных через глобальный семафор.
    bool Write(const void* buf, size_t count);

    const std::string& LastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
