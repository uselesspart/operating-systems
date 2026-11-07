// Хост игры "Волк и семеро козлят" (волк). Один процесс обслуживает произвольное число независимых козлят-клиентов.
// Основная сборка - GUI на Qt; при сборке с -DNO_GUI=ON (макрос WOLF_NO_GUI) - консольный режим для автотестов.
//
// Параметры: --wolf N          волк всегда бросает число N (без ввода пользователя)
//            --wait-kids K     дождаться K козлят перед первым раундом (по умолчанию 1)
//            --round-delay MS  пауза между раундами (по умолчанию 1500)
//            --wolf-timeout MS время на ввод числа волка (по умолчанию 3000)
//            --max-rounds N    ограничить число раундов
#include <signal.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include "game_host.h"
#include "handshake.h"
#include "log_util.h"

namespace {

volatile sig_atomic_t g_stop = 0;
void OnStopSignal(int) { g_stop = 1; }

void InstallStopHandlers() {
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = OnStopSignal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    signal(SIGPIPE, SIG_IGN);  // запись в закрытый канал не должна убивать хост
}

void PrintUsage(const char* prog) {
    std::cerr << "Использование: " << prog
              << " [--wolf N] [--wait-kids K] [--round-delay MS] [--wolf-timeout MS] [--max-rounds N]\n";
}

bool ParseInt(const char* s, int lo, int& out) {
    char* end = nullptr;
    long v = std::strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < lo || v > 1000000) return false;
    out = static_cast<int>(v);
    return true;
}

// Разбор параметров игры. Возвращает false при ошибке (сообщение уже выведено).
bool ParseOptions(int argc, char* argv[], GameConfig& cfg) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        int* target = nullptr;
        int lo = 0;
        if (a == "--wolf") { target = &cfg.fixedWolf; lo = 1; }
        else if (a == "--wait-kids") { target = &cfg.waitKids; lo = 1; }
        else if (a == "--round-delay") { target = &cfg.roundDelayMs; lo = 0; }
        else if (a == "--wolf-timeout") { target = &cfg.wolfTimeoutMs; lo = 1; }
        else if (a == "--max-rounds") { target = &cfg.maxRounds; lo = 1; }
        else { PrintUsage(argv[0]); return false; }
        if (i + 1 >= argc || !ParseInt(argv[i + 1], lo, *target)) {
            std::cerr << "Неверное значение для " << a << "\n";
            PrintUsage(argv[0]);
            return false;
        }
        ++i;
    }
    if (cfg.fixedWolf > rules::kMaxWolfNumber) {
        std::cerr << "--wolf: число должно быть от 1 до " << rules::kMaxWolfNumber << "\n";
        return false;
    }
    return true;
}

}  // namespace

#ifdef WOLF_NO_GUI
// ===================================================================== консольный режим

namespace {
class ConsoleView : public IGameView {
public:
    void OnLog(const std::string& msg) override { LogLine("host", msg); }
    void OnKids(const std::vector<KidInfo>&) override {}
    void OnWolfPrompt(bool, int) override {}
    void OnRound(const RoundReport& r) override { LogLine("host", FormatRound(r)); }
    void OnGameOver(const std::string& reason) override { LogLine("host", "Игра окончена: " + reason); }
};
}  // namespace

