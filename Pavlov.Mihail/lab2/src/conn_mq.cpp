#include "connection.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <mqueue.h>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unistd.h>

namespace wolf {
    namespace {

        class MqConnection final : public Connection {
        public:
            MqConnection()
                : toHostName_("/wolf_lab2_mq_" + std::to_string(getpid()) + "_to_host"),
                toChildName_("/wolf_lab2_mq_" + std::to_string(getpid()) + "_to_child")
            {
            }

            ~MqConnection() override
            {
                closeQueue(toHost_);
                closeQueue(toChild_);

                if (owner_) {
                    (void)mq_unlink(toHostName_.c_str());
                    (void)mq_unlink(toChildName_.c_str());
                }
            }

        private:
            static void closeQueue(mqd_t& queue)
            {
                if (queue != static_cast<mqd_t>(-1)) {
                    mq_close(queue);
                    queue = static_cast<mqd_t>(-1);
                }
            }

            static mqd_t createQueue(const std::string& name)
            {
                (void)mq_unlink(name.c_str());

                mq_attr attributes{};
                attributes.mq_maxmsg = 10;
                attributes.mq_msgsize = static_cast<long>(Connection::capacity);

                mqd_t queue = mq_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600,
                    &attributes);
                if (queue == static_cast<mqd_t>(-1)) {
                    throw std::system_error(errno, std::generic_category(),
                        "Не удалось создать очередь сообщений");
                }
                return queue;
            }

            static mqd_t openQueue(const std::string& name)
            {
                mqd_t queue = mq_open(name.c_str(), O_RDWR);
                if (queue == static_cast<mqd_t>(-1)) {
                    throw std::system_error(errno, std::generic_category(),
                        "Не удалось открыть очередь сообщений");
                }
                return queue;
            }

            void openTransport(Side side) override
            {
                if (side == Side::host) {
                    toHost_ = createQueue(toHostName_);
                    owner_ = true;
                    toChild_ = createQueue(toChildName_);

                    incoming_ = toHost_;
                    outgoing_ = toChild_;
                }
                else {
                    toHost_ = openQueue(toHostName_);
                    toChild_ = openQueue(toChildName_);

                    incoming_ = toChild_;
                    outgoing_ = toHost_;
                }
            }

            void receive(void* data, std::size_t size) override
            {
                std::array<char, Connection::capacity> buffer{};

                const ssize_t bytes =
                    mq_receive(incoming_, buffer.data(), buffer.size(), nullptr);
                if (bytes == -1) {
                    throw std::system_error(errno, std::generic_category(),
                        "Ошибка чтения из очереди сообщений");
                }
                if (static_cast<std::size_t>(bytes) != size) {
                    throw std::runtime_error("Получено сообщение неверного размера");
                }

                std::memcpy(data, buffer.data(), size);
            }

            void send(const void* data, std::size_t size) override
            {
                const auto* bytes = static_cast<const char*>(data);

                if (mq_send(outgoing_, bytes, size, 0) == -1) {
                    throw std::system_error(errno, std::generic_category(),
                        "Ошибка записи в очередь сообщений");
                }
            }

            std::string toHostName_;
            std::string toChildName_;

            mqd_t toHost_ = static_cast<mqd_t>(-1);
            mqd_t toChild_ = static_cast<mqd_t>(-1);
            mqd_t incoming_ = static_cast<mqd_t>(-1);
            mqd_t outgoing_ = static_cast<mqd_t>(-1);

            bool owner_ = false;
        };

    }

    std::unique_ptr<Connection> makeConnection()
    {
        return std::make_unique<MqConnection>();
    }

}