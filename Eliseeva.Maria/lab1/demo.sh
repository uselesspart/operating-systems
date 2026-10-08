# Демо: создаёт тестовые папки из daemon.conf, запускает демон и показывает результат.
# Запускать в Linux (на Mac - внутри контейнера, см. README).
set -e
cd "$(dirname "$0")"

[ -x ./daemon ] || ./build.sh

mkdir -p /tmp/test_a /tmp/test_b/sub
touch /tmp/test_a/keep.txt /tmp/test_a/file1 /tmp/test_b/file2 /tmp/test_b/sub/file3

./daemon daemon.conf
sleep 2

echo "/tmp/test_a (есть keep.txt) - должна остаться нетронутой:"
ls -A /tmp/test_a
echo
echo "/tmp/test_b (нет .ignore) - должна быть пустой:"
ls -A /tmp/test_b
echo
echo "pid демона: $(cat /tmp/dirclean_daemon.pid)"
echo "Остановить: kill -TERM \$(cat /tmp/dirclean_daemon.pid)"
