"""Build and run the camera-relative movement regression."""
import os
import subprocess
from build_host_probe import PRIVATE, PROJECT


def main():
    env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(PRIVATE / "compiler-cache/global"), ZIG_LOCAL_CACHE_DIR=str(PRIVATE / "compiler-cache/local"))
    target = PRIVATE / "build/modern-core-test.exe"
    subprocess.run([str(PRIVATE / "toolchain/zig-x86_64-windows-0.17.0/zig.exe"), "cc", "-target", "x86-windows-gnu", "-O2", "-Wall", "-Wextra", str(PROJECT / "native/modern_core_test.c"), "-o", str(target)], env=env, check=True)
    subprocess.run([str(target)], check=True)


if __name__ == "__main__":
    main()
