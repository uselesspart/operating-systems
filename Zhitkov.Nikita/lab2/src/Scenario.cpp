#include "Scenario.h"
#include <cstdlib>

Scenario::Scenario(int kid_count) : kid_count_(kid_count) {}

RoundResult Scenario::playRound(int wolf_num, int kid_num) {
    round_++;
    const int hide_threshold = 70 / kid_count_;
    const int res_threshold  = 20 / kid_count_;
    const int diff = std::abs(wolf_num - kid_num);

    RoundResult::Status status;
    if (kid_alive_) {
        if (diff <= hide_threshold) { status = RoundResult::Status::Hid;         hid_++; }
        else                        { status = RoundResult::Status::Caught;      caught_++; kid_alive_ = false; }
    } else {
        if (diff <= res_threshold)  { status = RoundResult::Status::Resurrected; resurrected_++; kid_alive_ = true; }
        else                        { status = RoundResult::Status::Dead; }
    }

    if (!kid_alive_) {
        consecutive_dead_++;
        if (consecutive_dead_ >= 2) game_over_ = true;
    } else {
        consecutive_dead_ = 0;
    }

    return RoundResult{ round_, wolf_num, kid_num, status, kid_alive_, game_over_ };
}