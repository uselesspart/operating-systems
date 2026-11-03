#pragma once

struct RoundResult {
    enum class Status { Hid, Caught, Resurrected, Dead };
    int    round;
    int    wolf_num;
    int    kid_num;
    Status kid_status;
    bool   kid_alive_after;
    bool   game_over;
};

class Scenario {
public:
    explicit Scenario(int kid_count = 1);

    RoundResult playRound(int wolf_num, int kid_num);

    int  round()            const { return round_; }
    bool kidAlive()         const { return kid_alive_; }
    bool gameOver()         const { return game_over_; }
    int  totalHid()         const { return hid_; }
    int  totalCaught()      const { return caught_; }
    int  totalResurrected() const { return resurrected_; }

private:
    int  kid_count_;
    int  round_            = 0;
    bool kid_alive_        = true;
    int  consecutive_dead_ = 0;
    int  hid_ = 0, caught_ = 0, resurrected_ = 0;
    bool game_over_        = false;
};