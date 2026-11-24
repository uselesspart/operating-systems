#include "client_reader.hpp"
#include "ipc/connection.hpp"

#include <exception>

ClientReader::ClientReader(Connection& connection)
    : connection_(connection)
{
}

ClientReader::~ClientReader()
{
    requestInterruption();
    quit();
    wait();
}

void ClientReader::run()
{
    // Объект создан в рабочем потоке. Его QSocketNotifier живёт здесь же.
    QObject context;
    try {
        connection_.listen(&context,
            [this](const std::string& bytes) {
                emit received(QString::fromStdString(bytes));
            },
            [this](const std::string& reason) {
                emit disconnected(QString::fromStdString(reason));
                quit();
            });
        if (!isInterruptionRequested()) {
            exec();
        }
    } catch (const std::exception& e) {
        emit disconnected(QString::fromUtf8(e.what()));
    }
}
