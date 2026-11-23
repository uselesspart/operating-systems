#pragma once

namespace rules {

enum class Outcome {
    kHidden,
    kCaught,
    kRevived,
    kStillDead
};


Outcome Judge(bool kidAlive, int kidNumber, int wolfNumber);

bool AliveAfter(Outcome outcome);

const char* OutcomeName(Outcome outcome);

class DeadStreak {
public:
    bool Update(bool kidAlive);

private:
    int count_ = 0;
};

}
