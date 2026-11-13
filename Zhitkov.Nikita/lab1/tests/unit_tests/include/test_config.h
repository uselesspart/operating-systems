#pragma once

// Юнит-тесты для парсера reminder.conf (модуль Config)
// Каждая функция проверяет один сценарий и при провале assert аварийно завершает процесс

void testValidConfig();
void testWhitespaceTolerance();
void testMissingEventsFile();
void testInvalidInterval();
void testNegativeInterval();
void testZeroInterval();
void testMissingFile();