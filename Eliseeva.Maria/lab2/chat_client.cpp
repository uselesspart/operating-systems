// Клиент сетевого чата (Qt GUI). Запускается хостом как дочерний процесс
// (fork + exec с ключом --client <id> <spec>), см. host.cpp.
#include <QtWidgets>

#include <signal.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <thread>

#include "conn.h"
#include "log.h"
#include "protocol.h"
#include "sem.h"

namespace {
long long steady_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
QString q(const std::string& s) { return QString::fromUtf8(s.c_str()); }

// сколько элементов в списке "0,1,3" (хост "0" входит)
int listCount(const std::string& csv) {
    int n = 0;
    size_t pos = 0;
    while (pos <= csv.size()) {
        size_t e = csv.find(',', pos);
        if (e == std::string::npos) e = csv.size();
        if (e > pos) ++n;
        pos = e + 1;
    }
    return n;
}

// есть ли id в списке "0,1,3"
bool listHas(const std::string& csv, int id) {
    size_t pos = 0;
    while (pos <= csv.size()) {
        size_t e = csv.find(',', pos);
        if (e == std::string::npos) e = csv.size();
        if (e > pos && std::atoi(csv.substr(pos, e - pos).c_str()) == id) return true;
        pos = e + 1;
    }
    return false;
}
}  // namespace

class ClientWindow : public QMainWindow {
    Q_OBJECT
public:
    ClientWindow(int id, const std::string& spec) : id_(id) {
        auto* central = new QWidget(this);
        auto* lay = new QVBoxLayout(central);
        chat_ = new QTextEdit;
        chat_->setReadOnly(true);
        lay->addWidget(chat_, 1);
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(q("Кому:")));
        to_ = new QComboBox;
        to_->addItem(q("Всем"), ID_ALL);
        row->addWidget(to_);
        input_ = new QLineEdit;
        input_->setPlaceholderText(q("Введите сообщение и нажмите Enter"));
        row->addWidget(input_, 1);
        send_ = new QPushButton(q("Отправить"));
        row->addWidget(send_);
        lay->addLayout(row);
        setCentralWidget(central);
        resize(520, 400);
        setWindowTitle(q(who(id) + " — чат [" + Conn::TypeName() + "]"));

        connect(this, &ClientWindow::chatLine, this, &ClientWindow::appendChat, Qt::QueuedConnection);
        connect(this, &ClientWindow::sysLine, this, &ClientWindow::appendSys, Qt::QueuedConnection);
        connect(this, &ClientWindow::usersChanged, this, &ClientWindow::setUsers, Qt::QueuedConnection);
        connect(this, &ClientWindow::fatal, this, &ClientWindow::onFatal, Qt::QueuedConnection);
        connect(send_, &QPushButton::clicked, this, &ClientWindow::onSend);
        connect(input_, &QLineEdit::returnPressed, this, &ClientWindow::onSend);

        try {
            conn_.reset(new Conn(id, false, spec));
        } catch (const std::exception& e) {
            emit fatal(q(std::string("Ошибка канала связи: ") + e.what()));
            return;
        }
        th_ = std::thread([this] { netThread(); });
    }
    ~ClientWindow() override { stopAll(); }

signals:
    void chatLine(const QString&);
    void sysLine(const QString&);
    void usersChanged(const QString&);
    void fatal(const QString&);

protected:
    void closeEvent(QCloseEvent* e) override {
        stopAll();
        QMainWindow::closeEvent(e);
    }

private slots:
    void appendChat(const QString& s) { chat_->append(s.toHtmlEscaped()); }
    void appendSys(const QString& s) {
        chat_->append("<i><span style='color:#777'>" + s.toHtmlEscaped() + "</span></i>");
        statusBar()->showMessage(s, 5000);
    }
    void setUsers(const QString& csv) {
        int cur = to_->currentData().toInt();
        to_->clear();
        to_->addItem(q("Всем"), ID_ALL);
        for (const QString& part : csv.split(',')) {
            if (part.isEmpty()) continue;
            int pid = part.toInt();
            if (pid == id_) continue;
            to_->addItem(q(who(pid)), pid);
        }
        int idx = to_->findData(cur);
        if (idx >= 0) to_->setCurrentIndex(idx);
    }
    void onFatal(const QString& reason) {
        connected_ = false;
        chatlog::line(who(id_), reason.toUtf8().toStdString() + " — клиент останавливается");
        appendSys(reason + q(" — клиент завершает работу"));
        input_->setEnabled(false);
        send_->setEnabled(false);
        QTimer::singleShot(2500, this, [] { QCoreApplication::quit(); });
    }
    void onSend() {
        std::string text = input_->text().toUtf8().toStdString();
        if (text.empty() || !connected_) return;
        Msg m{};
        m.type = MT_CHAT;
        m.from = id_;
        m.to = to_->currentData().toInt();
        m.ts = wall_ms();
        set_text(m, text);
        input_->clear();
        if (!conn_->Write(&m, sizeof m, 5000)) onFatal(q("Не удалось отправить сообщение хосту (тайм-аут/обрыв)"));
    }

