#pragma once
// Общий интерфейс всех типов взаимодействия. Реализация класса Conn
// лежит в одном из файлов conn_<тип>.cpp (в исполняемый файл линкуется ровно один).
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

class Conn {
public:
    // create=true  — сторона хоста (создаёт ресурсы канала для клиента id)
    // create=false — сторона клиента (подключается по spec, полученной от хоста)
    Conn(int id, bool create, const std::string& spec = "");
    ~Conn();
    Conn(const Conn&) = delete;
    Conn& operator=(const Conn&) = delete;

    // Завершение установки связи. Вызывается в каждом процессе ПОСЛЕ fork.
    // Не дольше 5 секунд, иначе false.
    bool Open();

    // Читает ровно count байт. >0 — успех, 0 — тайм-аут (ничего не прочитано),
    // <0 — ошибка/обрыв связи. timeout_ms < 0 — ждать бесконечно.
    int Read(void* buf, size_t count, int timeout_ms = -1);

    // Пишет ровно count байт; по умолчанию тайм-аут 5 секунд.
    bool Write(const void* buf, size_t count, int timeout_ms = 5000);

    void Close();

    // Описание клиентской стороны канала (передаётся дочернему процессу).
    std::string Spec() const;
    // Дескрипторы, которые должен унаследовать ребёнок (остальные — CLOEXEC).
    std::vector<int> ChildFds() const;
    // Родитель после fork закрывает клиентские концы.
    void CloseChildEnds();

    static const char* TypeName();

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
