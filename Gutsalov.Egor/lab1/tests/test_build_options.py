#!/usr/bin/env python3
"""Validate the public build switches without compiling another daemon."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

LAB = Path(__file__).resolve().parents[1]
NAMES = ("RELEASE", "ENABLE_TESTING", "ENABLE_ASAN", "ENABLE_TSAN")


class BuildOptionsTest(unittest.TestCase):
    def parse(self, *arguments, env=None):
        environment = {key: value for key, value in os.environ.items() if key not in NAMES}
        environment.update(env or {})
        return subprocess.run(
            ["bash", "-euc", 'source "$1"; shift; parse_build_options tests OFF ON "$@"; '
             'printf "%s\\n" "${CMAKE_OPTIONS[@]}" CTEST "${CTEST_OPTIONS[@]}"',
             "bash", str(LAB / "scripts/build_options.sh"), *arguments],
            env=environment, capture_output=True, text=True, timeout=10,
        )

    def test_defaults_and_command_line_override_environment(self):
        default = self.parse()
        self.assertEqual(default.returncode, 0, default.stderr)
        self.assertIn("-DRELEASE=OFF\n", default.stdout)
        self.assertIn("-DENABLE_TESTING=ON\n", default.stdout)
        result = self.parse("-DRELEASE=yes", "ENABLE_ASAN=true", "ENABLE_TSAN=0",
                            env={"RELEASE": "OFF", "ENABLE_ASAN": "OFF", "ENABLE_TSAN": "ON"})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("-DRELEASE=ON\n", result.stdout)
        self.assertIn("-DENABLE_ASAN=ON\n", result.stdout)
        self.assertIn("-DENABLE_TSAN=OFF\n", result.stdout)

    def test_ctest_arguments_are_forwarded(self):
        result = self.parse("RELEASE=OFF", "--", "-V", "-R", "name=value")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("CTEST\n-V\n-R\nname=value\n", result.stdout)

    def test_invalid_options_are_rejected_before_build(self):
        cases = (
            ("build.sh", "ENABLE_ASAN=ON", "ENABLE_TSAN=ON"),
            ("build.sh", "RELEASE=maybe"),
            ("build.sh", "ENABLE_UNKNOWN=ON"),
            ("run_tests.sh", "ENABLE_TESTING=OFF"),
        )
        for script, *arguments in cases:
            with self.subTest(script=script, arguments=arguments):
                result = subprocess.run(
                    [str(LAB / script), *arguments], capture_output=True, text=True,
                    timeout=10, env={key: value for key, value in os.environ.items() if key not in NAMES},
                )
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertNotIn("Build files have been written", result.stdout)

    def test_parent_cmake_switches_and_sanitizer_conflict(self):
        with tempfile.TemporaryDirectory(prefix="gutsalov-lab1-options-") as build:
            base = ["cmake", "-S", str(LAB.parent), "-B", build,
                    "-DENABLE_TESTING=OFF", "-DENABLE_ASAN=OFF", "-DENABLE_TSAN=OFF"]
            for release, expected in (("ON", "Release"), ("OFF", "Debug")):
                result = subprocess.run(base + [f"-DRELEASE={release}"], capture_output=True,
                                        text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                cache = (Path(build) / "CMakeCache.txt").read_text()
                self.assertIn(f"CMAKE_BUILD_TYPE:STRING={expected}\n", cache)
            result = subprocess.run(base + ["-DENABLE_ASAN=ON", "-DENABLE_TSAN=ON"],
                                    capture_output=True, text=True, timeout=30)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("mutually exclusive", result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
