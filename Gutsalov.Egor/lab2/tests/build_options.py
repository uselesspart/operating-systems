import os
from pathlib import Path
import subprocess
import shutil
import tempfile
import unittest

LAB = Path(__file__).resolve().parents[1]


class BuildOptions(unittest.TestCase):
    def parse(self, arguments, env=None):
        environment = dict(os.environ)
        for key in ["RELEASE", "ENABLE_TESTING", "ENABLE_ASAN", "ENABLE_TSAN"]:
            environment.pop(key, None)
        environment.update(env or {})
        return subprocess.run([
            "bash", "-c", 'source "$1/scripts/build_options.sh"; shift; '
            'parse_build_options tests OFF ON "$@" || exit $?; '
            'declare -p CMAKE_OPTIONS CTEST_OPTIONS', "test", str(LAB), *arguments,
        ], capture_output=True, text=True, env=environment)

    def test_defaults_and_overrides(self):
        result = self.parse([])
        self.assertEqual(result.returncode, 0)
        self.assertIn("-DRELEASE=OFF", result.stdout)
        self.assertIn("-DENABLE_TESTING=ON", result.stdout)
        result = self.parse(["-DRELEASE=OFF", "ENABLE_ASAN=YES", "--", "-V", "-R", "unit"], {"RELEASE": "ON"})
        self.assertEqual(result.returncode, 0)
        for value in ["-DRELEASE=OFF", "-DENABLE_ASAN=ON", "-V", "unit"]:
            self.assertIn(value, result.stdout)

    def test_rejections(self):
        for arguments in [["ENABLE_ASAN=ON", "ENABLE_TSAN=ON"], ["ENABLE_TESTING=OFF"],
                          ["RELEASE=maybe"], ["UNKNOWN=ON"]]:
            self.assertNotEqual(self.parse(arguments).returncode, 0)

    def test_cmake_discovers_new_sources_and_transport(self):
        with tempfile.TemporaryDirectory(prefix="gutsalov-lab2-discovery-") as temporary:
            source = Path(temporary) / "source"
            build = Path(temporary) / "build"
            source.mkdir()
            shutil.copy2(LAB / "CMakeLists.txt", source)
            for directory in ["src", "include", "cmake"]:
                shutil.copytree(LAB / directory, source / directory)
            def run(arguments):
                result = subprocess.run(arguments, text=True, capture_output=True, timeout=60)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            run(["cmake", "-S", str(source), "-B", str(build), "-DENABLE_TESTING=OFF"])
            # Add sources after configuration: make must notice them itself.
            shutil.copy2(source / "src/conn_sock.cpp", source / "src/conn_extra.cpp")
            (source / "src/new_source.cpp").write_text("int discovery_probe() { return 42; }\n")
            run(["cmake", "--build", str(build), "--parallel", "2"])
            for role in ["host", "client"]:
                self.assertTrue((build / "bin" / f"{role}_extra").is_file())
            self.assertTrue(list(build.rglob("new_source.cpp.o")))


if __name__ == "__main__":
    unittest.main()
