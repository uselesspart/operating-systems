#pragma once

#include <functional>
#include <string>
#include <string_view>

class QObject;

// Одна труба переносит данные в одном направлении.
class PipeChannel {
public:
    PipeChannel();
    ~PipeChannel();
    PipeChannel(const PipeChannel&) = delete;
    PipeChannel& operator=(const PipeChannel&) = delete;

    void closeReader();
    void closeWriter();
    void send(std::string_view text) const;

    // Вызывается один раз владельцем читающего конца, после создания QApplication.
    // context должен быть уничтожен раньше PipeChannel.
    void listen(QObject* context,
                std::function<void(const std::string&)> onMessage,
                std::function<void(const std::string&)> onEnd);

private:
    int ends_[2] = {-1, -1};
    std::string buffer_;
};