int main(int argc, char* argv[]) {
    BlockHandshakeSignal();  // до создания потоков: SIGUSR1 принимает только sigtimedwait
    InstallStopHandlers();

    GameConfig cfg;
    cfg.interactive = false;
    if (!ParseOptions(argc, argv, cfg)) return 2;

    ConsoleView view;
    GameHost host(view, cfg);
    host.Start();
    while (!g_stop && !host.Finished()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (g_stop) LogLine("host", "Получен сигнал завершения, останавливаюсь");
    host.Stop();
    return 0;
}

#else
// ===================================================================== GUI (Qt)
// Без Q_OBJECT/moc: обработчики - лямбды, обновление интерфейса из потоков хоста - через
// QMetaObject::invokeMethod(..., Qt::QueuedConnection).

#include <QApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMainWindow>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace {

class MainWindow : public QMainWindow {
public:
    MainWindow() {
        setWindowTitle(QStringLiteral("Волк и семеро козлят - хост (волк)"));
        resize(900, 650);

        auto* central = new QWidget(this);
        auto* layout = new QVBoxLayout(central);

        statusLabel_ = new QLabel(QStringLiteral("Хост запускается..."), central);
        layout->addWidget(statusLabel_);

        // --- ввод числа волка
        auto* inputRow = new QHBoxLayout();
        promptLabel_ = new QLabel(QStringLiteral("Ожидание следующего раунда"), central);
        wolfSpin_ = new QSpinBox(central);
        wolfSpin_->setRange(rules::kMinNumber, rules::kMaxWolfNumber);
        wolfSpin_->setValue(50);
        throwButton_ = new QPushButton(QStringLiteral("Бросить число"), central);
        wolfSpin_->setEnabled(false);
        throwButton_->setEnabled(false);
        inputRow->addWidget(promptLabel_, 1);
        inputRow->addWidget(wolfSpin_);
        inputRow->addWidget(throwButton_);
        layout->addLayout(inputRow);

        // --- таблица козлят
        kidsTable_ = new QTableWidget(0, 4, central);
        kidsTable_->setHorizontalHeaderLabels(QStringList() << QStringLiteral("Козленок") << QStringLiteral("pid")
                                                            << QStringLiteral("Состояние")
                                                            << QStringLiteral("Последнее число"));
        kidsTable_->horizontalHeader()->setStretchLastSection(true);
        kidsTable_->setEditTriggers(QAbstractItemView::NoEditTrigger);
        kidsTable_->setMaximumHeight(180);
        layout->addWidget(kidsTable_);

        // --- журнал
        log_ = new QPlainTextEdit(central);
        log_->setReadOnly(true);
        layout->addWidget(log_, 1);

        setCentralWidget(central);

        countdown_ = new QTimer(this);
        countdown_->setInterval(100);
        QObject::connect(countdown_, &QTimer::timeout, this, [this]() {
            countdownMs_ -= 100;
            if (countdownMs_ < 0) countdownMs_ = 0;
            promptLabel_->setText(QStringLiteral("Бросайте число! Осталось: %1 с").arg(countdownMs_ / 1000.0, 0, 'f', 1));
        });
        QObject::connect(throwButton_, &QPushButton::clicked, this, [this]() {
            if (host_ && host_->SubmitWolfNumber(wolfSpin_->value())) {
                throwButton_->setEnabled(false);
                wolfSpin_->setEnabled(false);
            }
        });
    }

    void SetHost(GameHost* host) { host_ = host; }

    void AppendLog(const QString& text) { log_->appendPlainText(QStringLiteral("[") + QString::fromStdString(Timestamp()) + QStringLiteral("] ") + text); }

    void SetKids(const std::vector<KidInfo>& kids) {
        int connected = 0;
        kidsTable_->setRowCount(static_cast<int>(kids.size()));
        for (int row = 0; row < static_cast<int>(kids.size()); ++row) {
            const KidInfo& k = kids[static_cast<size_t>(row)];
            if (k.connected) ++connected;
            QString state = !k.connected ? QStringLiteral("отключен") : (k.alive ? QStringLiteral("жив") : QStringLiteral("мертв"));
            kidsTable_->setItem(row, 0, new QTableWidgetItem(QStringLiteral("#%1").arg(k.id)));
            kidsTable_->setItem(row, 1, new QTableWidgetItem(QString::number(k.pid)));
            kidsTable_->setItem(row, 2, new QTableWidgetItem(state));
            kidsTable_->setItem(row, 3, new QTableWidgetItem(k.lastNumber > 0 ? QString::number(k.lastNumber) : QStringLiteral("-")));
        }
        connectedKids_ = connected;
        UpdateStatus();
    }

    void ShowRound(const RoundReport& r) {
        round_ = r.round;
        UpdateStatus();
        AppendLog(QString::fromStdString(FormatRound(r)));
    }

    void SetPrompt(bool active, int timeoutMs) {
        wolfSpin_->setEnabled(active);
        throwButton_->setEnabled(active);
        if (active) {
            countdownMs_ = timeoutMs;
            countdown_->start();
            promptLabel_->setText(QStringLiteral("Бросайте число! Осталось: %1 с").arg(timeoutMs / 1000.0, 0, 'f', 1));
            wolfSpin_->setFocus();
            wolfSpin_->selectAll();
        } else {
            countdown_->stop();
            promptLabel_->setText(QStringLiteral("Ожидание следующего раунда"));
        }
    }

    void GameOver(const QString& reason) {
        gameOver_ = true;
        UpdateStatus();
        AppendLog(QStringLiteral("ИГРА ОКОНЧЕНА: ") + reason);
    }

private:
    void UpdateStatus() {
        statusLabel_->setText(QStringLiteral("pid хоста: %1   |   козлят подключено: %2   |   раунд: %3%4")
                                  .arg(getpid())
                                  .arg(connectedKids_)
                                  .arg(round_)
                                  .arg(gameOver_ ? QStringLiteral("   |   ИГРА ОКОНЧЕНА") : QString()));
    }

    GameHost* host_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* promptLabel_ = nullptr;
    QSpinBox* wolfSpin_ = nullptr;
    QPushButton* throwButton_ = nullptr;
    QTableWidget* kidsTable_ = nullptr;
    QPlainTextEdit* log_ = nullptr;
    QTimer* countdown_ = nullptr;
    int countdownMs_ = 0;
    int connectedKids_ = 0;
    int round_ = 0;
    bool gameOver_ = false;
};

// Реализация IGameView: вызывается из потоков хоста, переправляет обновления в GUI-поток.
class GuiView : public IGameView {
public:
    explicit GuiView(MainWindow* w) : w_(w) {}

    void OnLog(const std::string& msg) override {
        LogLine("host", msg);
        QString q = QString::fromStdString(msg);
        Post([this, q]() { w_->AppendLog(q); });
    }
    void OnKids(const std::vector<KidInfo>& kids) override {
        Post([this, kids]() { w_->SetKids(kids); });
    }
    void OnWolfPrompt(bool active, int timeoutMs) override {
        Post([this, active, timeoutMs]() { w_->SetPrompt(active, timeoutMs); });
    }
    void OnRound(const RoundReport& r) override {
        LogLine("host", FormatRound(r));
        Post([this, r]() { w_->ShowRound(r); });
    }
    void OnGameOver(const std::string& reason) override {
        LogLine("host", "Игра окончена: " + reason);
        QString q = QString::fromStdString(reason);
        Post([this, q]() { w_->GameOver(q); });
    }

private:
    template <typename F>
    void Post(F f) {
        QMetaObject::invokeMethod(w_, f, Qt::QueuedConnection);
    }
    MainWindow* w_;
};

}  // namespace

int main(int argc, char* argv[]) {
    BlockHandshakeSignal();  // до создания Qt-потоков: SIGUSR1 принимает только sigtimedwait
    InstallStopHandlers();

    QApplication app(argc, argv);  // убирает из argv собственные параметры Qt

    GameConfig cfg;
    cfg.interactive = true;
    if (!ParseOptions(argc, argv, cfg)) return 2;
    if (cfg.fixedWolf > 0) cfg.interactive = false;

    MainWindow window;
    GuiView view(&window);
    GameHost host(view, cfg);  // объявлен после окна и view => уничтожается раньше них
    window.SetHost(&host);
    window.show();

    // Завершение по SIGINT/SIGTERM
    QTimer stopTimer;
    QObject::connect(&stopTimer, &QTimer::timeout, &app, []() {
        if (g_stop) QApplication::quit();
    });
    stopTimer.start(200);

    host.Start();
    int rc = app.exec();
    host.Stop();
    return rc;
}

#endif  // WOLF_NO_GUI
