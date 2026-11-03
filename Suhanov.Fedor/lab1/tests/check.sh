#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

if [[ $(uname -s) != Linux ]]; then
    echo "Проверка запускается только на Linux" >&2
    exit 1
fi

if [[ -n ${LOG_FILE:-} ]]; then
    if [[ ! -r $LOG_FILE ]]; then
        echo "Не могу прочитать журнал: $LOG_FILE" >&2
        exit 1
    fi
elif ! command -v journalctl >/dev/null; then
    echo "Укажите читаемый журнал: LOG_FILE=/путь/к/log ./tests/check.sh" >&2
    exit 1
fi

pid_file="/tmp/disk-monitor-$(id -u).pid"
if [[ -e $pid_file ]]; then
    echo "Уже есть PID-файл $pid_file. Остановите демон перед проверкой." >&2
    exit 1
fi

test_dir=$(mktemp -d /tmp/disk-monitor-check.XXXXXX)
config="$test_dir/config.conf"
first="$test_dir/first"
second="$test_dir/second"
pid=""
started_at=$(date '+%Y-%m-%d %H:%M:%S')

cleanup() {
    status=$?
    if ((status != 0)); then
        echo "Последние сообщения disk-monitor:" >&2
        logs | tail -n 10 >&2 || true
    fi
    if [[ -r $pid_file ]]; then
        current_pid=$(cat "$pid_file" 2>/dev/null || true)
        if [[ $current_pid =~ ^[0-9]+$ ]]; then
            current_exe=$(readlink -f "/proc/$current_pid/exe" 2>/dev/null || true)
            if [[ $current_exe == "$(readlink -f "$PWD/disk-monitor")" ]]; then
                kill -TERM "$current_pid" 2>/dev/null || true
            fi
        fi
    fi
    rm -rf -- "$test_dir"
}
trap cleanup EXIT

logs() {
    if [[ -n ${LOG_FILE:-} ]]; then
        cat "$LOG_FILE"
    else
        journalctl -t disk-monitor --since "$started_at" --no-pager 2>/dev/null
    fi
}

wait_for_log() {
    local expected=$1
    local line
    for ((i = 0; i < 50; ++i)); do
        if line=$(logs | grep -F -- "$expected" | tail -n 1); then
            echo "  журнал: $line"
            return 0
        fi
        sleep 0.1
    done
    echo "Не найдено сообщение в журнале: $expected" >&2
    return 1
}

wait_for_pid() {
    local previous=$1
    local current
    for ((i = 0; i < 50; ++i)); do
        if [[ -s $pid_file ]]; then
            current=$(cat "$pid_file")
            if [[ $current =~ ^[0-9]+$ && $current != "$previous" && -d /proc/$current ]]; then
                echo "$current"
                return 0
            fi
        fi
        sleep 0.1
    done
    echo "Новый PID-файл не появился" >&2
    return 1
}

mkdir "$first" "$second"
printf 'files = %s\n' "$first" > "$config"

echo "Каталог проверки: $test_dir"
echo "PID-файл: $pid_file"
if [[ -n ${LOG_FILE:-} ]]; then
    echo "Журнал: $LOG_FILE"
else
    echo "Журнал: journalctl -t disk-monitor"
fi

echo "[1/6] Сборка"
./build.sh

echo "[2/6] Запуск демона с конфигом $config"
./disk-monitor --config "$config"
pid=$(wait_for_pid "")
echo "  демон запущен, PID=$pid"
wait_for_log "daemon started, pid=$pid"

echo "[3/6] Создание файла в $first"
file="$first/created.txt"
printf 'test\n' > "$file"
wait_for_log "event=CREATE path=$file"

echo "[4/6] Перечитывание конфига по SIGHUP"
loaded_before=$(logs | grep -Fc -- "config loaded: $config" || true)
printf 'files = %s,%s\n' "$first" "$second" > "$config"
echo "  добавлен каталог $second"
kill -HUP "$pid"
loaded_after=$loaded_before
for ((i = 0; i < 50; ++i)); do
    loaded_after=$(logs | grep -Fc -- "config loaded: $config" || true)
    if ((loaded_after > loaded_before)); then
        break
    fi
    sleep 0.1
done
if ((loaded_after <= loaded_before)); then
    echo "Демон не перечитал конфиг" >&2
    exit 1
fi
echo "  конфиг перечитан"
echo "  журнал: $(logs | grep -F -- "config loaded: $config" | tail -n 1)"
file="$second/after-hup.txt"
printf 'test\n' > "$file"
wait_for_log "event=CREATE path=$file"

echo "[5/6] Повторный запуск"
old_pid=$pid
./disk-monitor --config "$config"
pid=$(wait_for_pid "$old_pid")
echo "  старый PID=$old_pid, новый PID=$pid"
wait_for_log "daemon started, pid=$pid"
if [[ -d /proc/$old_pid ]]; then
    echo "Старый демон ещё работает: $old_pid" >&2
    exit 1
fi

echo "[6/6] Остановка по SIGTERM"
kill -TERM "$pid"
for ((i = 0; i < 50; ++i)); do
    if [[ ! -e $pid_file && ! -d /proc/$pid ]]; then
        echo "  процесс завершился, PID-файл удалён"
        echo "Все проверки пройдены"
        exit 0
    fi
    sleep 0.1
done
echo "Демон не завершился или не удалил PID-файл" >&2
exit 1
