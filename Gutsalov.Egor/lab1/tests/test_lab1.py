#!/usr/bin/env python3
"""Black-box file and daemon tests; Linux, Python stdlib, no root required."""

import ctypes
import fcntl
import os
from pathlib import Path
import resource
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest

BINARY = Path(sys.argv.pop(1)).resolve()
SANITIZER = os.environ.get("LAB1_SANITIZER", "none")
CONTROL = Path(__file__).resolve().parents[1] / "scripts" / "control.sh"

# Adopt and reap double-forked test daemons even when container PID 1 cannot.
if ctypes.CDLL(None, use_errno=True).prctl(36, 1, 0, 0, 0) != 0:
    raise OSError(ctypes.get_errno(), "PR_SET_CHILD_SUBREAPER")


class LabTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="gutsalov-lab1-case-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "source logs"
        self.target = self.root / "archive logs"
        self.source.mkdir()
        self.target.mkdir()
        self.config = self.root / "lab1.conf"
        self.pid_file = self.root / "daemon.pid"
        self.capture = self.root / "syslog.txt"
        self.children = set()
        self.allowed_kills = set()
        self.unexpected_exits = []
        self.binary = BINARY
        self.env = os.environ.copy()
        self.env["LAB1_TEST_SYSLOG"] = str(self.capture)
        # The daemon redirects stderr. Separate files ensure a report in any
        # forked process fails the test, even when an error exit was expected.
        for option, stem in (("ASAN_OPTIONS", "asan"), ("TSAN_OPTIONS", "tsan")):
            previous = self.env.get(option, "")
            self.env[option] = f"{previous}:halt_on_error=1:log_path={self.root / stem}"
        self.write_config()
        self.addCleanup(self.assert_no_sanitizer_errors)
        self.addCleanup(self.stop_all)

    def write_config(self, source=None, target=None, interval=45):
        # Atomic replacement verifies that the daemon remembers a path, not an inode.
        temporary = self.config.with_suffix(".new")
        temporary.write_text(
            f"# test config\nsource_dir = {source or self.source.name}\n"
            f"target_dir = {target or self.target.name}\ninterval = {interval}\n",
            encoding="utf-8",
        )
        temporary.replace(self.config)

    def run_binary(self, *arguments, **kwargs):
        return subprocess.run(
            [str(self.binary), *map(str, arguments)], cwd=self.root,
            env=self.env, capture_output=True, text=True, timeout=15, **kwargs,
        )

    def once(self, **kwargs):
        return self.run_binary(self.config, "--once", **kwargs)

    def messages(self):
        return self.capture.read_text(encoding="utf-8") if self.capture.exists() else ""

    def output(self, target=None):
        path = (target or self.target) / "total.log"
        return path.read_bytes() if path.exists() else b""

    def reap(self):
        for pid in list(self.children):
            try:
                reaped, status = os.waitpid(pid, os.WNOHANG)
                if reaped == pid:
                    self.children.remove(pid)
                    code = os.waitstatus_to_exitcode(status)
                    if code != 0 and not (code == -signal.SIGKILL and pid in self.allowed_kills):
                        self.unexpected_exits.append((pid, code))
            except ChildProcessError:
                pass

    def wait_for(self, predicate, timeout=10):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.reap()
            if predicate():
                return
            time.sleep(0.02)
        self.fail(f"Condition did not become true. Captured syslog:\n{self.messages()}")

    def start(self, **kwargs):
        result = self.run_binary(self.config, "--pid-file", self.pid_file, **kwargs)
        self.assertEqual(result.returncode, 0, result.stderr + self.messages())
        pid = int(self.pid_file.read_text().strip())
        self.children.add(pid)
        return pid

    def stop(self, pid):
        os.kill(pid, signal.SIGTERM)
        self.wait_for(lambda: not Path(f"/proc/{pid}").exists())

    def stop_all(self):
        # Also cover a child that started just before an assertion failed.
        if self.pid_file.exists():
            text = self.pid_file.read_text().strip()
            if text.isdecimal():
                pid = int(text)
                try:
                    if Path(f"/proc/{pid}/exe").resolve() == self.binary:
                        self.children.add(pid)
                except OSError:
                    pass
        for pid in list(self.children):
            try:
                os.kill(pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
        deadline = time.monotonic() + (10 if SANITIZER != "none" else 3)
        while self.children and time.monotonic() < deadline:
            self.reap()
            time.sleep(0.01)
        for pid in list(self.children):
            try:
                self.unexpected_exits.append((pid, "shutdown timed out"))
                os.kill(pid, signal.SIGKILL)
                os.waitpid(pid, 0)
            except (ProcessLookupError, ChildProcessError):
                pass
        # Reap adopted grandchildren that failed before creating a PID file.
        while True:
            try:
                if os.waitpid(-1, os.WNOHANG)[0] == 0:
                    break
            except ChildProcessError:
                break

    def assert_no_sanitizer_errors(self):
        reports = []
        for pattern in ("asan.*", "tsan.*"):
            for path in self.root.glob(pattern):
                content = path.read_text(errors="replace")
                if content:
                    reports.append(f"{path.name}:\n{content}")
        self.assertEqual(reports, [], "\n".join(reports))
        self.assertEqual(self.unexpected_exits, [], "Daemon exited abnormally")

    def test_binary_append_format_and_deletion(self):
        (self.target / "total.log").write_bytes(b"existing\n")
        (self.source / "a.log").write_bytes(b"one\ntwo\n")
        (self.source / "b.log").write_bytes(b"")
        binary = bytes(range(256)) * 1024
        (self.source / "c.log").write_bytes(binary)
        (self.source / "keep.txt").write_bytes(b"keep")
        (self.source / "keep.LOG").write_bytes(b"uppercase")
        result = self.once()
        self.assertEqual(result.returncode, 0, result.stderr)
        expected = (b"existing\n\n\na.log\n\none\ntwo\n"
                    b"\n\nb.log\n\n\n\nc.log\n\n" + binary)
        self.assertEqual(self.output(), expected)
        self.assertEqual(sorted(p.name for p in self.source.iterdir()), ["keep.LOG", "keep.txt"])
        self.assertEqual(self.once().returncode, 0)
        self.assertEqual(self.output(), expected)

    def test_recursive_paths_unicode_and_special_files(self):
        nested = self.source / "вложенная папка"
        nested.mkdir()
        (nested / "ошибка.log").write_bytes("сообщение\n".encode())
        (self.source / "empty.log").mkdir()
        (self.source / "link.log").symlink_to(nested / "ошибка.log")
        (self.source / "dir-link").symlink_to(nested, target_is_directory=True)
        os.mkfifo(self.source / "pipe.log")
        self.assertEqual(self.once().returncode, 0)
        self.assertEqual(self.output(), "\n\nвложенная папка/ошибка.log\n\nсообщение\n".encode())
        self.assertTrue((self.source / "empty.log").is_dir())
        self.assertTrue((self.source / "link.log").is_symlink())
        self.assertTrue((self.source / "pipe.log").exists())

    def test_output_inside_source_and_hard_link_are_skipped(self):
        nested_target = self.source / "archive"
        nested_target.mkdir()
        (nested_target / "total.log").write_bytes(b"previous")
        os.link(nested_target / "total.log", self.source / "alias.log")
        self.write_config(target=nested_target)
        (self.source / "input.log").write_bytes(b"data")
        self.assertEqual(self.once().returncode, 0)
        self.assertEqual(self.output(nested_target), b"previous\n\ninput.log\n\ndata")
        self.assertTrue((self.source / "alias.log").exists())
        self.assertEqual(self.once().returncode, 0)
        self.assertEqual(self.output(nested_target), b"previous\n\ninput.log\n\ndata")

    def test_failed_output_preserves_source_and_symlink_target(self):
        source = self.source / "input.log"
        source.write_bytes(b"must survive")
        unrelated = self.root / "unrelated.txt"
        unrelated.write_bytes(b"unrelated")
        (self.target / "total.log").symlink_to(unrelated)
        self.assertNotEqual(self.once().returncode, 0)
        self.assertEqual(source.read_bytes(), b"must survive")
        self.assertEqual(unrelated.read_bytes(), b"unrelated")
        (self.target / "total.log").unlink()
        (self.target / "total.log").mkdir()
        self.assertNotEqual(self.once().returncode, 0)
        self.assertEqual(source.read_bytes(), b"must survive")

    def test_partial_write_is_rolled_back(self):
        content = b"x" * 8192
        source = self.source / "large.log"
        source.write_bytes(content)
        (self.target / "total.log").write_bytes(b"existing")

        def limit_file_size():
            resource.setrlimit(resource.RLIMIT_FSIZE, (1024, 1024))
            signal.signal(signal.SIGXFSZ, signal.SIG_IGN)

        result = self.once(preexec_fn=limit_file_size)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.output(), b"existing")
        self.assertEqual(source.read_bytes(), content)

    def test_default_config_comments_crlf_and_relative_paths(self):
        self.config.write_bytes(
            b"# comment\r\n\r\n source_dir = source logs \r\n"
            b"target_dir=archive logs\r\ninterval = 45\r\n")
        (self.source / "data.log").write_bytes(b"data")
        result = self.run_binary("--once")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.output(), b"\n\ndata.log\n\ndata")

    def test_invalid_configurations(self):
        good = self.config.read_text()
        cases = [
            good.replace("interval = 45", f"interval = {value}")
            for value in ("0", "-1", "1.5", "45sec", "2147483648", "", "+1")
        ]
        cases += [
            good + "interval=2\n", good + "unknown=3\n", good + "broken line\n",
            good.replace("interval = 45\n", ""),
            good.replace("source logs", "does-not-exist"),
            good.replace("archive logs", "source logs"),
            good.replace("source logs", "lab1.conf"),
        ]
        for config in cases:
            with self.subTest(config=config):
                self.config.write_text(config)
                result = self.once()
                self.assertNotEqual(result.returncode, 0)
                self.assertTrue(result.stderr)
                self.assertFalse((self.target / "total.log").exists())

    def test_cli_errors_and_missing_config(self):
        for args in (("--bad",), ("--pid-file",), ("one", "two"), ("missing.conf",)):
            with self.subTest(args=args):
                self.assertNotEqual(self.run_binary(*args).returncode, 0)
        self.assertEqual(self.run_binary("--help").returncode, 0)

    def test_daemon_context_logging_and_sigterm(self):
        (self.source / "first.log").write_bytes(b"first")
        pid = self.start()
        self.wait_for(lambda: not (self.source / "first.log").exists())
        proc = Path(f"/proc/{pid}")
        self.assertEqual(os.readlink(proc / "cwd"), "/")
        for number in range(3):
            self.assertEqual(os.readlink(proc / "fd" / str(number)), "/dev/null")
        self.assertNotEqual(os.getsid(pid), os.getsid(0))
        self.assertNotEqual(os.getsid(pid), pid)  # second fork
        stat = (proc / "stat").read_text().rsplit(")", 1)[1].split()
        self.assertEqual(int(stat[4]), 0)  # tty_nr
        self.assertIn("Umask:\t0077", (proc / "status").read_text())
        status = dict(line.split(":", 1) for line in (proc / "status").read_text().splitlines())
        blocked = int(status["SigBlk"].strip(), 16)
        for number in (signal.SIGHUP, signal.SIGTERM):
            self.assertTrue(blocked & (1 << (number - 1)))
        self.assertIn("openlog 24 gutsalov-lab1", self.messages())  # LOG_DAEMON
        self.assertIn("Daemon started", self.messages())
        self.stop(pid)
        self.assertEqual(self.pid_file.read_bytes(), b"")
        self.assertIn("SIGTERM received, daemon exiting", self.messages())
        self.assertIn("closelog", self.messages())

    def test_periodic_iteration(self):
        self.write_config(interval=1)
        self.start()
        self.wait_for(lambda: "Iteration completed" in self.messages())
        source = self.source / "later.log"
        source.write_bytes(b"later")
        self.wait_for(lambda: not source.exists(), timeout=3)
        self.assertEqual(self.output(), b"\n\nlater.log\n\nlater")

    def test_sighup_reloads_absolute_path_directories_and_interval(self):
        # Start with a relative config path; the daemon later changes cwd to '/'.
        result = self.run_binary("lab1.conf", "--pid-file", self.pid_file)
        self.assertEqual(result.returncode, 0, result.stderr)
        pid = int(self.pid_file.read_text())
        self.children.add(pid)
        self.wait_for(lambda: "Iteration completed" in self.messages())
        new_source = self.root / "new source"
        new_target = self.root / "new archive"
        new_source.mkdir()
        new_target.mkdir()
        self.write_config(source=new_source, target=new_target, interval=1)
        (new_source / "new.log").write_bytes(b"new")
        os.kill(pid, signal.SIGHUP)
        self.wait_for(lambda: not (new_source / "new.log").exists())
        self.assertEqual(self.output(new_target), b"\n\nnew.log\n\nnew")
        (new_source / "later.log").write_bytes(b"later")
        self.wait_for(lambda: not (new_source / "later.log").exists(), timeout=3)
        self.assertIn("Configuration reloaded", self.messages())
        self.assertFalse((self.target / "total.log").exists())

    def test_invalid_and_missing_reload_keep_previous_settings(self):
        self.write_config(interval=1)
        pid = self.start()
        self.wait_for(lambda: "Iteration completed" in self.messages())
        for index in range(2):
            with self.subTest(index=index):
                if index == 0:
                    self.config.write_text("interval=bad\n")
                else:
                    self.config.unlink()
                os.kill(pid, signal.SIGHUP)
                self.wait_for(lambda: self.messages().count("Configuration reload failed") >= index + 1)
                source = self.source / f"kept{index}.log"
                source.write_bytes(b"ok")
                self.wait_for(lambda: not source.exists(), timeout=3)
        self.assertTrue(Path(f"/proc/{pid}").exists())
        self.assertIn(b"kept1.log", self.output())

    def test_restart_replaces_pid_without_duplicate_processing(self):
        old_pid = self.start()
        self.wait_for(lambda: "Iteration completed" in self.messages())
        (self.source / "once.log").write_bytes(b"once")
        new_pid = self.start()
        self.assertNotEqual(old_pid, new_pid)
        self.wait_for(lambda: not Path(f"/proc/{old_pid}").exists())
        self.wait_for(lambda: not (self.source / "once.log").exists())
        self.assertEqual(self.output(), b"\n\nonce.log\n\nonce")
        self.assertEqual(int(self.pid_file.read_text()), new_pid)

    def test_invalid_start_does_not_stop_running_daemon(self):
        pid = self.start()
        self.config.write_text("broken\n")
        result = self.run_binary(self.config, "--pid-file", self.pid_file)
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(Path(f"/proc/{pid}").exists())
        self.assertEqual(int(self.pid_file.read_text()), pid)

    def test_stale_and_malformed_pid_files(self):
        for content in ("2147483647\n", "invalid\n", "-1\n", "0\n", "1\n", ""):
            with self.subTest(content=content):
                self.pid_file.write_text(content)
                pid = self.start()
                self.assertGreater(pid, 1)
                self.stop(pid)

    def test_foreign_pid_is_not_signalled(self):
        with subprocess.Popen(["sleep", "30"]) as foreign:
            try:
                self.pid_file.write_text(f"{foreign.pid}\n")
                result = self.run_binary(self.config, "--pid-file", self.pid_file)
                self.assertNotEqual(result.returncode, 0)
                self.assertIsNone(foreign.poll())
                self.assertIn("refusing to signal", self.messages())
            finally:
                foreign.terminate()

    def test_pid_file_symlink_is_rejected(self):
        victim = self.root / "victim"
        victim.write_text("do not change")
        self.pid_file.symlink_to(victim)
        result = self.run_binary(self.config, "--pid-file", self.pid_file)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(victim.read_text(), "do not change")

    def test_daemon_logs_iteration_errors_and_recovers(self):
        self.write_config(interval=1)
        (self.target / "total.log").mkdir()
        source = self.source / "retry.log"
        source.write_bytes(b"retry")
        self.start()
        self.wait_for(lambda: "retry.log:" in self.messages())
        self.assertEqual(source.read_bytes(), b"retry")
        (self.target / "total.log").rmdir()
        self.wait_for(lambda: not source.exists(), timeout=3)
        self.assertEqual(self.output(), b"\n\nretry.log\n\nretry")

    def test_inherited_file_descriptor_is_closed(self):
        with (self.root / "inherited.txt").open("wb") as inherited:
            pid = self.start(pass_fds=(inherited.fileno(),))
            descriptors = Path(f"/proc/{pid}/fd")
            targets = [os.readlink(entry) for entry in descriptors.iterdir()]
            self.assertNotIn(str(self.root / "inherited.txt"), targets)

    def test_start_with_closed_standard_descriptors(self):
        def close_standard_descriptors():
            for fd in range(3):
                os.close(fd)

        pid = self.start(preexec_fn=close_standard_descriptors)
        for fd in range(3):
            self.assertEqual(os.readlink(f"/proc/{pid}/fd/{fd}"), "/dev/null")

    def test_restart_after_sigkill_with_stale_pid(self):
        old_pid = self.start()
        self.allowed_kills.add(old_pid)
        os.kill(old_pid, signal.SIGKILL)
        self.wait_for(lambda: not Path(f"/proc/{old_pid}").exists())
        self.assertEqual(int(self.pid_file.read_text()), old_pid)
        new_pid = self.start()
        self.assertNotEqual(new_pid, old_pid)

    def test_control_script_start_status_reload_restart_stop(self):
        # Install into a temporary directory, just as build.sh installs lab1.
        installation = self.root / "installed lab"
        (installation / "scripts").mkdir(parents=True)
        self.binary = installation / "lab1"
        shutil.copy2(BINARY, self.binary)
        control = installation / "scripts" / "control.sh"
        shutil.copy2(CONTROL, control)

        def run_control(action):
            return subprocess.run(
                [str(control), action, str(self.config), str(self.pid_file)],
                cwd=self.root, env=self.env, capture_output=True,
                text=True, timeout=15,
            )

        self.assertEqual(run_control("status").returncode, 1)
        result = run_control("start")
        self.assertEqual(result.returncode, 0, result.stderr)
        pid = int(self.pid_file.read_text())
        self.children.add(pid)
        self.assertIn(str(pid), run_control("status").stdout)
        self.assertEqual(run_control("reload").returncode, 0)
        self.wait_for(lambda: "Configuration reloaded" in self.messages())
        self.assertEqual(run_control("restart").returncode, 0)
        new_pid = int(self.pid_file.read_text())
        self.children.add(new_pid)
        self.assertNotEqual(new_pid, pid)
        self.assertEqual(run_control("stop").returncode, 0)
        self.assertEqual(run_control("status").returncode, 1)
        self.wait_for(lambda: not Path(f"/proc/{new_pid}").exists())

    def test_signals_during_copy_are_consumed_after_iteration(self):
        source = self.source / "busy.log"
        content = b"still copying\n" * 100000
        source.write_bytes(content)
        with (self.target / "total.log").open("wb") as output:
            # Keep the worker inside appendFile while delivering signals.
            fcntl.flock(output, fcntl.LOCK_EX)
            pid = self.start()

            def source_is_open():
                for descriptor in Path(f"/proc/{pid}/fd").iterdir():
                    try:
                        if os.readlink(descriptor) == str(source):
                            return True
                    except FileNotFoundError:
                        pass
                return False

            self.wait_for(source_is_open)
            self.config.write_text("invalid config\n")
            for _ in range(20):
                os.kill(pid, signal.SIGHUP)
            os.kill(pid, signal.SIGTERM)
            self.assertTrue(source.exists())
            fcntl.flock(output, fcntl.LOCK_UN)
        self.wait_for(lambda: not Path(f"/proc/{pid}").exists())
        self.assertFalse(source.exists())
        self.assertEqual(self.output(), b"\n\nbusy.log\n\n" + content)
        self.assertNotIn("Configuration reload failed", self.messages())
        self.assertIn("SIGTERM received, daemon exiting", self.messages())


if __name__ == "__main__":
    unittest.main(verbosity=2)
