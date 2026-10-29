"""Host tier: shared helper that builds a pure C core with a test harness."""

import pathlib
import shutil
import subprocess

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
COMPONENTS = ROOT / "components"


@pytest.fixture(scope="module")
def compile_harness(tmp_path_factory):
    """Return build(name, harness, srcs, include_dirs) -> run(*args) -> stdout.

    Sources and include dirs are paths relative to components/. If any source
    is missing the test is xfailed as not written yet, never passed.
    """

    def build(name, harness, srcs, include_dirs):
        srcs = [COMPONENTS / s for s in srcs]
        missing = [str(s.relative_to(ROOT)) for s in srcs if not s.exists()]
        if missing:
            pytest.xfail(f"not written yet: {', '.join(missing)}")
        cc = shutil.which("cc") or shutil.which("gcc")
        d = tmp_path_factory.mktemp(name)
        (d / "harness.c").write_text(harness)
        exe = d / name
        subprocess.run([cc, "-std=c11", "-Wall", "-Werror",
                        *(f"-I{COMPONENTS / i}" for i in include_dirs),
                        *map(str, srcs), str(d / "harness.c"), "-o", str(exe)], check=True)

        def run(*args):
            return subprocess.run([str(exe), *map(str, args)], capture_output=True,
                                  text=True, check=True).stdout
        return run

    return build
