#include "connection.hpp"
#include "chat/frame.hpp"

#include <QSocketNotifier>
#include <cerrno>
#include <stdexcept>
#include <system_error>
#include <unistd.h>
#include <utility>

void Connection::Ends::close()
{
    if (output >= 0 && output != input) {
        ::close(output);
    }

    if (input >= 0) {
        ::close(input);
    }

    input = output = -1;
}

Connection::~Connection()
{
    close();
}

void Connection::close()
{
    host_.close();
    client_.close();
    local_ = nullptr;
}

void Connection::selectSide(bool isHost)
{
    local_ = isHost ? &host_ : &client_;
    (isHost ? client_ : host_).close();
}

void Connection::send(std::string_view text) const
{
    const auto frame = chat::encodeFrame(text);

    if (frame.size() > maxWrite_) {
        throw std::length_error("Сообщение слишком длинное для транспорта");
    }

    ssize_t n;
    do {
        n = ::write(local_->output, frame.data(), frame.size());
    } while (n == -1 && errno == EINTR);

    if (n == -1) {
        throw std::system_error(errno, std::generic_category(), "write");
    }

    if (static_cast<std::size_t>(n) != frame.size()) {
        throw std::runtime_error("Неполная запись кадра");
    }
}

void Connection::listen(QObject* context, Handler onMessage, Handler onEnd)
{
    auto* ready = new QSocketNotifier(
        local_->input, QSocketNotifier::Read, context
    );

    QObject::connect(
        ready, &QSocketNotifier::activated, context,
        [this, ready,
         onMessage = std::move(onMessage),
         onEnd = std::move(onEnd)] {
            // Буфер вмещает максимальный кадр нашего протокола:
            // 4096 байт текста и 2 байта заголовка.
            char chunk[8192];
            const ssize_t n = ::read(local_->input, chunk, sizeof(chunk));

            if (n < 0 && (errno == EINTR ||
                          errno == EAGAIN ||
                          errno == EWOULDBLOCK)) {
                return;
            }

            if (n <= 0) {
                const int error = errno;
                ready->setEnabled(false);

                if (n < 0) {
                    onEnd(std::system_error(
                        error, std::generic_category(), "read"
                    ).what());
                } else {
                    onEnd(buffer_.empty()
                              ? ""
                              : "Соединение оборвалось внутри кадра");
                }

                return;
            }

            try {
                buffer_.append(chunk, static_cast<std::size_t>(n));

                while (auto message = chat::takeFrame(buffer_)) {
                    onMessage(*message);
                }
            } catch (const std::exception& e) {
                ready->setEnabled(false);
                onEnd(e.what());
            }
        }
    );
}