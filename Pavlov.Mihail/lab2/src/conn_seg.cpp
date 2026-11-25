#include "connection.hpp"

#include <cerrno>
#include <cstring>
#include <system_error>
#include <sys/ipc.h>
#include <sys/shm.h>

namespace wolf {
    namespace {

        class SegConnection final : public Connection {
        public:
            SegConnection()
            {
                shmid_ = shmget(IPC_PRIVATE, 2 * Connection::capacity, IPC_CREAT | 0600);
                if (shmid_ == -1) {
                    throw std::system_error(errno, std::generic_category(),
                        "Не удалось создать сегмент shmget");
                }
                owner_ = true;
            }

            ~SegConnection() override
            {
                if (memory_ != nullptr) {
                    shmdt(memory_);
                }
                if (owner_ && shmid_ != -1) {
                    shmctl(shmid_, IPC_RMID, nullptr);
                }
            }

        private:
            void openTransport(Side side) override
            {
                void* address = shmat(shmid_, nullptr, 0);
                if (address == reinterpret_cast<void*>(-1)) {
                    throw std::system_error(errno, std::generic_category(),
                        "Не удалось присоединить сегмент shmget");
                }

                memory_ = static_cast<char*>(address);
                auto* first = memory_;
                auto* second = memory_ + Connection::capacity;

                incoming_ = side == Side::host ? second : first;
                outgoing_ = side == Side::host ? first : second;
            }

            void receive(void* data, std::size_t size) override
            {
                std::memcpy(data, incoming_, size);
            }

            void send(const void* data, std::size_t size) override
            {
                std::memcpy(outgoing_, data, size);
            }

            int shmid_ = -1;
            char* memory_ = nullptr;
            char* incoming_ = nullptr;
            char* outgoing_ = nullptr;
            bool owner_ = false;
        };

    }

    std::unique_ptr<Connection> makeConnection()
    {
        return std::make_unique<SegConnection>();
    }

}