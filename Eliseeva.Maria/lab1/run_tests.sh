# Запуск всех тестов в Docker: демоны, сигналы, /tmp и /dev/log не трогают систему.
#   ./run_tests.sh                 # все тесты
#   ./run_tests.sh -L unit         # только unit-тесты
#   ./run_tests.sh -L component    # только компонентные
#   ./run_tests.sh -R Sighup       # по имени
set -e
cd "$(dirname "$0")"

if [[ "${DIRCLEAN_IN_CONTAINER:-}" == "1" || -f /.dockerenv ]]; then
	export DIRCLEAN_IN_CONTAINER=1
	build_dir=/tmp/dirclean-tests-build
	cmake -S . -B "$build_dir" -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
	cmake --build "$build_dir" -j"$(nproc)"
	ctest --test-dir "$build_dir" --output-on-failure "$@"
	exit
fi

docker build -t dirclean-tests .
docker run --rm --init -e DIRCLEAN_IN_CONTAINER=1 dirclean-tests "$@"
