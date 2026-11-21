#include "host_worker.hpp"

#include "protocol.hpp"

HostWorker::HostWorker(Conn* connection)
    : connection_(connection), client_connected_(false) {}

void HostWorker::requestGoat(int round, int maximum) {
    const Message request{kMessageRound, round, maximum, 0};
    if (!connection_->Write(&request, sizeof(request))) {
        emit failed(QString::fromStdString(connection_->LastError()));
        return;
    }

    Message response{};
    if (!connection_->Read(&response, sizeof(response))) {
        emit failed(QString::fromStdString(connection_->LastError()));
        return;
    }
    if (response.type != kMessageGoat || response.round != round || response.flag != 0 ||
        response.value < 1 || response.value > maximum) {
        emit failed("Клиент прислал некорректное число или номер раунда");
        return;
    }

    if (!client_connected_) {
        client_connected_ = true;
        emit clientConnected();
    }
    emit goatReceived(round, response.value);
}

void HostWorker::sendResult(int round, bool alive, bool game_over) {
    const Message result{kMessageResult, round, alive ? 1 : 0, game_over ? 1 : 0};
    if (!connection_->Write(&result, sizeof(result))) {
        emit failed(QString::fromStdString(connection_->LastError()));
        return;
    }
    emit resultSent(round, alive, game_over);
}
