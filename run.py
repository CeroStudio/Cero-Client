#!/usr/bin/env python3
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "tools", "build"))

from logger import step, ok, info, warn_, fail_, C_BOLD, C_GREEN, C_RESET
import check_deps
import prepare_dirs
import build_launcher
import build_tailwind
import package_assets
import finalize
import build_agent

def binary_path():
    if sys.platform == "win32":
        return os.path.join("bin", "windows", "client", "CeroClient.exe")
    if sys.platform == "darwin":
        return os.path.join("bin", "macos", "client", "CeroClient")
    if sys.platform.startswith("freebsd"):
        return os.path.join("bin", "freebsd", "client", "CeroClient")
    return os.path.join("bin", "linux", "client", "CeroClient")


def assets_path():
    return os.path.join("bin", "assets", "assets.dat")


def run_client(extra_args=None):
    path = binary_path()
    if not os.path.exists(path):
        fail_(f"{path} not found - build step did not produce a binary")

    assets = assets_path()
    if not os.path.exists(assets):
        fail_(f"{assets} not found - asset packaging did not produce a file")

    step("Launching client")
    info(path)
    result = subprocess.run([
        os.path.abspath(path),
        f"--customAssetsPath={os.path.abspath(assets)}",
        *(extra_args or []),
    ])
    if result.returncode != 0:
        warn_(f"Client exited with code {result.returncode}")
    else:
        ok("Client exited normally")


def parse_launch_arg(argv):
    for i, a in enumerate(argv):
        if a.startswith("--launch="):
            return [a]
        if a == "--launch" and i + 1 < len(argv):
            return [f"--launch={argv[i + 1]}"]
    return []


def main():
    start_time = time.time()
    step("CeroClient - client-only build")

    try:
        check_deps.run()
        prepare_dirs.run()
        build_launcher.run()
        build_agent.run()
        build_tailwind.run()
        package_assets.run()
        finalize.run()

        duration = time.time() - start_time
        print(f"\n{C_BOLD}{C_GREEN}Build finished successfully in {duration:.2f}s !{C_RESET}\n")

        run_client(parse_launch_arg(sys.argv[1:]))
    except SystemExit as e:
        sys.exit(e.code)
    except Exception as e:
        fail_(f"Unexpected error: {e}")


if __name__ == "__main__":
    main()