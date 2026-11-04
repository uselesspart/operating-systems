# Сборка и тесты проекта в контейнере (Ubuntu 24.04 + Qt6).
#   docker build -t ipc_chat .          # соберёт проект и прогонит ВСЕ тесты (при провале сборка упадёт)
#   docker run --rm ipc_chat            # повторно запустить тесты
# Запуск GUI из контейнера — см. README.md, раздел «Docker».
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake qt6-base-dev procps \
        libgl1 libxkbcommon-x11-0 libxcb-cursor0 libxcb-icccm4 libxcb-image0 \
        libxcb-keysyms1 libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 \
        fonts-dejavu-core \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . /app
RUN chmod +x build.sh run_tests.sh tests/smoke_test.sh \
    && ./run_tests.sh

CMD ["./run_tests.sh"]
