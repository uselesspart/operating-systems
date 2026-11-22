#!/usr/bin/env python3
"""Black-box tests of real host/child processes for every transport."""
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

BIN = Path(sys.argv.pop(1)).resolve()
TYPES = ("mmap", "shm", "pipe")


class Integration(unittest.TestCase):
    def run_host(self, transport, *args, success=True):
        with tempfile.TemporaryDirectory(prefix="wolf-test-") as directory:
            log = Path(directory) / "session.log"
            result = subprocess.run(
                [str(BIN / f"host_{transport}"), "--headless", "--log", str(log), *args],
                text=True, capture_output=True, timeout=12,
            )
            self.assertEqual(result.returncode == 0, success, result.stderr)
            events = [json.loads(line) for line in result.stdout.splitlines()]
            text = log.read_text() if log.exists() else ""
            if events:
                self.assertFalse(Path(f"/proc/{events[0]['client_pid']}").exists(), "Child leaked")
                self.assertFalse(Path(f"/dev/shm/wolf_lab2_{events[0]['host_pid']}").exists(), "SHM leaked")
            return events, text

    def test_two_consecutive_dead_rounds(self):
        for transport in TYPES:
            with self.subTest(transport=transport):
                events, log = self.run_host(transport, "--wolf-sequence", "100,100", "--goat-sequence", "1,1")
                rounds = [e for e in events if e["event"] == "round"]
                self.assertEqual(len(rounds), 2)
                self.assertEqual([e["finished"] for e in rounds], [0, 1])
                self.assertEqual([e["caught"] for e in rounds], [1, 0])
                self.assertEqual(events[-1]["reason"], "two_dead_rounds")
                self.assertIn("client finished", log)
                self.assertNotIn("forcibly", log)

    def test_resurrection_resets_streak(self):
        for transport in TYPES:
            with self.subTest(transport=transport):
                events, _ = self.run_host(transport, "--wolf-sequence", "100,21,100,100", "--goat-sequence", "1,1,1,1")
                rounds = [e for e in events if e["event"] == "round"]
                self.assertEqual([e["alive"] for e in rounds], [0, 1, 0, 0])
                self.assertEqual(rounds[1]["resurrected"], 1)
                self.assertEqual([e["finished"] for e in rounds], [0, 0, 0, 1])

    def test_hide_boundary_and_round_limit(self):
        for transport in TYPES:
            with self.subTest(transport=transport):
                events, log = self.run_host(transport, "--wolf-sequence", "100", "--goat-sequence", "30", "--round-limit", "1")
                self.assertEqual(events[1]["hidden"], 1)
                self.assertEqual(events[-1]["reason"], "round_limit")
                self.assertNotIn("forcibly", log)

    def test_random_ranges_and_reproducibility(self):
        for transport in TYPES:
            with self.subTest(transport=transport):
                args = ("--seed", "42", "--round-limit", "200")
                first, _ = self.run_host(transport, *args)
                second, _ = self.run_host(transport, *args)
                self.assertEqual(first[1:], second[1:])
                alive = True
                for event in first[1:-1]:
                    self.assertTrue(1 <= event["wolf"] <= 100)
                    self.assertTrue(1 <= event["goat"] <= (100 if alive else 50))
                    alive = bool(event["alive"])

    def test_invalid_arguments(self):
        cases = [("--seed", "-1"), ("--wolf-sequence", "0"), ("--goat-sequence", "101"),
                 ("--wolf-sequence", "1,"), ("--round-limit", "0"), ("--delay-ms", "3001"),
                 ("--seed", "4294967296"), ("--seed",), ("--unknown", "x")]
        for transport in TYPES:
            for args in cases:
                with self.subTest(transport=transport, args=args):
                    events, _ = self.run_host(transport, *args, success=False)
                    self.assertEqual(events, [])

    def test_unwritable_log(self):
        for transport in TYPES:
            result = subprocess.run([str(BIN / f"host_{transport}"), "--headless", "--log", "/nonexistent-lab2-dir/log"],
                                    text=True, capture_output=True, timeout=3)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("open log", result.stderr)
            self.assertEqual(result.stdout, "")

    def test_killed_client_times_out_and_is_reaped(self):
        for transport in TYPES:
            with self.subTest(transport=transport), tempfile.TemporaryDirectory() as directory:
                log = Path(directory) / "log"
                process = subprocess.Popen([str(BIN / f"host_{transport}"), "--headless", "--delay-ms", "500",
                                            "--log", str(log)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                try:
                    started = json.loads(process.stdout.readline())
                    start = time.monotonic()
                    os.kill(started["client_pid"], signal.SIGKILL)
                    _, error = process.communicate(timeout=8)
                    elapsed = time.monotonic() - start
                    self.assertNotEqual(process.returncode, 0)
                    self.assertLessEqual(elapsed, 7.5, (transport, elapsed, error))
                    if "timeout" in error:
                        self.assertGreaterEqual(elapsed, 4.8)
                    else:
                        self.assertEqual(transport, "pipe")
                        self.assertIn("Broken pipe", error)
                    self.assertFalse(Path(f"/proc/{started['client_pid']}").exists())
                    self.assertFalse(Path(f"/dev/shm/wolf_lab2_{started['host_pid']}").exists())
                finally:
                    if process.poll() is None:
                        process.kill()
                        process.communicate()

    def test_signal_stops_child_and_cleans_resources(self):
        for transport in TYPES:
            with self.subTest(transport=transport), tempfile.TemporaryDirectory() as directory:
                log = Path(directory) / "log"
                process = subprocess.Popen([str(BIN / f"host_{transport}"), "--headless", "--delay-ms", "1000",
                                            "--log", str(log)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                try:
                    started = json.loads(process.stdout.readline())
                    time.sleep(0.1)
                    process.send_signal(signal.SIGTERM)
                    output, error = process.communicate(timeout=5)
                    self.assertEqual(process.returncode, 128 + signal.SIGTERM, error)
                    self.assertIn('"reason":"signal"', output)
                    self.assertFalse(Path(f"/proc/{started['client_pid']}").exists())
                    self.assertFalse(Path(f"/dev/shm/wolf_lab2_{started['host_pid']}").exists())
                    self.assertIn("client finished", log.read_text())
                finally:
                    if process.poll() is None:
                        process.kill()
                        process.communicate()

    def test_client_stops_after_host_stalls(self):
        for transport in TYPES:
            with self.subTest(transport=transport), tempfile.TemporaryDirectory() as directory:
                log = Path(directory) / "log"
                process = subprocess.Popen([str(BIN / f"host_{transport}"), "--headless", "--delay-ms", "500",
                                            "--wolf-sequence", "50,50", "--goat-sequence", "50,50",
                                            "--log", str(log)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                try:
                    started = json.loads(process.stdout.readline())
                    first_round = json.loads(process.stdout.readline())
                    self.assertEqual(first_round["event"], "round")
                    process.send_signal(signal.SIGSTOP)
                    start = time.monotonic()
                    while "client error: IPC timeout" not in log.read_text() and time.monotonic() - start < 6.5:
                        time.sleep(0.05)
                    self.assertIn("client error: IPC timeout", log.read_text())
                    self.assertGreaterEqual(time.monotonic() - start, 4.7)
                    process.send_signal(signal.SIGTERM)
                    process.send_signal(signal.SIGCONT)
                    process.communicate(timeout=5)
                    self.assertFalse(Path(f"/proc/{started['client_pid']}").exists())
                    self.assertFalse(Path(f"/dev/shm/wolf_lab2_{started['host_pid']}").exists())
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGCONT)
                        process.kill()
                        process.communicate()


if __name__ == "__main__":
    unittest.main(verbosity=2)
