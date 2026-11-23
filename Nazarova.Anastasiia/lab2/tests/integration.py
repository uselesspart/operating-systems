import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import threading
import time
import unittest


DRIVER = str(Path(sys.argv.pop(1)).resolve())
WAIT = 8


class Peer:
    def __init__(self, host=None):
        self.errors = tempfile.TemporaryFile(mode="w+t")
        args = [DRIVER] if host is None else [DRIVER, str(host)]
        self.process = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=self.errors, text=True, encoding="utf-8", bufsize=1)
        self.pid = self.process.pid
        self.events = []
        self.changed = threading.Condition()
        self.reader = threading.Thread(target=self.read, daemon=True)
        self.reader.start()

    def read(self):
        for line in self.process.stdout:
            with self.changed:
                self.events.append(json.loads(line))
                self.changed.notify_all()

    def wait(self, predicate, timeout=WAIT):
        deadline = time.monotonic() + timeout
        with self.changed:
            while True:
                for event in self.events:
                    if predicate(event):
                        return event
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    self.errors.flush()
                    self.errors.seek(0)
                    raise AssertionError(f"PID {self.pid}: событие не получено; {self.errors.read()}")
                self.changed.wait(min(remaining, 0.1))

    def connected(self):
        self.wait(lambda event: event.get("type") == "members" and self.pid in event["members"])

    def send(self, text, recipient=0):
        self.process.stdin.write(json.dumps({"text": text, "recipient": recipient}) + "\n")
        self.process.stdin.flush()

    def message(self, text):
        return self.wait(lambda event: event.get("type") == "chat" and event.get("text") == text)

    def has_message(self, text):
        with self.changed:
            return any(event.get("type") == "chat" and event.get("text") == text for event in self.events)

    def stop(self):
        if self.process.poll() is None:
            os.kill(self.pid, signal.SIGCONT)
            try:
                self.process.stdin.write('{"type":"quit"}\n')
                self.process.stdin.flush()
                self.process.wait(timeout=WAIT)
            except (BrokenPipeError, subprocess.TimeoutExpired):
                self.process.kill()
                self.process.wait()
        self.reader.join(timeout=WAIT)
        try:
            self.process.stdin.close()
        except BrokenPipeError:
            pass
        self.process.stdout.close()
        self.errors.close()


