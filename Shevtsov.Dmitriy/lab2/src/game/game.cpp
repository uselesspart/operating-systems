#include "game.h"

#include <algorithm>
#include <cstdlib>
#include <random>
#include <stdexcept>

namespace game {

	int roll(int max){
		thread_local std::mt19937 engine{std::random_device{}()};
		return std::uniform_int_distribution<int>(1, max)(engine);
	}

	Game::Game(int goats) : alive(goats, true){}

	RoundResult Game::playRound(int wolf, const std::vector<int>& goats){
		if (goats.size() != alive.size()) {
			throw std::invalid_argument("expected one number per goat");
		}

		const double count = alive.size();
		RoundResult result;
		for (size_t i = 0; i < goats.size(); i++) {
			int distance = std::abs(goats[i] - wolf);
			if (alive[i] && distance <= HIDE_RANGE / count) {
				result.hidden++;
			} else if (alive[i]) {
				alive[i] = false;
				result.caught++;
			} else if (distance <= RESURRECT_RANGE / count) {
				alive[i] = true;
				result.resurrected++;
			}
		}

		result.dead = std::count(alive.begin(), alive.end(), false);
		if (result.dead == static_cast<int>(alive.size())) {
			deadRounds++;
		} else {
			deadRounds = 0;
		}
		return result;
	}

	bool Game::isAlive(int goat) const{
		return alive.at(goat);
	}

	bool Game::isOver() const{
		return deadRounds >= DEAD_ROUNDS_TO_FINISH;
	}

}
