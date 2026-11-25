#include "conn.h"
#include "chat_logic.h"
#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QTimer>
#include <QDateTime>
#include <csignal>
#include <chrono>
#include <cstdio>
#include <thread>

static QTextEdit*  g_logView = nullptr;
static QTextEdit*  g_chatView = nullptr;
static QLabel*     g_status   = nullptr;
static QComboBox*  g_target   = nullptr;
static Conn*       g_conn     = nullptr;
static ChatLogic   g_logic;
static int         g_peerPid  = -1;

static void logMsg(const QString& s) {
    if (g_logView) g_logView->append(
        QString("[%1] %2").arg(QDateTime::currentDateTime().toString("hh:mm:ss")).arg(s));
}

static void sigusr1_handler(int) {
    logMsg("SIGUSR1 received from client");
}

static void processIncoming() {
    if (!g_conn) return;
    WireMessage wm;
    if (!g_conn->Read(&wm, sizeof(wm))) return;

    if (strcmp(wm.content, "ping") == 0) {
        WireMessage ack = wm;
        ack.type = WireMessage::ACK;
        g_conn->Write(&ack, sizeof(ack));
        return;
    }

    if (wm.type == WireMessage::ACK) return;

    std::string sender(wm.sender);
    g_logic.addParticipant(sender);
    g_logic.updateActivity(sender, std::chrono::system_clock::now());

    ChatMessage cm;
    cm.sender    = sender;
    cm.recipient = wm.recipient;
    cm.content   = wm.content;
    cm.timestamp = std::chrono::system_clock::now();

    auto targets = g_logic.routeMessage(cm);

    // Отправляем ACK
    WireMessage ack = wm; ack.type = WireMessage::ACK;
    g_conn->Write(&ack, sizeof(ack));

    for (const auto& t : targets) {
        if (t == "host") {
            g_chatView->append(QString("%1: %2")
                .arg(QString::fromStdString(sender))
                .arg(QString::fromStdString(cm.content)));
        } else {
            WireMessage fwd = wm;
            strncpy(fwd.recipient, t.c_str(), sizeof(fwd.recipient)-1);
            g_conn->Write(&fwd, sizeof(fwd));
        }
    }
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    signal(SIGUSR1, sigusr1_handler);

    QWidget window;
    window.setWindowTitle("Chat Host - Client");
    window.resize(700, 500);

    g_status   = new QLabel("Waiting for client...", &window);
    g_chatView = new QTextEdit(&window);  g_chatView->setReadOnly(true);
    g_logView  = new QTextEdit(&window);  g_logView->setReadOnly(true);
    g_logView->setMaximumHeight(100);

    QLineEdit*  input  = new QLineEdit(&window);
    QPushButton* sendBtn = new QPushButton("Send", &window);
    g_target = new QComboBox(&window);
    g_target->addItem("All", QString(""));

    QHBoxLayout* inputLayout = new QHBoxLayout;
    inputLayout->addWidget(g_target);
    inputLayout->addWidget(input);
    inputLayout->addWidget(sendBtn);

    QVBoxLayout* main = new QVBoxLayout(&window);
    main->addWidget(g_status);
    main->addWidget(g_chatView, 3);
    main->addWidget(g_logView, 1);
    main->addLayout(inputLayout);

    QObject::connect(sendBtn, &QPushButton::clicked, [&]() {
        if (!g_conn) return;
        QString text = input->text().trimmed();
        if (text.isEmpty()) return;

        WireMessage wm;
        wm.type = g_target->currentData().toString().isEmpty()
                  ? WireMessage::BROADCAST : WireMessage::PRIVATE;
        strncpy(wm.sender, "host", sizeof(wm.sender)-1);
        strncpy(wm.recipient, g_target->currentData().toString().toUtf8().constData(),
                sizeof(wm.recipient)-1);
        strncpy(wm.content, text.toUtf8().constData(), sizeof(wm.content)-1);
        wm.timestamp_ms = QDateTime::currentMSecsSinceEpoch();

        if (!g_conn->Write(&wm, sizeof(wm))) { logMsg("Write failed"); return; }
        g_chatView->append(QString("host: %1").arg(text));
        input->clear();
    });

    QTimer readTimer;
    QObject::connect(&readTimer, &QTimer::timeout, processIncoming);
    readTimer.start(100);

    QTimer timeoutTimer;
    QObject::connect(&timeoutTimer, &QTimer::timeout, [&]() {
        auto now = std::chrono::system_clock::now();
        auto timedOut = g_logic.getTimedOutParticipants(now);
        for (const auto& name : timedOut) {
            logMsg(QString("Timeout: %1 - SIGKILL").arg(QString::fromStdString(name)));
            if (g_peerPid > 0) kill(g_peerPid, SIGKILL);
            g_logic.removeParticipant(name);
        }
    });
    timeoutTimer.start(1000);

    QTimer targetUpdateTimer;
    QObject::connect(&targetUpdateTimer, &QTimer::timeout, [&]() {
        if (!g_conn) return;
        QString currentText = g_target->currentText();
        g_target->blockSignals(true);
        g_target->clear();
        g_target->addItem("All", QString(""));
        for (const auto& p : g_logic.participants()){
            if (p != "host") g_target->addItem(QString::fromStdString(p), QString::fromStdString(p));
        }
        int index = g_target->findText(currentText);
        if (index != -1) g_target->setCurrentIndex(index);
        g_target->blockSignals(false);
    });
    targetUpdateTimer.start(1000);

    window.show();
 
    std::thread handshakeThread([&]() {
        Conn* c = createConn("", true);
        if (!c) {
            QMetaObject::invokeMethod(&window, [&]() { logMsg("Failed to create conn"); });
            return;
        }
        if (!c->Handshake()) {
            QMetaObject::invokeMethod(&window, [&]() { logMsg("Handshake failed"); });
            delete c;
            return;
        }
        g_conn = c;
        g_peerPid = g_conn->GetPeerPid();
        g_logic.addParticipant("host");
        g_logic.addParticipant("client"); 

        QMetaObject::invokeMethod(&window, [&]() {
            g_status->setText("Client connected");
            logMsg("Client joined");
        });
    });
    handshakeThread.detach();

    int rc = app.exec();
    delete g_conn;
    return rc;
}