"""jetio-dev: thin wrapper around CMake presets, clang-format and the benchmarks."""

from __future__ import annotations

import argparse
import hashlib
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SOURCE_DIRS = ("include", "src", "tests", "tools")
CPP_SUFFIXES = {".hpp", ".cpp", ".h", ".cc", ".ixx", ".cppm"}

# The Docker build environment: the image is made from these files and named after their content.
IMAGE_FILES = ("tools/docker/Dockerfile", "tools/setup/linux-apt.sh")
# Build trees of the container go to build/docker/<preset>, those of the host to build/<preset>
# (the binaryDir of the presets reads this variable).
DOCKER_BUILD_ENV = "docker/"
# The default seccomp profile of Docker, plus the personality call that TSan needs (see README.md).
SECCOMP_PROFILE = ROOT / "tools" / "docker" / "seccomp.json"

use_docker = False


def default_preset() -> str:
    system = platform.system().lower()
    if system == "linux":
        return "linux-x64-gcc-debug"
    raise SystemExit(f"no default preset for {system!r}; pass --preset")


def image_name() -> str:
    digest = hashlib.sha256()
    for name in IMAGE_FILES:
        digest.update((ROOT / name).read_bytes())
    return f"jetio-dev:{digest.hexdigest()[:12]}"


def ensure_image() -> str:
    """The image of the build environment, made if it does not exist yet."""
    image = image_name()
    exists = subprocess.run(
        ("docker", "image", "inspect", image), capture_output=True, check=False
    ).returncode == 0
    if not exists:
        build = ("docker", "build", "-t", image, "-f", "tools/docker/Dockerfile", "tools")
        print("+", " ".join(build), flush=True)
        if subprocess.run(build, cwd=ROOT, check=False).returncode:
            raise SystemExit("building the Docker image failed")
    return image


def in_docker(cmd: tuple[str | Path, ...]) -> tuple[str | Path, ...]:
    """The command run in the container: the same user and the same path to the checkout."""
    if shutil.which("docker") is None:
        raise SystemExit("docker not found; install it, or build without it (--no-docker)")
    tty = ("-t",) if sys.stdin.isatty() and sys.stdout.isatty() else ()
    return (
        "docker", "run", "--rm", "-i", *tty,
        "--user", f"{os.getuid()}:{os.getgid()}",
        "--security-opt", f"seccomp={SECCOMP_PROFILE}",
        "-e", "HOME=/tmp",
        "-e", f"JETIO_BUILD_ENV={DOCKER_BUILD_ENV}",
        "-v", f"{ROOT}:{ROOT}",
        "-w", str(ROOT),
        ensure_image(),
        *cmd,
    )


def build_dir(preset: str) -> Path:
    return ROOT / "build" / (DOCKER_BUILD_ENV if use_docker else "") / preset


def run(*cmd: str | Path) -> int:
    if use_docker:
        cmd = in_docker(cmd)
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
    clang_format = "clang-format" if use_docker else shutil.which("clang-format")
    if clang_format is None:
        print("clang-format not found", file=sys.stderr)
        return 1
    files = sorted(
        p for d in SOURCE_DIRS for p in (ROOT / d).rglob("*") if p.suffix in CPP_SUFFIXES
    )
    mode = ["--dry-run", "--Werror"] if args.check else ["-i"]
    return run(clang_format, *mode, *files) if files else 0


def cmd_clean(args: argparse.Namespace) -> int:
    path = build_dir(args.preset)
    if path.exists():
        shutil.rmtree(path)
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="jetio-dev", description=__doc__)
    parser.add_argument(
        "--no-docker",
        action="store_true",
        help="use the tools of this machine (see tools/setup) instead of the Docker image; "
        "also JETIO_DOCKER=0",
    )
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
    global use_docker
    args = build_parser().parse_args(argv)
    use_docker = (
        platform.system().lower() == "linux"
        and not args.no_docker
        and os.environ.get("JETIO_DOCKER") != "0"
    )
    if hasattr(args, "preset") and args.preset is None:
        args.preset = default_preset()
    return args.func(args)
