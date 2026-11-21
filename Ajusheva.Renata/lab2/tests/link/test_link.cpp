#include "check.hpp"

#include "conn/conn.hpp"
#include "link/Link.hpp"
#include "link/WorkDir.hpp"

#include <chrono>
#include <csignal>
#include <memory>
#include <string>
#include <system_error>

#include <sys/wait.h>
#include <unistd.h>

using namespace chatlab;
using namespace std::chrono_literals;

namespace {

Bytes bytesOf(const std::string& text) {
    return Bytes(text.begin(), text.end());
}

std::string textOf(const Bytes& bytes) {
    return std::string(bytes.begin(), bytes.end());
}

int childExit(pid_t child) {
    int status = 0;
    ::waitpid(child, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

template <typename Body>
pid_t spawnGuest(int slot, Body body) {
    const pid_t host = ::getpid();
    const pid_t child = ::fork();
    if (child == 0) {
        int code = 0;
        try {
            Link link(ConnKey{host, slot}, Link::Side::Guest);
            code = body(link);
        } catch (const std::exception&) {
            code = 90;
        }
        ::_exit(code);
    }
    return child;
}

struct Fixture {
    Fixture() : dir(::getpid()) { ::signal(SIGPIPE, SIG_IGN); }
    WorkDir dir;
};

}

TEST(link_kind_matches_build) {
    CHECK_EQ(std::string(Conn::Kind()), std::string(LAB2_KIND));
}

TEST(link_request_reply_cycles) {
    Fixture fixture;
    Link host(ConnKey{::getpid(), 1}, Link::Side::Host);
    const pid_t child = spawnGuest(1, [](Link& link) {
        for (int round = 0; round < 50; ++round) {
            if (link.send(bytesOf("ping " + std::to_string(round))) != LinkStatus::Ok) {
                return 1;
            }
            Bytes reply;
            if (link.receive(reply, 5000ms) != LinkStatus::Ok ||
                textOf(reply) != "pong " + std::to_string(round)) {
                return 2;
            }
        }
        return 0;
    });
    for (int round = 0; round < 50; ++round) {
        Bytes request;
        CHECK(host.receive(request, 5000ms) == LinkStatus::Ok);
        CHECK_EQ(textOf(request), "ping " + std::to_string(round));
        CHECK(host.send(bytesOf("pong " + std::to_string(round))) == LinkStatus::Ok);
    }
    CHECK_EQ(childExit(child), 0);
}

TEST(link_carries_large_and_empty_frames) {
    Fixture fixture;
    Link host(ConnKey{::getpid(), 2}, Link::Side::Host);
    const std::string big(Link::kFrameLimit, 'q');
    const pid_t child = spawnGuest(2, [&big](Link& link) {
        if (link.send(bytesOf(big)) != LinkStatus::Ok) {
            return 1;
        }
        Bytes reply;
        if (link.receive(reply, 5000ms) != LinkStatus::Ok || !reply.empty()) {
            return 2;
        }
        return 0;
    });
    Bytes request;
    CHECK(host.receive(request, 5000ms) == LinkStatus::Ok);
    CHECK_EQ(request.size(), big.size());
    CHECK(host.send(Bytes{}) == LinkStatus::Ok);
    CHECK_EQ(childExit(child), 0);
}

TEST(link_rejects_oversized_frame) {
    Fixture fixture;
    Link host(ConnKey{::getpid(), 3}, Link::Side::Host);
    CHECK(host.send(Bytes(Link::kFrameLimit + 1, 0)) == LinkStatus::Malformed);
}

TEST(link_silent_peer_gives_timeout) {
    Fixture fixture;
    Link host(ConnKey{::getpid(), 4}, Link::Side::Host);
    const auto started = std::chrono::steady_clock::now();
    Bytes request;
    CHECK(host.receive(request, 300ms) == LinkStatus::TimedOut);
    CHECK(std::chrono::steady_clock::now() - started >= 280ms);
}

TEST(link_send_to_departed_peer_breaks) {
    Fixture fixture;
    Link host(ConnKey{::getpid(), 5}, Link::Side::Host);
    const pid_t child = spawnGuest(5, [](Link& link) {
        Bytes reply;
        const bool ok = link.send(bytesOf("hello")) == LinkStatus::Ok &&
                        link.receive(reply, 5000ms) == LinkStatus::Ok;
        return ok ? 0 : 1;
    });
    Bytes request;
    CHECK(host.receive(request, 5000ms) == LinkStatus::Ok);
    CHECK(host.send(bytesOf("bye")) == LinkStatus::Ok);
    CHECK_EQ(childExit(child), 0);
    CHECK(host.receive(request, 300ms) == LinkStatus::TimedOut);
    CHECK(host.send(bytesOf("anyone?")) == LinkStatus::Broken);
}

TEST(link_guest_without_host_channel_fails) {
    Fixture fixture;
    CHECK_THROWS(Link(ConnKey{::getpid(), 77}, Link::Side::Guest), std::system_error);
}

TEST(link_host_removes_its_semaphores) {
    Fixture fixture;
    const std::string semFile = "/dev/shm/sem.lab2chat-" + std::to_string(::getpid()) + "-c6-up";
    {
        Link host(ConnKey{::getpid(), 6}, Link::Side::Host);
        CHECK(::access(semFile.c_str(), F_OK) == 0);
    }
    CHECK(::access(semFile.c_str(), F_OK) != 0);
}
