#include "check.hpp"

#include "chat/Hub.hpp"

#include <algorithm>

using namespace chatlab;

namespace {

constexpr Millis kStart = 1000000;
constexpr auto kSettle = std::chrono::milliseconds(400);

std::vector<std::string> texts(const std::vector<Post>& posts) {
    std::vector<std::string> result;
    for (const Post& post : posts) {
        result.push_back(post.text);
    }
    return result;
}

void seatTwo(Hub& hub) {
    hub.join(1, 101, "Алиса", kStart);
    hub.join(2, 102, "Борис", kStart);
}

}

TEST(hub_names_are_unique_and_never_empty) {
    Hub hub(kSettle);
    CHECK_EQ(hub.join(1, 11, "Анна", kStart), std::string("Анна"));
    const std::string second = hub.join(2, 12, "Анна", kStart);
    CHECK(second != "Анна");
    CHECK(unit::contains(second, "Анна"));
    CHECK(hub.join(3, 13, "Хост", kStart) != "Хост");
    CHECK_EQ(hub.join(4, 14, "   ", kStart), std::string("Участник 4"));
}

TEST(hub_roster_starts_with_host_and_changes_stamp) {
    Hub hub(kSettle);
    const auto before = hub.rosterStamp();
    hub.join(5, 55, "Вера", kStart);
    CHECK(hub.rosterStamp() != before);
    const std::vector<Member> expected{{0, "Хост"}, {5, "Вера"}};
    CHECK(hub.roster() == expected);
    hub.leave(5, "вышла");
    CHECK_EQ(hub.roster().size(), std::size_t(1));
}

TEST(hub_public_post_reaches_everybody_after_settle) {
    Hub hub(kSettle);
    seatTwo(hub);
    CHECK(hub.submit(1, {kEveryone, kStart + 10, "всем"}, kStart + 10).ok);

    CHECK(hub.release(kStart + 100).forHost.empty());
    CHECK(hub.take(2).posts.empty());

    const Released released = hub.release(kStart + 10 + 400);
    CHECK_EQ(texts(released.forHost), std::vector<std::string>{"всем"});
    CHECK_EQ(texts(hub.take(1).posts), std::vector<std::string>{"всем"});
    CHECK_EQ(texts(hub.take(2).posts), std::vector<std::string>{"всем"});
}

TEST(hub_private_post_only_for_sender_and_receiver) {
    Hub hub(kSettle);
    seatTwo(hub);
    hub.join(3, 103, "Вера", kStart);
    CHECK(hub.submit(1, {2, kStart, "секрет"}, kStart).ok);
    const Released released = hub.release(kStart + 1000);
    CHECK(released.forHost.empty());
    CHECK_EQ(released.relayed.size(), std::size_t(1));
    CHECK_EQ(texts(hub.take(1).posts), std::vector<std::string>{"секрет"});
    CHECK_EQ(texts(hub.take(2).posts), std::vector<std::string>{"секрет"});
    CHECK(hub.take(3).posts.empty());
}

TEST(hub_private_post_to_host_is_shown_to_host) {
    Hub hub(kSettle);
    seatTwo(hub);
    CHECK(hub.submit(2, {kHostId, kStart, "хосту"}, kStart).ok);
    CHECK(hub.submit(kHostId, {1, kStart, "от хоста"}, kStart).ok);
    const Released released = hub.release(kStart + 1000);
    CHECK_EQ(texts(released.forHost), (std::vector<std::string>{"хосту", "от хоста"}));
    CHECK(hub.take(1).posts.size() == 1);
    CHECK(hub.take(2).posts.size() == 1);
}

TEST(hub_rejects_bad_posts) {
    Hub hub(kSettle);
    seatTwo(hub);
    CHECK(!hub.submit(1, {9, kStart, "кому?"}, kStart).ok);
    CHECK(!hub.submit(1, {1, kStart, "себе"}, kStart).ok);
    CHECK(!hub.submit(1, {kEveryone, kStart, " \t "}, kStart).ok);
    CHECK(!hub.submit(7, {kEveryone, kStart, "чужой"}, kStart).ok);
    CHECK(!hub.submit(kHostId, {kHostId, kStart, "сам себе"}, kStart).ok);
}

