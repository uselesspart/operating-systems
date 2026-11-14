#pragma once
// Протокол чата: фиксированное сообщение, одинаковое для всех типов связи.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <string>

enum MsgType : int32_t {
    MT_HELLO = 1,  // клиент -> хост: «я подключился»
    MT_CHAT  = 2,  // сообщение чата (общее или личное)
    MT_USERS = 3,  // хост -> клиент: список участников "0,1,3"
    MT_SYS   = 4   // хост -> клиент: статусное сообщение
};

constexpr int32_t ID_ALL  = -1;  // адресат «всем»
constexpr int32_t ID_HOST = 0;   // идентификатор хоста

struct Msg {
    int32_t type;
    int32_t from;
    int32_t to;
    int32_t pad;
    int64_t ts;        // время (мс, wall clock), проставляется хостом
    char    text[232];
};
static_assert(sizeof(Msg) == 256, "unexpected Msg size");

inline std::string who(int id) {
    if (id == ID_ALL) return "Всем";
    if (id == ID_HOST) return "Хост";
    return "Клиент " + std::to_string(id);
}

inline int64_t wall_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

inline void set_text(Msg& m, const std::string& s) {
    std::memset(m.text, 0, sizeof m.text);
    size_t n = std::min(s.size(), sizeof(m.text) - 1);
    // не режем многобайтный символ UTF-8 посередине
    while (n > 0 && n < s.size() && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
    std::memcpy(m.text, s.data(), n);
}

inline std::string format_line(const Msg& m) {
    time_t t = static_cast<time_t>(m.ts / 1000);
    tm tmv;
    localtime_r(&t, &tmv);
    char b[16];
    strftime(b, sizeof b, "%H:%M:%S", &tmv);
    std::string s = std::string("[") + b + "] " + who(m.from);
    if (m.to != ID_ALL) s += " \xE2\x86\x92 " + who(m.to) + " (лично)";
    s += ": ";
    s += m.text;
    return s;
}
