#!/bin/bash

TEST_DIR="/home/bi/test_folder"

# ТЕСТ 1: Глубина 2 (проверка сохранения глубоких файлов)
echo -e "\n[TEST 1] Depth = 2"
rm -rf $TEST_DIR
mkdir -p $TEST_DIR/sub1/sub2/sub3
touch $TEST_DIR/sub1/sub2/sub3/important.txt
touch $TEST_DIR/sub1/sub2/file_at_depth2.txt
mkdir -p $TEST_DIR/other
touch $TEST_DIR/root_file.txt

echo -e "interval 2\n$TEST_DIR 2" > test_config.txt
./lab1_daemon test_config.txt
sleep 3

echo -e "\nAfter daemon run:"
ls -R $TEST_DIR

# sub1 остается. 
# sub2 остается (но файлы в ней удалены).
# sub3 остается, и important.txt внутри sub3 жив
# root_file.txt остается.
if [ -d "$TEST_DIR/sub1/sub2" ] && [ -d "$TEST_DIR/sub1/sub2/sub3" ] && \
   [ -f "$TEST_DIR/sub1/sub2/sub3/important.txt" ] && \
   [ ! -f "$TEST_DIR/sub1/sub2/file_at_depth2.txt" ] && [ -f "$TEST_DIR/root_file.txt" ]; then
    echo -e ">> TEST 1: PASSED (deep files kept, depth2 files deleted)\n"
else
    echo -e ">> TEST 1: FAILED\n"
fi
kill $(cat /tmp/lab1_daemon.pid) 2>/dev/null
sleep 1

# ТЕСТ 2: Посторонний PID
echo -e "\n[TEST 2] Foreign PID protection"
sleep 100 & 
FOREIGN_PID=$!
echo $FOREIGN_PID > /tmp/lab1_daemon.pid

./lab1_daemon test_config.txt
sleep 1

# Проверяем, что посторонний процесс жив
if kill -0 $FOREIGN_PID 2>/dev/null; then
    echo -e ">> TEST 2: PASSED (foreign process not killed)\n"
else
    echo -e ">> TEST 2: FAILED (foreign process was killed!)\n"
fi
kill $FOREIGN_PID 2>/dev/null

kill $(cat /tmp/lab1_daemon.pid) 2>/dev/null
rm -rf $TEST_DIR
rm -f test_config.txt