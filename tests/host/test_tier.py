"""Host tier smoke test: proves the tier runs, verifies no requirement."""

import shutil
import subprocess


def test_host_c_compiler_builds_and_runs(tmp_path):
    cc = shutil.which("cc") or shutil.which("gcc")
    assert cc, "no C compiler on PATH; the host tier cannot build pure cores"
    src = tmp_path / "t.c"
    src.write_text("int main(void) { return 42; }\n")
    exe = tmp_path / "t"
    subprocess.run([cc, str(src), "-o", str(exe)], check=True)
    assert subprocess.run([str(exe)]).returncode == 42
