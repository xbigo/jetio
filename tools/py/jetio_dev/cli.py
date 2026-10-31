"""jetio-dev: thin wrapper around CMake presets, clang-format and the benchmarks."""

from __future__ import annotations

import argparse
import platform
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SOURCE_DIRS = ("include", "src", "tests", "tools")
CPP_SUFFIXES = {".hpp", ".cpp", ".h", ".cc", ".ixx", ".cppm"}


def default_preset() -> str:
    system = platform.system().lower()
    if system == "linux":
        return "linux-x64-gcc-debug"
    raise SystemExit(f"no default preset for {system!r}; pass --preset")


def run(*cmd: str | Path) -> int:
    print("+", " ".join(str(c) for c in cmd), flush=True)
    return subprocess.run(cmd, cwd=ROOT, check=False).returncode


def cmd_list(_: argparse.Namespace) -> int:
    return run("cmake", "--list-presets")


def cmd_configure(args: argparse.Namespace) -> int:
    return run("cmake", "--preset", args.preset, *args.extra)


def cmd_build(args: argparse.Namespace) -> int:
    target = ["--target", args.target] if getattr(args, "target", None) else []
    return run("cmake", "--build", "--preset", args.preset, *target, *args.extra)


def cmd_test(args: argparse.Namespace) -> int:
    return run("ctest", "--preset", args.preset, *args.extra)


def cmd_check(args: argparse.Namespace) -> int:
    """Configure, build and test one preset; the unit tests must meet the time limit."""
    for step in (cmd_configure, cmd_build):
        status = step(args)
        if status:
            return status
    return run("cmake", "--build", "--preset", args.preset, "--target", "jetio_check")


def cmd_bench(args: argparse.Namespace) -> int:
    return run("ctest", "--preset", f"{args.preset}-bench", "--verbose", *args.extra)


def cmd_format(args: argparse.Namespace) -> int:
    clang_format = shutil.which("clang-format")
    if clang_format is None:
        print("clang-format not found", file=sys.stderr)
        return 1
    files = sorted(
        p for d in SOURCE_DIRS for p in (ROOT / d).rglob("*") if p.suffix in CPP_SUFFIXES
    )
    mode = ["--dry-run", "--Werror"] if args.check else ["-i"]
    return run(clang_format, *mode, *files) if files else 0


def cmd_clean(args: argparse.Namespace) -> int:
    path = ROOT / "build" / args.preset
    if path.exists():
        shutil.rmtree(path)
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="jetio-dev", description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    def add(name: str, func, help_: str, preset: bool = True) -> argparse.ArgumentParser:
        p = sub.add_parser(name, help=help_)
        p.set_defaults(func=func)
        if preset:
            p.add_argument("-p", "--preset", default=None, help="CMake preset (default: host)")
            p.add_argument("extra", nargs="*", help="extra arguments passed to the tool")
        return p

    add("list", cmd_list, "list CMake presets", preset=False)
    add("configure", cmd_configure, "cmake --preset")
    build = add("build", cmd_build, "cmake --build --preset")
    build.add_argument("-t", "--target", help="build only this target")
    add("test", cmd_test, "ctest --preset")
    add("check", cmd_check, "configure + build + unit tests with the time limit")
    add("bench", cmd_bench, "run the micro-benchmarks (release presets)")
    add("clean", cmd_clean, "remove the build directory of a preset")
    fmt = add("format", cmd_format, "run clang-format", preset=False)
    fmt.add_argument("--check", action="store_true", help="only report differences")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if hasattr(args, "preset") and args.preset is None:
        args.preset = default_preset()
    return args.func(args)
