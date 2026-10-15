#pragma once

#include <sys/types.h>

class PidFile
{
private:

   static constexpr const char* PATH = "/tmp/mydaemon.pid";

   int fd_ = -1;
   pid_t readPid() const;

   bool processExists(pid_t pid) const;
   void terminatePreviousInstance();

public:
    ~PidFile() noexcept;
    void acquireLock();
    void writeCurrentPid();
    void release() noexcept;
};

