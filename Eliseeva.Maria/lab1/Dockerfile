FROM ubuntu:24.04

RUN apt-get update \
    && apt-get install -y --no-install-recommends g++ make cmake libgtest-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

# Компонентные тесты запускаются только в контейнере: они подменяют /dev/log
# и запускают настоящие процессы-демоны.
ENV DIRCLEAN_IN_CONTAINER=1

RUN cmake -S . -B /build -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug \
    && cmake --build /build -j"$(nproc)"

# Аргументы docker run передаются в ctest (например, -L unit или -R Sighup).
ENTRYPOINT ["ctest", "--test-dir", "/build", "--output-on-failure"]
