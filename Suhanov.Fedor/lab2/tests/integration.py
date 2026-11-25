#!/usr/bin/env python3
"""Exercise independent Qt applications in an X11 session (or xvfb-run)."""
import argparse
import ctypes
import os
from pathlib import Path
import re
import signal
import subprocess
import threading
import time

clipboard_owner = None


def xdo(*arguments):
    return subprocess.check_output(
        ["xdotool", *map(str, arguments)], text=True, stderr=subprocess.DEVNULL, timeout=3
    ).strip()


def window_for(pid):
    result = subprocess.run(
        ["xdotool", "search", "--onlyvisible", "--pid", str(pid)],
        capture_output=True, text=True, timeout=3,
    )
    return result.stdout.splitlines()[0] if result.returncode == 0 else None


def read_clipboard():
    try:
        result = subprocess.run(
            ["xclip", "-selection", "clipboard", "-out", "-target", "UTF8_STRING"],
            capture_output=True, encoding="utf-8", timeout=1,
        )
        return result.stdout if result.returncode == 0 else None
    except subprocess.TimeoutExpired:
        return None


def stop_clipboard_owner():
    global clipboard_owner
    if clipboard_owner is not None:
        if clipboard_owner.poll() is None:
            clipboard_owner.terminate()
        clipboard_owner.wait(timeout=3)
        clipboard_owner = None


