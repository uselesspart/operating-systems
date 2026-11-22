// Правила игры "Волк и семеро козлят" - чистые функции без ввода-вывода (удобно тестировать).
#pragma once

namespace rules {

constexpr int kMinNumber = 1;
constexpr int kMaxWolfNumber = 100;
constexpr int kMaxAliveKidNumber = 100;
constexpr int kMaxDeadKidNumber = 50;
constexpr int kAllDeadRoundsToFinish = 2;  // игра заканчивается, если 2 хода подряд все козлята мертвы

// Допустимое расстояние до числа волка, при котором живой козлёнок прячется (70/n)
double HideDistance(int kids);
// Допустимое расстояние, при котором мёртвый козлёнок воскресает (20/n)
double ReviveDistance(int kids);

// Живые бросают 1..100, мёртвые 1..50
bool IsValidKidNumber(bool alive, int number);

enum class Outcome { Hidden, Caught, Revived, StayedDead };

// kids - число козлят (n) в этом раунде
Outcome Judge(bool alive, int kidNumber, int wolfNumber, int kids);

bool OutcomeIsAlive(Outcome o);       // жив ли козлёнок после раунда
const char* OutcomeName(Outcome o);   // "спрятался", "попался", "воскрес", "остался мертвым"

}  // namespace rules
