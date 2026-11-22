"""Real independent processes, production binaries and Linux IPC; no mocks."""
import concurrent.futures
import os
from pathlib import Path
import signal
import subprocess
import sys
import threading
import time
import unittest

BIN = Path(sys.argv[1])
MODE = sys.argv[2]
sys.argv[1:] = []


class Process:
    def __init__(self, executable, *args, gui=False):
        # Qt5's offscreen plugin retains a QScreen at exit; minimal is also
        # display-independent and lets LeakSanitizer check all allocations.
        env = dict(os.environ, QT_QPA_PLATFORM="minimal")
        self.process = subprocess.Popen(
            [str(BIN / executable), *map(str, args), *([] if gui else ["--headless"])],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, encoding="utf-8", env=env,
        )
        self.lines = []
        self.errors = []
        self.allowed_exits = {0}
        self.lock = threading.Lock()
        self.threads = []
        for stream, destination in [(self.process.stdout, self.lines), (self.process.stderr, self.errors)]:
            def collect(source=stream, target=destination):
                for line in source:
                    with self.lock:
                        target.append(line.rstrip("\n"))
            thread = threading.Thread(target=collect, daemon=True)
            thread.start()
            self.threads.append(thread)

    @property
    def pid(self):
        return self.process.pid

    def send(self, text):
        self.process.stdin.write(text + "\n")
        self.process.stdin.flush()

    def wait_for(self, predicate, timeout=8):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            with self.lock:
                for line in self.lines:
                    if predicate(line):
                        return line
            if self.process.poll() is not None:
                break
            time.sleep(0.02)
        raise AssertionError(f"PID {self.pid}: expected output; stdout={self.lines}, stderr={self.errors}")

    def ready(self):
        self.wait_for(lambda line: line.startswith("READY "))
        return self

    def message(self, text):
        return self.wait_for(lambda line: line.startswith("MSG ") and line.split(" ", 5)[5] == text)

    def messages(self):
        with self.lock:
            return [line for line in self.lines if line.startswith("MSG ")]

    def close(self):
        if self.process.poll() is None:
            self.process.send_signal(signal.SIGCONT)
            self.process.terminate()
            try:
                self.process.wait(12)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(3)
                raise AssertionError(f"PID {self.pid} did not stop on SIGTERM")
        for thread in self.threads:
            thread.join(2)
        for stream in [self.process.stdin, self.process.stdout, self.process.stderr]:
            try:
                stream.close()
            except BrokenPipeError:
                pass
        errors = "\n".join(self.errors)
        if "Sanitizer" in errors or "runtime error:" in errors:
            raise AssertionError(errors)
        if self.process.returncode not in self.allowed_exits:
            raise AssertionError(f"PID {self.pid}: unexpected exit {self.process.returncode}; {errors}")


