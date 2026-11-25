#include "conn.h"
#include <mqueue.h>
#include <semaphore.h>
#include <fcntl.h>
#include <signal.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <errno.h>
#include <unistd.h>

static const char* MQ_C2S = "/my_mq_c2s";
static const char* MQ_S2C = "/my_mq_s2c";
static const char* SEM_C2S = "/my_mq_sem_c2s";
static const char* SEM_S2C = "/my_mq_sem_s2c";

class ConnMQ : public Conn {
public:
    ConnMQ(const std::string& id, bool create) : Conn(id, create), mq_rd(-1), mq_wr(-1) {
        if (create) {
            sem_rd = sem_open(SEM_C2S, O_CREAT, 0644, 1);
            sem_wr = sem_open(SEM_S2C, O_CREAT, 0644, 1);
            
            struct mq_attr attr{};
            attr.mq_maxmsg  = 10;
            attr.mq_msgsize = sizeof(WireMessage);
            
            mq_unlink(MQ_C2S); mq_unlink(MQ_S2C);
            mq_rd = mq_open(MQ_C2S, O_CREAT | O_RDONLY | O_NONBLOCK, 0644, &attr);
            mq_wr = mq_open(MQ_S2C, O_CREAT | O_WRONLY | O_NONBLOCK, 0644, &attr);
        } else {
            sem_rd = sem_open(SEM_S2C, 0);
            sem_wr = sem_open(SEM_C2S, 0);
            mq_rd = mq_open(MQ_S2C, O_RDONLY | O_NONBLOCK);
            mq_wr = mq_open(MQ_C2S, O_WRONLY | O_NONBLOCK);
        }
    }
    ~ConnMQ() override {
        if (mq_rd != (mqd_t)-1) mq_close(mq_rd);
        if (mq_wr != (mqd_t)-1) mq_close(mq_wr);
        sem_close(sem_rd); sem_close(sem_wr);
        if (m_create) {
            mq_unlink(MQ_C2S); mq_unlink(MQ_S2C);
            sem_unlink(SEM_C2S); sem_unlink(SEM_S2C);
        }
    }
    
    bool Handshake() override {
        if (m_create) {
            WireMessage ping;
            
            while (true) {
                if (Read(&ping, sizeof(ping))) {
                    if (strcmp(ping.content, "ping") == 0) {
                        WireMessage ack = ping;
                        ack.type = WireMessage::ACK;
                        Write(&ack, sizeof(ack));
                        return true;
                    }
                }
            }
        }
        pid_t host = std::stoi(m_id);
        if (kill(host, SIGUSR1) == -1) { perror("kill"); return false; }
        
        WireMessage ping;
        ping.type = WireMessage::BROADCAST;
        strncpy(ping.sender, "client", sizeof(ping.sender)-1);
        strcpy(ping.content, "ping");
        Write(&ping, sizeof(ping));

        WireMessage ack;
        struct timespec start, now;
        clock_gettime(CLOCK_REALTIME, &start);
        
        while (true) {
            if (Read(&ack, sizeof(ack)) && ack.type == WireMessage::ACK) {
                return true;
            }
            clock_gettime(CLOCK_REALTIME, &now);
            if (now.tv_sec - start.tv_sec >= 5) break;
            usleep(100000);
        }
        return false;
    }
    
    bool Write(const void* buf, size_t count) override {
        struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); ts.tv_sec += 5;
        if (sem_timedwait(sem_wr, &ts) == -1) return false;
        int r = mq_send(mq_wr, static_cast<const char*>(buf), count, 0);
        sem_post(sem_wr);
        return r == 0;
    }
    
    bool Read(void* buf, size_t count) override {
        struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); ts.tv_sec += 5;
        if (sem_timedwait(sem_rd, &ts) == -1) return false;
        
        ssize_t n = mq_receive(mq_rd, static_cast<char*>(buf), count, nullptr);
        if (n == -1 && errno == EAGAIN) {
            sem_post(sem_rd);
            return false;
        }
        sem_post(sem_rd);
        return n > 0;
    }
private:
    mqd_t mq_rd, mq_wr;
    sem_t *sem_rd, *sem_wr;
};

Conn* createConn(const std::string& id, bool create) { return new ConnMQ(id, create); }