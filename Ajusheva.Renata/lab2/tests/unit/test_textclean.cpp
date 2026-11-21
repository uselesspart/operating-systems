#include "check.hpp"

#include "chat/Limits.hpp"
#include "chat/TextClean.hpp"

using namespace chatlab;

TEST(textclean_keeps_normal_text) {
    CHECK_EQ(textclean::cleanText("Привет, мир! 123"), std::string("Привет, мир! 123"));
    CHECK_EQ(textclean::cleanText("emoji 😀 ok"), std::string("emoji 😀 ok"));
}

TEST(textclean_replaces_control_characters_and_trims) {
    CHECK_EQ(textclean::cleanText("  a\tb\rc\x7f "), std::string("a b c"));
    CHECK_EQ(textclean::cleanText("\x01\x02"), std::string(""));
}

TEST(textclean_drops_escape_sequences) {
    CHECK_EQ(textclean::cleanText("до\x1b[31mкрасный\x1b[0m"), std::string("докрасный"));
    CHECK_EQ(textclean::cleanText("стрелка\x1b[A"), std::string("стрелка"));
    CHECK_EQ(textclean::cleanText("a\x1b" "cb"), std::string("ab"));
}

TEST(textclean_drops_broken_utf8) {
    CHECK_EQ(textclean::cleanText("при\xd0"), std::string("при"));
    CHECK_EQ(textclean::cleanText("a\xff\xfe" "b"), std::string("ab"));
    CHECK_EQ(textclean::cleanText("\xc0\xaf"), std::string(""));
    CHECK_EQ(textclean::cleanText("\xed\xa0\x80x"), std::string("x"));
}

TEST(textclean_cut_keeps_whole_letters) {
    CHECK_EQ(textclean::cutUtf8("абв", 3), std::string("а"));
    CHECK_EQ(textclean::cutUtf8("абв", 4), std::string("аб"));
    CHECK_EQ(textclean::cutUtf8("abc", 10), std::string("abc"));
}

TEST(textclean_limits_text_and_name) {
    CHECK_EQ(textclean::cleanText(std::string(kTextLimit + 50, 'x')).size(), kTextLimit);
    const std::string name = textclean::cleanName(std::string(40, 'q'));
    CHECK_EQ(name.size(), kNameLimit);
    CHECK_EQ(textclean::cleanName("  Анна  "), std::string("Анна"));
}
