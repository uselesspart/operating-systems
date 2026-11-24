#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

class QObject;

class Connection {
public:
    using Handler = std::function<void(const std::string&)>;

    virtual ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // Вызывается в каждом процессе после fork(), до создания окна.
    virtual void selectSide(bool isHost);

    void send(std::string_view text) const;
    void listen(QObject* context, Handler onMessage, Handler onEnd);
    void close();

protected:
    explicit Connection(std::size_t maxWrite) : maxWrite_(maxWrite) {}

    struct Ends {
        int input = -1;
        int output = -1;

        void close();
    };

    Ends host_;
    Ends client_;

private:
    Ends* local_ = nullptr;
    std::size_t maxWrite_;
    std::string buffer_;
};

std::unique_ptr<Connection> openConnection();