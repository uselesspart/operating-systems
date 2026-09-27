FROM ubuntu:24.04

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    rsyslog \
    inotify-tools \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

RUN echo "local0.* /var/log/disk_monitor.log" > /etc/rsyslog.d/10-disk-monitor.conf

CMD service rsyslog start && /bin/bash