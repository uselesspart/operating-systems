#pragma once
// Простой потокобезопасный лог: stderr + файл.
#include <ctime>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>

namespace chatlog {

inline std::mutex& mtx() {
    static std::mutex m;
    return m;
}
inline std::ofstream& file() {
    static std::ofstream f;
    return f;
}
inline void init(const std::string& path) {
    std::lock_guard<std::mutex> l(mtx());
    file().open(path, std::ios::app);
}
inline void line(const std::string& tag, const std::string& text) {
    time_t t = time(nullptr);
    tm tmv;
    localtime_r(&t, &tmv);
    char b[16];
    strftime(b, sizeof b, "%H:%M:%S", &tmv);
    std::lock_guard<std::mutex> l(mtx());
    std::string s = std::string("[") + b + "] " + tag + ": " + text;
    std::cerr << s << std::endl;
    if (file().is_open()) file() << s << std::endl;
}

}  // namespace chatlog