TEST(hub_orders_by_sending_time_not_by_arrival) {
    Hub hub(kSettle);
    seatTwo(hub);
    CHECK(hub.submit(2, {kEveryone, kStart + 300, "второе"}, kStart + 310).ok);
    CHECK(hub.submit(1, {kEveryone, kStart + 100, "первое"}, kStart + 320).ok);
    CHECK(hub.submit(kHostId, {kEveryone, kStart + 200, "среднее"}, kStart + 330).ok);
    hub.release(kStart + 1000);
    CHECK_EQ(texts(hub.take(1).posts), (std::vector<std::string>{"первое", "среднее", "второе"}));
}

TEST(hub_late_post_never_goes_before_released_ones) {
    Hub hub(kSettle);
    seatTwo(hub);
    CHECK(hub.submit(1, {kEveryone, kStart + 500, "раньше выпущено"}, kStart + 500).ok);
    hub.release(kStart + 1000);
    const Accepted late = hub.submit(2, {kEveryone, kStart + 100, "опоздало"}, kStart + 1001);
    CHECK(late.ok);
    CHECK(late.post.sentAt >= kStart + 500);
    hub.release(kStart + 3000);
    CHECK_EQ(texts(hub.take(2).posts), (std::vector<std::string>{"раньше выпущено", "опоздало"}));
}

TEST(hub_future_timestamps_are_clamped) {
    Hub hub(kSettle);
    seatTwo(hub);
    const Accepted accepted = hub.submit(1, {kEveryone, kStart + 999999, "из будущего"}, kStart);
    CHECK(accepted.ok);
    CHECK_EQ(accepted.post.sentAt, kStart);
}

TEST(hub_detects_silent_members) {
    Hub hub(kSettle);
    seatTwo(hub);
    const auto limit = std::chrono::milliseconds(60000);
    CHECK(hub.silentFor(kStart + 59000, limit).empty());
    CHECK(hub.submit(1, {kEveryone, kStart + 30000, "живу"}, kStart + 30000).ok);
    const auto silent = hub.silentFor(kStart + 61000, limit);
    CHECK_EQ(silent.size(), std::size_t(1));
    CHECK_EQ(silent[0].id, 2);
    CHECK_EQ(silent[0].pid, pid_t(102));
}

TEST(hub_join_and_leave_notes_go_to_others) {
    Hub hub(kSettle);
    seatTwo(hub);
    CHECK(hub.take(1).notes.size() == 1);
    hub.leave(2, "вышел");
    const Batch batch = hub.take(1);
    CHECK_EQ(batch.notes.size(), std::size_t(1));
    CHECK(unit::contains(batch.notes[0], "Борис"));
    CHECK(unit::contains(batch.notes[0], "вышел"));
    CHECK(!hub.isMember(2));
}

TEST(hub_take_respects_batch_limit) {
    Hub hub(kSettle);
    seatTwo(hub);
    for (std::size_t index = 0; index < kPostsPerReply + 5; ++index) {
        CHECK(hub.submit(1, {kEveryone, kStart, "m" + std::to_string(index)}, kStart).ok);
    }
    hub.release(kStart + 1000);
    CHECK_EQ(hub.take(2).posts.size(), kPostsPerReply);
    CHECK_EQ(hub.take(2).posts.size(), std::size_t(5));
}

TEST(hub_post_for_departed_member_is_dropped_safely) {
    Hub hub(kSettle);
    seatTwo(hub);
    CHECK(hub.submit(1, {2, kStart, "не успел"}, kStart).ok);
    hub.leave(2, "ушёл");
    hub.release(kStart + 1000);
    CHECK_EQ(hub.take(1).posts.size(), std::size_t(1));
    CHECK(hub.take(2).posts.empty());
}
