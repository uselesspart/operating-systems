#pragma once

#include <vector>

namespace game {

	constexpr int MAX_NUMBER = 100;
	constexpr int DEAD_MAX_NUMBER = 50;
	constexpr double HIDE_RANGE = 70.0;
	constexpr double RESURRECT_RANGE = 20.0;
	constexpr int DEAD_ROUNDS_TO_FINISH = 2;

	struct RoundResult {
		int hidden = 0;
		int caught = 0;
		int resurrected = 0;
		int dead = 0;
	};

	int roll(int max);

	class Game {
	public:
		explicit Game(int goats);

		RoundResult playRound(int wolf, const std::vector<int>& goats);
		bool isAlive(int goat) const;
		bool isOver() const;

	private:
		std::vector<bool> alive;
		int deadRounds = 0;
	};

}
