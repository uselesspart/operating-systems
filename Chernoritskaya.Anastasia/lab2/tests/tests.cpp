#include <gtest/gtest.h>
#include "chat_logic.h"
#include <algorithm>
#include <chrono>

using namespace std::chrono;


TEST(ChatLogic, AddRemove) {
    ChatLogic l;
    l.addParticipant("a"); l.addParticipant("b"); l.removeParticipant("a");
    ChatMessage m{"b", "", "hi", system_clock::now()};
    EXPECT_TRUE(l.routeMessage(m).empty());
}

TEST(ChatLogic, BroadcastExcludesSender) {
    ChatLogic l;
    l.addParticipant("a"); l.addParticipant("b"); l.addParticipant("c");
    ChatMessage m{"a", "", "hi", system_clock::now()};
    auto t = l.routeMessage(m);
    EXPECT_EQ(t.size(), 2u);
    EXPECT_NE(std::find(t.begin(), t.end(), "b"), t.end());
    EXPECT_NE(std::find(t.begin(), t.end(), "c"), t.end());
}

TEST(ChatLogic, PrivateValid) {
    ChatLogic l;
    l.addParticipant("a"); l.addParticipant("b");
    ChatMessage m{"a", "b", "s", system_clock::now()};
    auto t = l.routeMessage(m);
    ASSERT_EQ(t.size(), 1u);
    EXPECT_EQ(t[0], "b");
}

TEST(ChatLogic, PrivateUnknown) {
    ChatLogic l;
    l.addParticipant("a");
    ChatMessage m{"a", "z", "?", system_clock::now()};
    EXPECT_TRUE(l.routeMessage(m).empty());
}

TEST(ChatLogic, Timeout60s) {
    ChatLogic l;
    auto t0 = system_clock::now();
    l.addParticipant("a"); l.updateActivity("a", t0);
    EXPECT_FALSE(l.getTimedOutParticipants(t0 + seconds(59)).size());
    EXPECT_EQ(l.getTimedOutParticipants(t0 + seconds(61)).size(), 1u);
    l.updateActivity("a", t0 + seconds(61));
    EXPECT_FALSE(l.getTimedOutParticipants(t0 + seconds(91)).size());
}

TEST(ChatLogic, HostNeverTimesOut) {
    ChatLogic l;
    auto t0 = system_clock::now();
    l.addParticipant("host");
    l.updateActivity("host", t0);
    
    auto t1 = t0 + seconds(1000);
    EXPECT_EQ(l.getTimedOutParticipants(t1).size(), 0u);
}

TEST(ChatLogic, RemoveNonExistent) {
    ChatLogic l;
    l.addParticipant("a");
    l.removeParticipant("ghost"); // нет такого
    EXPECT_EQ(l.participants().size(), 1u); // "a" остался
}

TEST(ChatLogic, AddDuplicateIgnored) {
    ChatLogic l;
    l.addParticipant("a");
    l.addParticipant("a");
    EXPECT_EQ(l.participants().size(), 1u);
}

TEST(ChatLogic, PrivateToSelf) {
    ChatLogic l;
    l.addParticipant("a");
    ChatMessage m{"a", "a", "self", system_clock::now()};
    auto t = l.routeMessage(m);
    ASSERT_EQ(t.size(), 1u);
    EXPECT_EQ(t[0], "a");
}

TEST(ChatLogic, MultipleTimeouts) {
    ChatLogic l;
    auto t0 = system_clock::now();
    l.addParticipant("a"); l.addParticipant("b");
    l.updateActivity("a", t0);
    l.updateActivity("b", t0);
    
    auto t1 = t0 + seconds(61);
    auto timedOut = l.getTimedOutParticipants(t1);
    EXPECT_EQ(timedOut.size(), 2u);
    
    // Удаляем одного, второй должен остаться в списке
    l.removeParticipant("a");
    auto timedOut2 = l.getTimedOutParticipants(t1);
    EXPECT_EQ(timedOut2.size(), 1u);
    EXPECT_EQ(timedOut2[0], "b");
}

TEST(ChatLogic, BroadcastEmptyChat) {
    ChatLogic l;
    l.addParticipant("host");
    ChatMessage m{"host", "", "hello", system_clock::now()};
    auto t = l.routeMessage(m);
    EXPECT_TRUE(t.empty()); // Некому отправлять
}