class Components(unittest.TestCase):
    def setUp(self):
        self.processes = []
        self.hosts = []

    def tearDown(self):
        errors = []
        for process in reversed(self.processes):
            try:
                process.close()
            except Exception as error:
                errors.append(error)
        for host in self.hosts:
            directory = Path(f"/tmp/gutsalov-lab2-{os.geteuid()}-{host.pid}")
            if directory.exists():
                errors.append(AssertionError(f"Session leaked: {directory}, {list(directory.iterdir())}"))
            prefix = f"sem.gutsalov-l2-{os.geteuid()}-{host.pid}-"
            leaked = list(Path('/dev/shm').glob(prefix + '*'))
            if leaked:
                errors.append(AssertionError(f"Semaphores leaked: {leaked}"))
        if errors:
            raise AssertionError(errors)

    def start(self, role, code=MODE, host=None, gui=False):
        process = Process(f"{role}_{code}", *([] if host is None else [host.pid]), gui=gui)
        self.processes.append(process)
        if role == "host":
            self.hosts.append(process)
        return process.ready()

    @unittest.skipIf(MODE == "idle", "normal transport suite")
    def test_chat_and_lifecycle(self):
        host = self.start("host")
        clients = [self.start("client", host=host) for _ in range(3)]
        host.send("host broadcast")
        for participant in [host, *clients]:
            participant.message("host broadcast")
        clients[0].send("Привет, общий чат!")
        for participant in [host, *clients]:
            participant.message("Привет, общий чат!")
        clients[0].send(f"/to {clients[1].pid} secret")
        clients[0].message("secret")
        clients[1].message("secret")
        time.sleep(0.4)
        self.assertFalse(any(line.endswith(" secret") for p in [host, clients[2]] for line in p.messages()))
        clients[1].send("/to 0 host-only")
        host.message("host-only")
        clients[1].message("host-only")
        host.send(f"/to {clients[2].pid} host-private")
        host.message("host-private")
        clients[2].message("host-private")
        clients[0].send(f"/to {clients[0].pid} self-private")
        clients[0].message("self-private")
        clients[0].send("/to 99999999 missing")
        clients[0].wait_for(lambda line: line == "STATUS unknown recipient 99999999")
        forbidden = [(clients[0], "host-only"), (clients[2], "host-only"),
                     (clients[0], "host-private"), (clients[1], "host-private"),
                     (host, "self-private"), (clients[1], "self-private"), (clients[2], "self-private")]
        for participant, text in forbidden:
            self.assertFalse(any(line.split(" ", 5)[5] == text for line in participant.messages()))
        clients[0].send("x" * 1024)
        for participant in [host, *clients]:
            participant.message("x" * 1024)
        clients[0].send("x" * 1025)
        clients[0].wait_for(lambda line: line.startswith("ERROR "))
        with concurrent.futures.ThreadPoolExecutor() as pool:
            futures = [pool.submit(client.send, f"parallel-{i}") for i, client in enumerate(clients)]
            for future in futures:
                future.result()
        for participant in [host, *clients]:
            for i in range(3):
                participant.message(f"parallel-{i}")
            messages = participant.messages()
            sequences = [int(line.split()[1]) for line in messages]
            stamps = [int(line.split()[2]) for line in messages]
            self.assertEqual(sequences, sorted(set(sequences)))
            self.assertEqual(stamps, sorted(stamps))
        orders = [[line for line in p.messages() if " parallel-" in line] for p in [host, *clients]]
        self.assertTrue(all(order == orders[0] for order in orders))
        clients[2].send("/quit")
        self.assertEqual(clients[2].process.wait(5), 0)
        replacement = self.start("client", host=host)
        replacement.send("reconnected")
        host.message("reconnected")
        host.process.terminate()
        self.assertEqual(host.process.wait(12), 0)
        for client in [clients[0], clients[1], replacement]:
            client.process.wait(8)

    @unittest.skipIf(MODE == "idle", "normal transport suite")
    def test_gui_without_display(self):
        host = self.start("host", gui=True)
        gui_client = self.start("client", host=host, gui=True)
        sender = self.start("client", host=host)
        sender.send("GUI receives message")
        for participant in [host, gui_client, sender]:
            participant.message("GUI receives message")

    @unittest.skipIf(MODE == "idle", "normal transport suite")
    def test_simultaneous_handshake(self):
        host = self.start("host")
        clients = [Process(f"client_{MODE}", host.pid) for _ in range(8)]
        self.processes.extend(clients)
        for client in clients:
            client.ready()
        host.send("all joined")
        for client in clients:
            client.message("all joined")

    @unittest.skipIf(MODE == "idle", "normal transport suite")
    def test_peer_crash(self):
        host = self.start("host")
        client = self.start("client", host=host)
        client.allowed_exits.add(-signal.SIGKILL)
        client.process.kill()
        client.process.wait(3)
        host.wait_for(lambda line: "disconnected" in line)
        remaining = self.start("client", host=host)
        remaining.send("after crash")
        host.message("after crash")

    @unittest.skipIf(MODE == "idle", "normal transport suite")
    def test_semaphore_timeout(self):
        host = self.start("host")
        client = self.start("client", host=host)
        # Drain connection notifications before freezing the reader.
        host.send("before freeze")
        client.message("before freeze")
        client.process.send_signal(signal.SIGSTOP)
        host.send("unread first")
        host.message("unread first")
        time.sleep(0.3)
        started = time.monotonic()
        host.send("unread second")
        host.wait_for(lambda line: "sem_clockwait" in line, timeout=8)
        elapsed = time.monotonic() - started
        self.assertGreaterEqual(elapsed, 4.8, host.lines)
        self.assertLess(elapsed, 7.5)
        client.process.send_signal(signal.SIGCONT)
        client.process.wait(8)

    @unittest.skipIf(MODE == "idle", "normal transport suite")
    def test_cli_errors(self):
        for role in ["host", "client"]:
            result = subprocess.run([str(BIN / f"{role}_{MODE}"), "--help"], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
        for args in [[], ["-1"], ["abc"], ["2147483648"], ["99999999", "--headless"]]:
            result = subprocess.run([str(BIN / f"client_{MODE}"), *args], capture_output=True, text=True, timeout=8)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("ERROR", result.stderr)

    @unittest.skipIf(MODE == "idle", "normal transport suite")
    def test_handshake_timeout(self):
        host = self.start("host")
        host.process.send_signal(signal.SIGSTOP)
        started = time.monotonic()
        try:
            result = subprocess.run([str(BIN / f"client_{MODE}"), str(host.pid), "--headless"],
                                    capture_output=True, text=True, timeout=8)
        finally:
            host.process.send_signal(signal.SIGCONT)
        self.assertEqual(result.returncode, 1)
        self.assertIn("Handshake timeout", result.stderr)
        self.assertGreaterEqual(time.monotonic() - started, 4.8)

    @unittest.skipUnless(MODE == "idle", "60-second test runs once for all transports")
    def test_real_minute_inactivity(self):
        sessions = []
        started = time.monotonic()
        for code in ["pipe", "fifo", "sock"]:
            host = self.start("host", code)
            idle = self.start("client", code, host)
            idle.allowed_exits.add(-signal.SIGKILL)
            active = self.start("client", code, host)
            sessions.append((host, idle, active))
        while time.monotonic() - started < 62:
            elapsed = time.monotonic() - started
            for host, idle, active in sessions:
                if elapsed < 58:
                    self.assertIsNone(idle.process.poll(), "client killed before one minute")
                self.assertIsNone(host.process.poll())
                self.assertIsNone(active.process.poll())
                if 29 < elapsed < 31:
                    active.send("keep active")
            time.sleep(0.5)
        for host, idle, active in sessions:
            self.assertEqual(idle.process.wait(3), -signal.SIGKILL)
            host.wait_for(lambda line: line == f"STATUS idle-kill {idle.pid}")
            self.assertIsNone(active.process.poll())
            active.send("still working")
            host.message("still working")


if __name__ == "__main__":
    unittest.main(verbosity=2)
