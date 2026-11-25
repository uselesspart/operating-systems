#include "conn.h"
#include <iostream>
#include <cstdio>
#include <cstring>
#include <string>
#include <csignal>
#include <chrono>
#include <thread>
#include <atomic>

static std::atomic<bool> g_running{true};
static void on_signal(int) { g_running = false; }

int main(int argc, char* argv[]) {
    if (argc < 2) { fprintf(stderr, "Usage: %s <host_pid>\n", argv[0]); return 1; }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    Conn* conn = createConn(argv[1], false);
    if (!conn) { fprintf(stderr, "createConn failed\n"); return 1; }

    if (!conn->Handshake()) {
        fprintf(stderr, "Handshake failed\n");
        delete conn;
        return 1;
    }
    printf("Connected. Type message (empty line to quit).\n");

    // Поток для асинхронного чтения
    std::thread reader([&]() {
        while (g_running) {
            WireMessage wm;
            if (conn->Read(&wm, sizeof(wm))) {
                if (wm.type == WireMessage::BROADCAST || wm.type == WireMessage::PRIVATE) {
                    printf("\n[%s] %s: %s\n> ", wm.recipient[0] ? "private" : "broadcast", wm.sender, wm.content);
                    fflush(stdout);
                }
            }
        }
    });

    std::string line;
    while (g_running && std::getline(std::cin, line)) {
        if (line.empty()) break;

        WireMessage msg;
        msg.type = WireMessage::BROADCAST;
        strncpy(msg.sender, "client", sizeof(msg.sender)-1);
        strncpy(msg.content, line.c_str(), sizeof(msg.content)-1);
        msg.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        if (!conn->Write(&msg, sizeof(msg))) {
            fprintf(stderr, "Write failed\n");
            break;
        }
    }

    g_running = false;
    reader.detach();
    delete conn;
    return 0;
}