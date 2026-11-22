#ifndef CHAT_TESTS_THROWS_H
#define CHAT_TESTS_THROWS_H

namespace chat::testing {

// Portable replacement for QVERIFY_EXCEPTION_THROWN (deprecated in Qt 6, its successor is missing in Qt 5)
template <typename Exception, typename Call>
bool throws(Call call)
{
    try {
        call();
    } catch (const Exception&) {
        return true;
    }
    return false;
}

}

#endif // CHAT_TESTS_THROWS_H
