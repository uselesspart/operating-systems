#pragma once
#include "Conn.h"
#include "SharedSems.h"

void runClientLoop(Conn* conn, SharedSems* sems);