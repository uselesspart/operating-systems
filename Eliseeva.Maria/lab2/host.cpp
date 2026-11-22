// Хост сетевого чата (Qt GUI). Клиенты — дочерние процессы (fork + exec этого же
// файла с ключом --client), для каждого клиента выделен отдельный поток.
#include <QtWidgets>

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <thread>
#include <vector>

#include "conn.h"
#include "log.h"
#include "protocol.h"
#include "sem.h"

int run_client(int argc, char** argv);  // chat_client.cpp

namespace {
constexpr int kIoTimeoutMs = 5000;         // тайм-аут операций ожидания
constexpr long long kDefaultIdleLimitMs = 60000;  // клиент молчит дольше минуты -> SIGKILL

long long steady_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
QString q(const std::string& s) { return QString::fromUtf8(s.c_str()); }
}  // namespace

struct ClientRec {
    int id = 0;
    pid_t pid = -1;
    std::unique_ptr<Conn> conn;
    Sem wsem;  // локальный семафор: один писатель в канал клиента
    std::atomic<bool> alive{true};
    std::atomic<bool> joined{false};
    std::atomic<bool> reaped{false};
    std::atomic<long long> last_ms{0};
    std::thread th;
};

class HostWindow : public QMainWindow {
    Q_OBJECT
public:
    HostWindow() {
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

        add_ = new QPushButton(q("Добавить клиента"));
        lay->addWidget(add_);
        setCentralWidget(central);
        resize(640, 480);
        setWindowTitle(q(std::string("Чат — Хост [") + Conn::TypeName() + "]"));

        connect(this, &HostWindow::chatLine, this, &HostWindow::appendChat, Qt::QueuedConnection);
        connect(this, &HostWindow::sysLine, this, &HostWindow::appendSys, Qt::QueuedConnection);
        connect(this, &HostWindow::usersChanged, this, &HostWindow::setUsers, Qt::QueuedConnection);
        connect(send_, &QPushButton::clicked, this, &HostWindow::onSend);
        connect(input_, &QLineEdit::returnPressed, this, &HostWindow::onSend);
        connect(add_, &QPushButton::clicked, this, &HostWindow::spawnClient);
        connect(&timer_, &QTimer::timeout, this, &HostWindow::onTick);
        timer_.start(1000);
        sys("Хост запущен, тип связи: " + std::string(Conn::TypeName()));

        // Переменные окружения для автоматических тестов (в обычной работе не нужны):
        //   CHAT_IDLE_MS        — порог неактивности клиента вместо 60000 мс
        //   CHAT_AUTOSPAWN=N    — автоматически запустить N клиентов
        //   CHAT_EXIT_AFTER_MS  — штатно закрыть хост через указанное время
        if (const char* e = std::getenv("CHAT_IDLE_MS")) idleLimitMs_ = std::atoll(e);
        if (const char* e = std::getenv("CHAT_AUTOSPAWN")) {
            const int cnt = std::atoi(e);
            for (int i = 0; i < cnt; ++i) QTimer::singleShot(200 * (i + 1), this, [this] { spawnClient(); });
        }
        if (const char* e = std::getenv("CHAT_EXIT_AFTER_MS"))
            QTimer::singleShot(std::atoi(e), this, [this] { close(); });
    }
    ~HostWindow() override { shutdown(); }

signals:
    void chatLine(const QString&);
    void sysLine(const QString&);
    void usersChanged(const QString&);

protected:
    void closeEvent(QCloseEvent* e) override {
        shutdown();
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
            int id = part.toInt();
            if (id == ID_HOST) continue;
            to_->addItem(q(who(id)), id);
        }
        int idx = to_->findData(cur);
        if (idx >= 0) to_->setCurrentIndex(idx);
    }

    void onSend() {
        std::string text = input_->text().toUtf8().toStdString();
        if (text.empty()) return;
        Msg m{};
        m.type = MT_CHAT;
        m.from = ID_HOST;
        m.to = to_->currentData().toInt();
        set_text(m, text);
        input_->clear();
        route(m);
    }

    // Создание клиента: fork + exec (клиент — GUI-процесс, потомок хоста)
    void spawnClient() {
        int id = nextId_++;
        auto rec = std::make_shared<ClientRec>();
        rec->id = id;
        try {
            rec->conn.reset(new Conn(id, true));
        } catch (const std::exception& e) {
            sys(std::string("Не удалось создать канал связи: ") + e.what());
            return;
        }
        std::vector<std::string> a{"/proc/self/exe", "--client", std::to_string(id), rec->conn->Spec()};
        std::vector<char*> argv;
        for (auto& s : a) argv.push_back(&s[0]);
        argv.push_back(nullptr);
        std::vector<int> fds = rec->conn->ChildFds();

        pid_t pid = fork();
        if (pid < 0) {
            sys(std::string("fork: ") + strerror(errno));
            return;
        }
        if (pid == 0) {  // ребёнок: только async-signal-safe вызовы
            for (int fd : fds) fcntl(fd, F_SETFD, 0);
            execv(argv[0], argv.data());
            _exit(127);
        }
        rec->conn->CloseChildEnds();
        rec->pid = pid;
        rec->last_ms = steady_ms();
        {
            SemGuard g(gsem_);
            if (!g.ok()) {
                sys("Тайм-аут семафора при добавлении клиента");
                kill(pid, SIGKILL);
                waitpid(pid, nullptr, 0);
                return;
            }
            clients_[id] = rec;
        }
        rec->th = std::thread(&HostWindow::clientThread, this, rec);
        sys(who(id) + " запущен (pid " + std::to_string(pid) + ")");
    }

    void onTick() {
        reapChildren();
        SemGuard g(gsem_);
        if (!g.ok()) return;
        const long long now = steady_ms();
        for (auto& kv : clients_) {
            ClientRec& r = *kv.second;
            if (r.joined && r.alive && now - r.last_ms > idleLimitMs_) {
                sysBroadcast_locked(who(r.id) + " молчит более минуты — завершён (SIGKILL)");
                killClient(r);
                r.alive = false;
            }
        }
    }

