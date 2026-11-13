#!/bin/sh
# "Поддельный терминал" для автотестов: вместо окна выполняет скрипт напоминания
# и дописывает его вывод в файл $FAKE_TERMINAL_OUT.
sh "$1" < /dev/null >> "${FAKE_TERMINAL_OUT:-/tmp/fake_terminal.out}" 2>&1
