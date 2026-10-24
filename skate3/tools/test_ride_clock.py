"""Build/run the native 60 Hz clock regression without launching Gothic."""
import os
import subprocess
from build_host_probe import PRIVATE, PROJECT


def main():
    zig = PRIVATE / "toolchain/zig-x86_64-windows-0.17.0/zig.exe"
    output = PRIVATE / "build/ride-clock-test.exe"
    env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(PRIVATE / "compiler-cache/global"),
               ZIG_LOCAL_CACHE_DIR=str(PRIVATE / "compiler-cache/local"))
    subprocess.run([str(zig), "cc", "-target", "x86-windows-gnu", "-O2", "-Wall", "-Wextra",
                    str(PROJECT / "native/ride_clock_test.c"), "-o", str(output)], env=env, check=True)
    subprocess.run([str(output)], check=True)


if __name__ == "__main__":
    main()
