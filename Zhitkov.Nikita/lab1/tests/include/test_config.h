#pragma once

// Юнит-тесты для парсера config.ini (модуль Config).
// Каждая функция проверяет один сценарий и при провале assert'а
// аварийно завершает процесс.

void testValidConfig();
void testWhitespaceTolerance();
void testMissingEventsFile();
void testInvalidInterval();
void testNegativeInterval();
void testZeroInterval();
void testMissingFile();