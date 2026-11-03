#pragma once
#include <string>
#include <sys/types.h>

// Работа с pid-файлом и проверка процессов через /proc.
class PidManager {
public:
    explicit PidManager(std::string pidFile);

    // Путь по умолчанию: $DIRCLEAN_PID_FILE или /tmp/dirclean_daemon.pid
    static std::string defaultPath();

    const std::string& path() const { return pidFile_; }

    bool readPid(pid_t& pid) const;
    bool writePid(pid_t pid) const;
    // Удаляет pid-файл, только если в нём записан pid (чтобы не стереть файл нового экземпляра).
    void removeIfOwnedBy(pid_t pid) const;

    // Существует ли процесс (по /proc/<pid>; зомби считается завершённым).
    static bool processExists(pid_t pid);

    // Если в pid-файле указан живой процесс — посылает ему SIGTERM и ждёт завершения.
    // true — старого экземпляра нет (или он остановлен); false — остановить не удалось.
    bool stopOldInstance(int timeoutSeconds = 5) const;

private:
    std::string pidFile_;
};