private:
    void sys(const std::string& s) {
        chatlog::line("host", s);
        emit sysLine(q(s));
    }

    void killClient(ClientRec& r) {
        if (!r.reaped && r.pid > 0) kill(r.pid, SIGKILL);
    }

    void reapChildren() {
        int st = 0;
        pid_t p;
        while ((p = waitpid(-1, &st, WNOHANG)) > 0) {
            SemGuard g(gsem_);
            for (auto& kv : clients_)
                if (kv.second->pid == p) kv.second->reaped = true;
            if (WIFSIGNALED(st))
                chatlog::line("host", "процесс " + std::to_string(p) + " завершён сигналом " + std::to_string(WTERMSIG(st)));
            else
                chatlog::line("host", "процесс " + std::to_string(p) + " завершился, код " + std::to_string(WEXITSTATUS(st)));
        }
    }

    // Запись в канал клиента под его семафором. Вызывается с захваченным gsem_.
    bool sendTo(ClientRec& r, const Msg& m) {
        SemGuard w(r.wsem);
        if (!w.ok()) {
            chatlog::line("host", "тайм-аут семафора записи для " + who(r.id));
            r.alive = false;
            killClient(r);
            return false;
        }
        if (!r.conn || !r.alive) return false;
        if (!r.conn->Write(&m, sizeof m, kIoTimeoutMs)) {
            chatlog::line("host", "ошибка/тайм-аут записи клиенту " + std::to_string(r.id) + " — связь прервана");
            r.alive = false;
            killClient(r);
            return false;
        }
        return true;
    }

    void sendSys(ClientRec& r, const std::string& text) {
        Msg m{};
        m.type = MT_SYS;
        m.from = ID_HOST;
        m.to = r.id;
        m.ts = wall_ms();
        set_text(m, text);
        sendTo(r, m);
    }

    void sysBroadcast_locked(const std::string& text) {
        sys(text);
        Msg m{};
        m.type = MT_SYS;
        m.from = ID_HOST;
        m.to = ID_ALL;
        m.ts = wall_ms();
        set_text(m, text);
        for (auto& kv : clients_)
            if (kv.second->joined) sendTo(*kv.second, m);
    }

    void broadcastUsers_locked() {
        std::string csv = "0";
        for (auto& kv : clients_)
            if (kv.second->joined) csv += "," + std::to_string(kv.first);
        emit usersChanged(q(csv));
        Msg m{};
        m.type = MT_USERS;
        m.from = ID_HOST;
        m.to = ID_ALL;
        m.ts = wall_ms();
        set_text(m, csv);
        for (auto& kv : clients_)
            if (kv.second->joined) sendTo(*kv.second, m);
    }

    // Единая точка маршрутизации: порядок сообщений определяется порядком захвата gsem_.
    void route(Msg m) {
        SemGuard g(gsem_);
        if (!g.ok()) {
            chatlog::line("host", "тайм-аут семафора маршрутизации — сообщение потеряно");
            return;
        }
        m.type = MT_CHAT;
        m.ts = wall_ms();
        m.text[sizeof m.text - 1] = 0;
        const std::string line = format_line(m);
        auto find = [&](int id) -> ClientRec* {
            auto it = clients_.find(id);
            return (it != clients_.end() && it->second->joined) ? it->second.get() : nullptr;
        };

        if (m.to == ID_ALL) {
            chatlog::line("chat", line);
            emit chatLine(q(line));
            for (auto& kv : clients_)
                if (kv.second->joined) sendTo(*kv.second, m);
            return;
        }
        // личное сообщение: видят только отправитель и адресат
        if (m.from == ID_HOST || m.to == ID_HOST) {
            chatlog::line("chat", line);
            emit chatLine(q(line));
        }
        if (m.to != ID_HOST) {
            ClientRec* r = find(m.to);
            if (r) {
                sendTo(*r, m);
            } else if (m.from == ID_HOST) {
                sys(who(m.to) + " недоступен");
            } else if (ClientRec* s = find(m.from)) {
                sendSys(*s, who(m.to) + " недоступен");
            }
        }
        if (m.from != ID_HOST && m.from != m.to) {
            if (ClientRec* s = find(m.from)) sendTo(*s, m);  // эхо отправителю
        }
    }

    void finishClient(const std::shared_ptr<ClientRec>& rec) {
        rec->alive = false;
        {
            SemGuard w(rec->wsem);
            if (w.ok()) rec->conn.reset();  // закрывает дескрипторы (для fifo — удаляет файлы)
        }
        bool was = rec->joined.exchange(false);
        if (was && !stopping_) {
            SemGuard g(gsem_);
            if (g.ok()) {
                sysBroadcast_locked(who(rec->id) + " отключился");
                broadcastUsers_locked();
            }
        }
    }

    // Поток обслуживания одного клиента
    void clientThread(std::shared_ptr<ClientRec> rec) {
        const std::string tag = who(rec->id);
        bool ok = rec->conn->Open();
        Msg m{};
        if (ok) {
            ok = false;
            for (int i = 0; i < 10 && !stopping_; ++i) {  // ожидание HELLO до 5 секунд
                int r = rec->conn->Read(&m, sizeof m, 500);
                if (r > 0) {
                    ok = (m.type == MT_HELLO);
                    break;
                }
                if (r < 0) break;
            }
        }
        if (!ok) {
            if (!stopping_) sys(tag + ": не удалось установить соединение (тайм-аут 5 с или ошибка)");
            killClient(*rec);
            finishClient(rec);
            return;
        }
        rec->last_ms = steady_ms();
        {
            SemGuard g(gsem_);
            if (!g.ok()) {
                chatlog::line("host", "тайм-аут семафора при подключении " + tag);
                killClient(*rec);
                finishClient(rec);
                return;
            }
            rec->joined = true;
            sysBroadcast_locked(tag + " присоединился");
            broadcastUsers_locked();
        }
        while (!stopping_ && rec->alive) {
            int r = rec->conn->Read(&m, sizeof m, 500);  // плановое ожидание сообщений
            if (r == 0) continue;
            if (r < 0) break;
            m.text[sizeof m.text - 1] = 0;
            if (m.type != MT_CHAT) continue;
            m.from = rec->id;
            rec->last_ms = steady_ms();
            route(m);
        }
        finishClient(rec);
    }

    void shutdown() {
        if (down_.exchange(true)) return;
        stopping_ = true;
        timer_.stop();
        std::vector<std::shared_ptr<ClientRec>> all;
        {
            SemGuard g(gsem_);
            for (auto& kv : clients_) all.push_back(kv.second);
        }
        for (auto& r : all) r->alive = false;
        for (auto& r : all)
            if (r->th.joinable()) r->th.join();
        for (auto& r : all) r->conn.reset();  // удаляет файлы fifo
        for (auto& r : all)
            if (!r->reaped && r->pid > 0) kill(r->pid, SIGTERM);
        for (int i = 0; i < 20; ++i) {  // даём детям до 1 секунды на выход
            while (waitpid(-1, nullptr, WNOHANG) > 0) {}
            bool left = false;
            for (auto& r : all)
                if (r->pid > 0 && kill(r->pid, 0) == 0) left = true;
            if (!left) break;
            usleep(50000);
        }
        for (auto& r : all)
            if (r->pid > 0 && kill(r->pid, 0) == 0) kill(r->pid, SIGKILL);
        while (waitpid(-1, nullptr, WNOHANG) > 0) {}
        chatlog::line("host", "хост завершил работу");
    }

    QTextEdit* chat_ = nullptr;
    QComboBox* to_ = nullptr;
    QLineEdit* input_ = nullptr;
    QPushButton* send_ = nullptr;
    QPushButton* add_ = nullptr;
    QTimer timer_;

    Sem gsem_;  // глобальная блокировка таблицы клиентов и порядка сообщений
    std::map<int, std::shared_ptr<ClientRec>> clients_;
    int nextId_ = 1;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> down_{false};
    long long idleLimitMs_ = kDefaultIdleLimitMs;
};

int main(int argc, char** argv) {
    signal(SIGPIPE, SIG_IGN);
    if (argc >= 2 && std::strcmp(argv[1], "--client") == 0) return run_client(argc, argv);
    QApplication app(argc, argv);
    chatlog::init(std::string("chat_host_") + Conn::TypeName() + ".log");
    HostWindow w;
    w.show();
    return app.exec();
}

#include "host.moc"
