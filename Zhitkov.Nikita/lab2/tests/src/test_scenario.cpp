#include "test_scenario.h"
#include "Scenario.h"
#include <cassert>

void testScenario() {
    // hid
    {
        Scenario s(1);
        auto r = s.playRound(10, 30);   // |20| <= 70
        assert(r.kid_status == RoundResult::Status::Hid);
        assert(r.kid_alive_after);
    }
    // caught
    {
        Scenario s(1);
        auto r = s.playRound(10, 100);  // |90| > 70
        assert(r.kid_status == RoundResult::Status::Caught);
        assert(!r.kid_alive_after);
    }
    // resurrect
    {
        Scenario s(1);
        s.playRound(10, 100);            // caught
        auto r = s.playRound(10, 30);    // |20| <= 20 -> resurrect
        assert(r.kid_status == RoundResult::Status::Resurrected);
        assert(r.kid_alive_after);
    }
    // граница hid: diff == 70
    {
        Scenario s(1);
        auto r = s.playRound(30, 100);   // |70| -> hid (<=)
        assert(r.kid_status == RoundResult::Status::Hid);
    }
    // граница hid: diff == 71
    {
        Scenario s(1);
        auto r = s.playRound(29, 100);   // |71| -> caught
        assert(r.kid_status == RoundResult::Status::Caught);
    }
    // game over после 2 мёртвых раундов подряд
    {
        Scenario s(1);
        s.playRound(10, 100);
        assert(!s.gameOver());
        s.playRound(10, 100);
        assert(s.gameOver());
    }
    // воскрешение сбрасывает счётчик
    {
        Scenario s(1);
        s.playRound(10, 100);            // dead, counter 1
        s.playRound(10, 10);             // resurrect, counter 0
        s.playRound(10, 100);            // dead, counter 1
        assert(!s.gameOver());
    }
    // счётчики
    {
        Scenario s(1);
        s.playRound(10, 30);             // hid
        s.playRound(10, 100);            // caught
        s.playRound(10, 10);             // resurrect
        assert(s.totalHid() == 1);
        assert(s.totalCaught() == 1);
        assert(s.totalResurrected() == 1);
    }
}