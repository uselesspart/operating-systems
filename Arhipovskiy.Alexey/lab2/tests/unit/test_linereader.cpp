#include "client/LineReader.h"
#include "ipc/UniqueFd.h"

#include <QtTest>

#include <string>

#include <fcntl.h>
#include <unistd.h>

using namespace chat;
using namespace std::chrono_literals;
using Status = LineReader::Status;

class LineReaderTest : public QObject {
    Q_OBJECT

private:
    UniqueFd readEnd_;
    UniqueFd writeEnd_;

    void write(const std::string& data)
    {
        QCOMPARE(::write(writeEnd_.get(), data.data(), data.size()), static_cast<ssize_t>(data.size()));
    }

private slots:
    void init()
    {
        int fds[2];
        QCOMPARE(::pipe2(fds, O_CLOEXEC), 0);
        readEnd_.reset(fds[0]);
        writeEnd_.reset(fds[1]);
    }

    void readsLinesOneByOne()
    {
        LineReader reader(readEnd_.get());
        write("первая\nвторая\n");
        std::string line;
        QCOMPARE(reader.readLine(line, 1s), Status::Line);
        QCOMPARE(line, std::string("первая"));
        QCOMPARE(reader.readLine(line, 1s), Status::Line);
        QCOMPARE(line, std::string("вторая"));
    }

    void timesOutWithoutCompleteLine()
    {
        LineReader reader(readEnd_.get());
        write("незаконченная");
        std::string line;
        QElapsedTimer timer;
        timer.start();
        QCOMPARE(reader.readLine(line, 100ms), Status::Timeout);
        QCOMPARE(reader.readLine(line, 100ms), Status::Timeout);
        QVERIFY(timer.elapsed() >= 100);

        write(" строка\n");
        QCOMPARE(reader.readLine(line, 1s), Status::Line);
        QCOMPARE(line, std::string("незаконченная строка"));
    }

    void lastLineWithoutNewlineBeforeEnd()
    {
        LineReader reader(readEnd_.get());
        write("одна\nхвост");
        writeEnd_.reset();
        std::string line;
        QCOMPARE(reader.readLine(line, 1s), Status::Line);
        QCOMPARE(reader.readLine(line, 1s), Status::Line);
        QCOMPARE(line, std::string("хвост"));
        QCOMPARE(reader.readLine(line, 1s), Status::End);
        QCOMPARE(reader.readLine(line, 1s), Status::End);
    }

    void longLineSpanningSeveralReads()
    {
        LineReader reader(readEnd_.get());
        const std::string longLine(20'000, 'x');
        write(longLine + "\n");
        std::string line;
        Status status = Status::Timeout;
        for (int attempt = 0; attempt < 10 && status == Status::Timeout; ++attempt) {
            status = reader.readLine(line, 1s);
        }
        QCOMPARE(status, Status::Line);
        QCOMPARE(line.size(), longLine.size());
    }
};

QTEST_GUILESS_MAIN(LineReaderTest)
#include "test_linereader.moc"