private:
    void netThread() {
        chatlog::line(who(id_), "подключение к хосту");
        emit sysLine(q("Подключение к хосту..."));
        if (!conn_->Open()) {
            emit fatal(q("Не удалось установить соединение с хостом (5 с)"));
            return;
        }
        Msg hello{};
        hello.type = MT_HELLO;
        hello.from = id_;
        hello.to = ID_HOST;
        hello.ts = wall_ms();
        if (!conn_->Write(&hello, sizeof hello, 5000)) {
            emit fatal(q("Не удалось отправить приветствие хосту"));
            return;
        }
        connected_ = true;
        emit sysLine(q("Подключён к хосту"));
        const long long start = steady_ms();
        bool gotFirst = false;
        bool said = false;
        while (!stop_) {
            Msg m{};
            int r = conn_->Read(&m, sizeof m, 500);  // плановое ожидание сообщений
            if (r == 0) {
                if (!gotFirst && steady_ms() - start > 5000) {
                    emit fatal(q("Хост не ответил за 5 секунд"));
                    return;
                }
                continue;
            }
            if (r < 0) {
                if (!stop_) emit fatal(q("Связь с хостом потеряна"));
                return;
            }
            gotFirst = true;
            m.text[sizeof m.text - 1] = 0;
            switch (m.type) {
            case MT_CHAT: {
                std::string line = format_line(m);
                chatlog::line("chat", line);
                emit chatLine(q(line));
                break;
            }
            case MT_SYS:
                emit sysLine(q(m.text));
                break;
            case MT_USERS:
                emit usersChanged(q(m.text));
                autoSay(m.text, said);
                break;
            default:
                break;
            }
        }
    }

    // Только для автотестов: CHAT_AUTOSAY=текст — клиент сам отправляет общее сообщение
    // "<текст> #<id>"; если задан CHAT_AUTOSAY_TO=<id>, дополнительно шлёт этому клиенту
    // личное "PRIVATE <текст> from <id>" (после появления адресата в списке участников).
    void autoSay(const std::string& csv, bool& said) {
        const char* say = std::getenv("CHAT_AUTOSAY");
        if (!say || said) return;
        const char* to = std::getenv("CHAT_AUTOSAY_TO");
        const int target = to ? std::atoi(to) : id_;
        // CHAT_AUTOSAY_MIN=N — не говорить, пока в чате меньше N клиентов (хост не считается)
        if (const char* mn = std::getenv("CHAT_AUTOSAY_MIN"))
            if (listCount(csv) < std::atoi(mn) + 1) return;
        if (target != id_ && !listHas(csv, target)) return;
        said = true;
        Msg m{};
        m.type = MT_CHAT;
        m.from = id_;
        m.to = ID_ALL;
        m.ts = wall_ms();
        set_text(m, std::string(say) + " #" + std::to_string(id_));
        conn_->Write(&m, sizeof m, 5000);
        if (target != id_) {
            m.to = target;
            set_text(m, std::string("PRIVATE ") + say + " from " + std::to_string(id_));
            conn_->Write(&m, sizeof m, 5000);
        }
    }

    void stopAll() {
        stop_ = true;
        if (th_.joinable()) th_.join();
    }

    int id_;
    QTextEdit* chat_ = nullptr;
    QComboBox* to_ = nullptr;
    QLineEdit* input_ = nullptr;
    QPushButton* send_ = nullptr;
    std::unique_ptr<Conn> conn_;
    std::thread th_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> connected_{false};
};

int run_client(int argc, char** argv) {
    signal(SIGPIPE, SIG_IGN);
    if (argc < 4) {
        fprintf(stderr, "usage: %s --client <id> <spec>\n", argv[0]);
        return 2;
    }
    const int id = atoi(argv[2]);
    const std::string spec = argv[3];
    QApplication app(argc, argv);
    chatlog::init("chat_client" + std::to_string(id) + "_" + Conn::TypeName() + ".log");
    ClientWindow w(id, spec);
    w.show();
    return app.exec();
}

#include "chat_client.moc"