def set_clipboard(text):
    global clipboard_owner
    stop_clipboard_owner()
    clipboard_owner = subprocess.Popen(
        ["xclip", "-quiet", "-selection", "clipboard", "-in", "-target", "UTF8_STRING"],
        stdin=subprocess.PIPE, encoding="utf-8", stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    clipboard_owner.stdin.write(text)
    clipboard_owner.stdin.close()
    eventually(lambda: read_clipboard() == text, timeout=3)


def eventually(predicate, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.05)
    assert predicate(), "Condition did not become true before deadline"


class Process:
    def __init__(self, executable, *arguments):
        self.process = subprocess.Popen(
            [str(executable), *map(str, arguments)], stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
            encoding="utf-8", bufsize=1,
        )
        self.errors = []
        self.condition = threading.Condition()
        self.last_history = ""
        self.reader = threading.Thread(target=self._read_errors, daemon=True)
        self.reader.start()

    @property
    def pid(self):
        return self.process.pid

    def _read_errors(self):
        for line in self.process.stderr:
            with self.condition:
                self.errors.append(line.rstrip())
                self.condition.notify_all()

    def focus(self):
        window = window_for(self.pid)
        assert window, f"Window unavailable for PID {self.pid}: {self.errors}"
        xdo("windowraise", window)
        xdo("windowfocus", "--sync", window)

    def history(self):
        self.focus()
        set_clipboard("")
        xdo("key", "--clearmodifiers", "alt+h", "ctrl+a", "ctrl+c")
        self.last_history = read_clipboard() or ""
        return self.last_history

    def wait(self, text, timeout=10):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                break
            if window_for(self.pid) and text in self.history():
                return self.last_history
            time.sleep(0.05)
        raise AssertionError(f"PID {self.pid}: missing {text!r}\n{self.last_history}\n{self.errors}")

    def wait_error(self, text, timeout=10):
        deadline = time.monotonic() + timeout
        with self.condition:
            while not any(text in line for line in self.errors):
                remaining = deadline - time.monotonic()
                assert remaining > 0, f"PID {self.pid}: missing error {text!r}: {self.errors}"
                self.condition.wait(min(remaining, 0.1))

    def send(self, text, recipient=0):
        index = 0
        if recipient:
            roster = {}
            for line in self.history().splitlines():
                joined = re.fullmatch(r"\[Подключение\] (.+), PID (\d+)", line)
                left = re.match(r"\[Отключение\] (.+?): ", line)
                if joined:
                    roster[joined[1]] = int(joined[2])
                elif left:
                    roster.pop(left[1], None)
            index = list(roster.values()).index(recipient) + 1
        self.focus()
        xdo("key", "--clearmodifiers", "alt+r", "Home", *(["Down"] * index), "alt+m", "ctrl+a")
        set_clipboard(text)
        xdo("key", "--clearmodifiers", "ctrl+v", "Return")

    def messages(self):
        return [line for line in self.history().splitlines() if re.match(r"\d\d:\d\d:\d\d\.\d{3}  #", line)]

    def quit(self):
        self.focus()
        xdo("key", "--clearmodifiers", "ctrl+q")

    def close(self):
        if self.process.poll() is None:
            os.kill(self.pid, signal.SIGCONT)
            try:
                if window_for(self.pid):
                    self.quit()
                else:
                    self.process.terminate()
                self.process.wait(timeout=12)
            except (AssertionError, subprocess.SubprocessError):
                self.process.kill()
                self.process.wait(timeout=3)
        self.reader.join(timeout=2)
        self.process.stderr.close()


def check_cleanup(root, host_pid, code):
    assert not root.exists(), f"Leaked IPC directory: {root}"
    semaphores = list(Path("/dev/shm").glob(f"sem.pn{code}_{host_pid}_*"))
    assert not semaphores, f"Leaked semaphores: {semaphores}"


def failed_start(executable, *arguments, error):
    result = subprocess.run(
        [str(executable), *map(str, arguments)], stdin=subprocess.DEVNULL,
        capture_output=True, encoding="utf-8", timeout=10,
    )
    assert result.returncode != 0 and error in result.stderr, result.stderr


def exercise(bin_dir, code, idle):
    host_exe = bin_dir / f"host_{code}"
    client_exe = bin_dir / f"client_{code}"
    failed_start(client_exe, "--host", "2147483647", "--name", "absent", error="Host PID")
    failed_start(client_exe, "--host", "0", "--name", "invalid", error="PID must")
    failed_start(client_exe, "--host", "2", "--name", "bad name", error="Name cannot")
    host = Process(host_exe, "--name", "host")
    processes = [host]
    root = Path(f"/tmp/polynet_{os.getuid()}_{code}_{host.pid}")
    try:
        host.wait(f"READY host={host.pid}")
        clients = [Process(client_exe, "--host", host.pid, "--name", name)
                   for name in ("alice", "bob", "carol")]
        processes += clients
        alice, bob, carol = clients
        for client in clients:
            client.wait(f"READY client={client.pid}")
        for participant in processes:
            participant.wait(f"carol, PID {carol.pid}")

        alice.send("public-alice")
        bob.send("public-bob")
        host.send("public-host")
        for participant in processes:
            participant.wait("alice → всем: public-alice")
            participant.wait("bob → всем: public-bob")
            participant.wait("host → всем: public-host")

        alice.send("secret-alice-bob", bob.pid)
        alice.wait(": secret-alice-bob")
        bob.wait(": secret-alice-bob")
        host.send("secret-host-alice", alice.pid)
        host.wait(": secret-host-alice")
        alice.wait(": secret-host-alice")
        alice.send("secret-alice-host", host.pid)
        host.wait(": secret-alice-host")
        alice.wait(": secret-alice-host")
        carol.send("secret-self", carol.pid)
        carol.wait(": secret-self")
        for participant in (host, carol):
            assert "secret-alice-bob" not in participant.history()
        for participant in (bob, carol):
            history = participant.history()
            assert "secret-host-alice" not in history and "secret-alice-host" not in history
        for participant in (host, alice, bob):
            assert "secret-self" not in participant.history()

        for index in range(4):
            for sender, participant in enumerate(processes):
                participant.send(f"ordered-{sender}-{index}")
        ordered = []
        for participant in processes:
            participant.wait(": ordered-3-3")
            messages = participant.messages()
            numbers = [int(re.search(r"#(\d+)", message)[1]) for message in messages]
            assert all(a < b for a, b in zip(numbers, numbers[1:])), numbers
            ordered.append([message for message in messages if ": ordered-" in message])
        assert len(ordered[0]) == 16 and all(messages == ordered[0] for messages in ordered)

        message = "я" * 1024
        alice.send(message)
        for participant in processes:
            participant.wait(": " + message)
        alice.send("я" * 1025)
        alice.wait("Message must contain 1 to 2048 UTF-8 bytes")
        alice.send("valid-after-size-error")
        host.wait(": valid-after-size-error")

        duplicate = Process(client_exe, "--host", host.pid, "--name", "alice")
        processes.append(duplicate)
        duplicate.wait_error("Name already in use")
        assert duplicate.process.wait(timeout=8) != 0
        eventually(lambda: not (root / f"c_{duplicate.pid}").exists())

        if idle:
            for client in clients:
                client.send(f"start-idle-{client.pid}")
            host.wait(f": start-idle-{carol.pid}")
            print(f"{code}: checking real 60-second inactivity", flush=True)
            deadline = time.monotonic() + 66
            while time.monotonic() < deadline and any(p.process.poll() is None for p in (bob, carol)):
                host.send("incoming-does-not-reset-idle")
                alice.send("keep-alice-alive")
                time.sleep(2)
            assert bob.process.wait(timeout=3) == -signal.SIGKILL
            assert carol.process.wait(timeout=3) == -signal.SIGKILL
            assert alice.process.poll() is None
            host.wait("Inactivity exceeded 60 seconds; SIGKILL")
        else:
            carol.quit()
            assert carol.process.wait(timeout=8) == 0
            host.wait("[Отключение] carol:")
            eventually(lambda: not (root / f"c_{carol.pid}").exists())
            alice.send("before-timeouts-alice")
            bob.send("before-timeouts-bob")

            libc = ctypes.CDLL(None, use_errno=True)
            libc.sem_open.restype = ctypes.c_void_p
            libc.sem_open.argtypes = [ctypes.c_char_p, ctypes.c_int]
            for function in (libc.sem_wait, libc.sem_post, libc.sem_close):
                function.argtypes = [ctypes.c_void_p]
                function.restype = ctypes.c_int
            sem = libc.sem_open(f"/pn{code}_{host.pid}_join".encode(), 0)
            assert sem not in (None, ctypes.c_void_p(-1).value)
            assert libc.sem_wait(sem) == 0
            try:
                start = time.monotonic()
                blocked = Process(client_exe, "--host", host.pid, "--name", "blocked")
                processes.append(blocked)
                eventually(lambda: window_for(blocked.pid), timeout=2)
                blocked.wait_error("Semaphore timed out after 5 seconds", timeout=8)
                assert blocked.process.wait(timeout=3) != 0
                assert 4.8 <= time.monotonic() - start < 9
            finally:
                assert libc.sem_post(sem) == 0
                assert libc.sem_close(sem) == 0

            os.kill(host.pid, signal.SIGSTOP)
            try:
                start = time.monotonic()
                stalled = Process(client_exe, "--host", host.pid, "--name", "stalled")
                processes.append(stalled)
                eventually(lambda: window_for(stalled.pid), timeout=2)
                stalled.wait_error("Handshake timed out after 5 seconds", timeout=8)
                assert stalled.process.wait(timeout=3) != 0
                assert 4.8 <= time.monotonic() - start < 9
            finally:
                os.kill(host.pid, signal.SIGCONT)
            eventually(lambda: not (root / f"c_{stalled.pid}").exists())

            host.send("before-stalled-reader")
            bob.wait(": before-stalled-reader")
            os.kill(bob.pid, signal.SIGSTOP)
            try:
                host.send("stalled-reader-first")
                host.wait(": stalled-reader-first")
                start = time.monotonic()
                host.send("stalled-reader-second")
                host.wait_error(f"Semaphore timed out after 5 seconds: /pn{code}_{host.pid}_{bob.pid}c_s")
                assert 4.8 <= time.monotonic() - start < 9
                host.wait("[Отключение] bob:")
                eventually(lambda: not (root / f"c_{bob.pid}").exists())
            finally:
                os.kill(bob.pid, signal.SIGCONT)
            assert bob.process.wait(timeout=8) != 0
            alice.send("after-stalled-reader")
            host.wait(": after-stalled-reader")

        host.quit()
        host.process.wait(timeout=12)
        for client in clients:
            if client.process.poll() is None:
                client.process.wait(timeout=8)
    finally:
        for process in reversed(processes):
            process.close()
        stop_clipboard_owner()
    check_cleanup(root, host.pid, code)
    for process in processes:
        assert not any("ASSERT" in error or "QObject::" in error for error in process.errors), process.errors
    print(f"{code}: GUI chat, privacy, order, timeouts and cleanup passed", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin-dir", type=Path, required=True)
    parser.add_argument("--transport", choices=("sock", "fifo"), required=True)
    parser.add_argument("--idle", action="store_true")
    args = parser.parse_args()
    exercise(args.bin_dir.resolve(), args.transport, args.idle)
