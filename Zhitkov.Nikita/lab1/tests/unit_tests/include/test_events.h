#pragma once

// Юнит-тесты для модуля Event:
//  - parseEventLine / repeatSeconds;
//  - decideEventAction.

void testParseEventLine();
void testOneTimeEvent();
void testHourlyEvent();
void testDailyEvent();
void testWeeklyEvent();
void testRecurringScheduleAlignment();
void testGraceVariants();