class ChatTests(unittest.TestCase):
    def setUp(self):
        self.peers = []
        self.host = self.start()
        self.directory = Path(tempfile.gettempdir()) / f"lab2_{os.getuid()}_{self.host.pid}_0_chat"
        self.addCleanup(self.cleanup)
        self.host.connected()

    def cleanup(self):
        if self.host.process.poll() is None:
            os.kill(self.host.pid, signal.SIGCONT)
        for peer in reversed(self.peers):
            peer.stop()
        self.assertFalse(self.directory.exists())
        self.assertEqual(list(Path("/dev/shm").glob(f"sem.lab2_{os.getuid()}_{self.host.pid}_*")), [])

    def start(self, host=None, wait=False):
        peer = Peer(host)
        self.peers.append(peer)
        if wait:
            peer.connected()
        return peer

    def test_public_and_private(self):
        first = self.start(self.host.pid, wait=True)
        second = self.start(self.host.pid, wait=True)
        first.send("Общее")
        for peer in (self.host, first, second):
            event = peer.message("Общее")
            self.assertEqual(event["sender"], first.pid)
        first.send("Личное", second.pid)
        first.message("Личное")
        second.message("Личное")
        self.assertFalse(self.host.has_message("Личное"))
        second.send("Хосту", self.host.pid)
        self.host.message("Хосту")
        second.message("Хосту")
        self.assertFalse(first.has_message("Хосту"))
        self.host.send("От хоста", first.pid)
        first.message("От хоста")
        self.host.message("От хоста")
        self.host.send("Всем от хоста")
        for peer in (self.host, first, second):
            peer.message("Всем от хоста")
        self.assertFalse(second.has_message("От хоста"))
        self.assertFalse(self.host.has_message("Личное"))

    def test_simultaneous_clients(self):
        clients = [self.start(self.host.pid) for _ in range(8)]
        for peer in clients:
            peer.connected()
        self.host.wait(lambda event: event.get("type") == "members" and len(event["members"]) == len(clients) + 1)
        self.assertGreaterEqual(len(list(Path(f"/proc/{self.host.pid}/task").iterdir())), len(clients) + 1)
        for index, peer in enumerate(clients):
            peer.send(f"Сообщение {index}")
        for peer in [self.host] + clients:
            for index in range(len(clients)):
                peer.message(f"Сообщение {index}")

    def test_long_unicode_message(self):
        client = self.start(self.host.pid, wait=True)
        text = "Строка 🐱 <b>текст</b>\n" * 5000
        client.send(text)
        self.assertEqual(self.host.message(text)["text"], text)
        self.assertEqual(client.message(text)["text"], text)

    def test_disconnect_and_new_clients(self):
        for index in range(5):
            peer = self.start(self.host.pid, wait=True)
            peer.send(f"Вход {index}")
            self.host.message(f"Вход {index}")
            os.kill(peer.pid, signal.SIGTERM)
            self.assertEqual(peer.process.wait(timeout=WAIT), 0)
            self.host.wait(lambda event: event.get("type") == "members"
                           and str(peer.pid) in event.get("text", "") and peer.pid not in event["members"])
            self.assertFalse((self.directory / str(peer.pid)).exists())

    def test_missing_recipient(self):
        client = self.start(self.host.pid, wait=True)
        other = self.start(self.host.pid, wait=True)
        os.kill(other.pid, signal.SIGTERM)
        other.process.wait(timeout=WAIT)
        client.wait(lambda event: event.get("type") == "members"
                    and str(other.pid) in event.get("text", "") and other.pid not in event["members"])
        client.send("Не доставлять", other.pid)
        client.wait(lambda event: event.get("type") == "error")
        self.assertFalse(self.host.has_message("Не доставлять"))
        self.assertFalse(client.has_message("Не доставлять"))

    def test_host_shutdown(self):
        client = self.start(self.host.pid, wait=True)
        os.kill(self.host.pid, signal.SIGTERM)
        self.assertEqual(self.host.process.wait(timeout=WAIT), 0)
        self.assertNotEqual(client.process.wait(timeout=WAIT), 0)

    def test_handshake_timeout(self):
        os.kill(self.host.pid, signal.SIGSTOP)
        start = time.monotonic()
        client = self.start(self.host.pid)
        self.assertNotEqual(client.process.wait(timeout=WAIT), 0)
        self.assertGreaterEqual(time.monotonic() - start, 4.5)
        os.kill(self.host.pid, signal.SIGCONT)
        self.start(self.host.pid, wait=True)

    def test_foreign_reply_signal(self):
        os.kill(self.host.pid, signal.SIGSTOP)
        client = self.start(self.host.pid)
        time.sleep(0.2)
        os.kill(client.pid, signal.SIGUSR2)
        time.sleep(0.2)
        self.assertIsNone(client.process.poll())
        os.kill(self.host.pid, signal.SIGCONT)
        client.connected()
        client.send("После знакомства")
        self.host.message("После знакомства")

    def test_exchange_timeout(self):
        client = self.start(self.host.pid, wait=True)
        os.kill(self.host.pid, signal.SIGSTOP)
        start = time.monotonic()
        self.assertNotEqual(client.process.wait(timeout=WAIT), 0)
        self.assertGreaterEqual(time.monotonic() - start, 4.5)
        os.kill(self.host.pid, signal.SIGCONT)

    def test_idle_client_gets_sigkill(self):
        idle = self.start(self.host.pid, wait=True)
        active = self.start(self.host.pid, wait=True)
        start = time.monotonic()
        next_message = start
        while idle.process.poll() is None and time.monotonic() - start < 70:
            now = time.monotonic()
            if now >= next_message:
                text = f"Работаю {now}"
                active.send(text)
                active.message(text)
                next_message = now + 20
            time.sleep(0.1)
        self.assertEqual(idle.process.poll(), -signal.SIGKILL)
        self.assertGreaterEqual(time.monotonic() - start, 59)
        self.assertIsNone(active.process.poll())
        self.assertIsNone(self.host.process.poll())


if __name__ == "__main__":
    unittest.main(verbosity=2)
