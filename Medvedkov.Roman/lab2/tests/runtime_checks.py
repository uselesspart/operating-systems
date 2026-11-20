#!/usr/bin/env python3
"""Run the actual GUI executables; do not shorten the required 60-second limit."""
import glob
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent


def children(pid):
    try:
        return [int(value) for value in Path(f"/proc/{pid}/task/{pid}/children").read_text().split()]
    except FileNotFoundError:
        return []


def resources():
    return set(glob.glob("/tmp/roman-chat-*")) | set(glob.glob("/dev/mqueue/roman-chat-*"))


def check_idle_clients():
    baseline = resources()
    running = []
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen")
    try:
        for code in ("mq", "fifo", "sock"):
            log = tempfile.TemporaryFile(mode="w+t", encoding="utf-8")
            process = subprocess.Popen([str(ROOT / f"host_{code}")], env=env, stdout=log, stderr=log)
            running.append((code, process, log, time.monotonic()))
        deadline = time.monotonic() + 8
        while any(len(children(process.pid)) != 1 for _, process, _, _ in running):
            if time.monotonic() > deadline:
                raise AssertionError("Each host must create exactly one child")
            time.sleep(0.05)
        client_pids = {process.pid: children(process.pid)[0] for _, process, _, _ in running}
        # Prove the idle-user exception: the child must survive ordinary idle time.
        time.sleep(6)
        for code, process, _, _ in running:
            assert process.poll() is None and Path(f"/proc/{client_pids[process.pid]}").exists(), code
        print("PASS: all clients survive 6 seconds of waiting for user input", flush=True)
        deadline = min(start for _, _, _, start in running) + 68
        while any(Path(f"/proc/{client_pids[process.pid]}").exists() for _, process, _, _ in running):
            if time.monotonic() > deadline:
                raise AssertionError("Idle child was not SIGKILLed and reaped after 60 seconds")
            time.sleep(0.1)
        for code, process, log, start in running:
            assert process.poll() is None, f"Host {code} must remain open to show status"
            log.seek(0)
            contents = log.read()
            assert "Client idle for more than 60 seconds: SIGKILL" in contents, contents
            assert time.monotonic() - start >= 60, "No premature client kill"
            print(f"PASS: {code}, real 60-second idle timeout, SIGKILL, child reaped, host alive", flush=True)
    finally:
        for _, process, log, _ in running:
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)
                try:
                    process.wait(timeout=7)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=3)
            log.close()
    assert resources() == baseline, "IPC resources leaked after host shutdown"
    print("PASS: resources removed after closing all hosts", flush=True)


if __name__ == "__main__":
    check_idle_clients()
