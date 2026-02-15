#!/usr/bin/env python3
"""
Build script for compiling Pure Data patches to RP2350 firmware.

This script:
1. Takes a PD patch file as input
2. Creates a build directory
3. Compiles the patch using hvcc
4. Integrates the generated code with the core firmware project
5. Builds the firmware using CMake/pico-sdk
"""

from __future__ import annotations

import argparse
import logging
import os
import shutil
import stat
import subprocess
import sys
import threading
import time
from contextlib import contextmanager
from pathlib import Path
from typing import Iterable, List, Optional, Sequence, Tuple


LOGGER_NAME = "build_firmware"
DEFAULT_LOG_LEVEL = "normal"

# CMake feature options exposed as first-class CLI flags.
# These are declared as `option(...)` in `core/CMakeLists.txt`.
# Single-argument semantics:
# - Defaults come from CMake when flag is omitted.
# - WS2812 is ON by default -> provide --no-ws2812 to disable.
# - Other features are OFF by default -> provide --<feature> to enable.
FEATURE_OPTIONS = [
    {
        "flag": "--no-ws2812",
        "cmake_var": "ENABLE_WS2812",
        "action": "store_false",
        "help": "Disable WS2812 LED support (CMake: ENABLE_WS2812).",
    },
    {
        "flag": "--usb-midi",
        "cmake_var": "ENABLE_USB_MIDI",
        "action": "store_true",
        "help": "Enable USB MIDI support (CMake: ENABLE_USB_MIDI).",
    },
    {
        "flag": "--oled",
        "cmake_var": "ENABLE_OLED",
        "action": "store_true",
        "help": "Enable SSD1306 OLED support (CMake: ENABLE_OLED).",
    },
    {
        "flag": "--mpr121",
        "cmake_var": "ENABLE_MPR121",
        "action": "store_true",
        "help": "Enable MPR121 capacitive touch support (CMake: ENABLE_MPR121).",
    },
]

# RP2350 overclock presets exposed as convenience CLI flags.
FW_SYS_CLOCK_PROFILE_OC_240MHZ = "FW_SYS_CLOCK_PROFILE_OC_240MHZ"
DEFAULT_OC_FLASH_SPI_CLKDIV = "4"

# Console verbosity mapping. File logging follows selected log level.
LOG_LEVELS = {
    "quiet": logging.ERROR,
    "normal": logging.INFO,
    "verbose": logging.DEBUG,
    "debug": logging.DEBUG,
}

FILE_LOG_LEVELS = {
    "quiet": logging.ERROR,
    "normal": logging.INFO,
    "verbose": logging.DEBUG,
    "debug": logging.DEBUG,
}


def get_logger() -> logging.Logger:
    return logging.getLogger(LOGGER_NAME)


def configure_logging(log_level: str, log_file: Optional[Path] = None) -> logging.Logger:
    """Configure console logging and (optionally) file logging."""
    logger = get_logger()
    logger.setLevel(logging.DEBUG)
    logger.propagate = False

    # Clear existing handlers to avoid duplicate logs on reconfigure.
    for handler in list(logger.handlers):
        logger.removeHandler(handler)

    console_level = LOG_LEVELS.get(log_level, LOG_LEVELS[DEFAULT_LOG_LEVEL])
    console_handler = logging.StreamHandler(stream=sys.stdout)
    console_handler.setLevel(console_level)
    console_handler.setFormatter(logging.Formatter("%(levelname)s: %(message)s"))
    logger.addHandler(console_handler)

    if log_file:
        attach_file_logger(logger, log_file, log_level)

    return logger


def attach_file_logger(logger: logging.Logger, log_file: Path, log_level: str) -> None:
    """Attach a file handler matching selected log level."""
    log_file.parent.mkdir(parents=True, exist_ok=True)
    for handler in logger.handlers:
        if isinstance(handler, logging.FileHandler):
            try:
                if Path(handler.baseFilename) == log_file:
                    return
            except Exception:
                continue

    file_handler = logging.FileHandler(log_file, encoding="utf-8")
    file_handler.setLevel(FILE_LOG_LEVELS.get(log_level, logging.INFO))
    file_handler.setFormatter(
        logging.Formatter("%(asctime)s %(levelname)s %(message)s")
    )
    logger.addHandler(file_handler)


@contextmanager
def log_step(logger: logging.Logger, title: str):
    """Log a step with timing."""
    start = time.monotonic()
    logger.info("== %s ==", title)
    try:
        yield
    except Exception:
        elapsed = time.monotonic() - start
        logger.error("Step failed: %s (%.2fs)", title, elapsed)
        raise
    else:
        elapsed = time.monotonic() - start
        logger.info("Step done: %s (%.2fs)", title, elapsed)


def format_cmd(cmd: Sequence[str]) -> str:
    """Format a command for readable logging."""
    try:
        return subprocess.list2cmdline(list(cmd))
    except Exception:
        return " ".join(str(c) for c in cmd)


def run_cmd(
    cmd: Sequence[str],
    logger: logging.Logger,
    log_level: str,
    *,
    cwd: Optional[Path] = None,
    env: Optional[dict] = None,
    label: Optional[str] = None,
    check: bool = True,
) -> subprocess.CompletedProcess:
    """
    Run a command and route output by log_level.
    Full stdout/stderr logged at DEBUG. quiet/normal: captured and filtered;
    verbose/debug: streamed in real time.
    """
    if label and log_level in ("verbose", "debug"):
        logger.debug("%s", label)

    if log_level == "debug":
        logger.debug("Command: %s", format_cmd(cmd))
        if cwd:
            logger.debug("CWD: %s", cwd)

    if log_level in ("verbose", "debug"):
        return _run_cmd_streaming(
            cmd,
            logger,
            cwd=cwd,
            env=env,
            check=check,
        )

    result = subprocess.run(
        list(cmd),
        check=False,
        capture_output=True,
        text=True,
        cwd=str(cwd) if cwd else None,
        env=env,
    )

    stdout_lines = result.stdout.splitlines() if result.stdout else []
    stderr_lines = result.stderr.splitlines() if result.stderr else []

    for line in stdout_lines:
        logger.debug("stdout: %s", line)
    for line in stderr_lines:
        logger.debug("stderr: %s", line)

    if log_level in ("normal", "quiet"):
        _emit_filtered_output(logger, stdout_lines + stderr_lines)

    if check and result.returncode != 0:
        raise subprocess.CalledProcessError(
            result.returncode, list(cmd), result.stdout, result.stderr
        )

    return result


def _run_cmd_streaming(
    cmd: Sequence[str],
    logger: logging.Logger,
    *,
    cwd: Optional[Path] = None,
    env: Optional[dict] = None,
    check: bool = True,
) -> subprocess.CompletedProcess:
    process = subprocess.Popen(
        list(cmd),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1,
        cwd=str(cwd) if cwd else None,
        env=env,
    )

    stdout_lines: List[str] = []
    stderr_lines: List[str] = []

    def _consume(pipe, store: List[str]) -> None:
        for line in iter(pipe.readline, ""):
            store.append(line)
            stripped = line.rstrip("\n")
            if stripped == "":
                continue
            _log_classified_line(logger, stripped)
        pipe.close()

    threads = [
        threading.Thread(
            target=_consume, args=(process.stdout, stdout_lines), daemon=True
        ),
        threading.Thread(
            target=_consume, args=(process.stderr, stderr_lines), daemon=True
        ),
    ]
    for t in threads:
        t.start()
    for t in threads:
        t.join()

    returncode = process.wait()
    stdout = "".join(stdout_lines)
    stderr = "".join(stderr_lines)

    if check and returncode != 0:
        raise subprocess.CalledProcessError(returncode, list(cmd), stdout, stderr)

    return subprocess.CompletedProcess(list(cmd), returncode, stdout, stderr)


def _emit_filtered_output(logger: logging.Logger, lines: Iterable[str]) -> None:
    for line in lines:
        level = _classify_line(line)
        if level == "info":
            continue
        _log_classified_line(logger, line)


def _classify_line(line: str) -> str:
    upper = line.upper()
    if "ERROR" in upper or "FAILED" in upper:
        return "error"
    if "WARNING" in upper or "NOT FOUND" in upper:
        return "warning"
    return "info"


def _log_classified_line(logger: logging.Logger, line: str) -> None:
    level = _classify_line(line)
    if level == "error":
        logger.error("%s", line)
    elif level == "warning":
        logger.warning("%s", line)
    else:
        logger.info("%s", line)


def get_project_root() -> Path:
    """Get the project root directory."""
    script_dir = Path(__file__).parent
    return script_dir.parent


def get_hvcc_search_paths() -> List[str]:
    """Return hvcc abstraction search paths: HVCC_SEARCH_PATHS env + vendored third_party/heavylib only."""
    paths: List[Path] = []

    env_paths = os.environ.get("HVCC_SEARCH_PATHS", "").strip()
    if env_paths:
        for p in env_paths.split(";"):
            p = p.strip().strip('"')
            if p:
                paths.append(Path(p))

    project_root = get_project_root()
    vendored_heavylib = project_root / "third_party" / "heavylib"
    if vendored_heavylib.exists():
        paths.append(vendored_heavylib)
        paths.append(vendored_heavylib / "hv.lfo")
        paths.append(vendored_heavylib / "hv.osc")
        paths.append(vendored_heavylib / "hv.filters")

    existing: List[str] = []
    seen = set()
    for p in paths:
        try:
            if p.exists() and p.is_dir():
                s = str(p)
                if s not in seen:
                    existing.append(s)
                    seen.add(s)
        except Exception:
            continue

    return existing


def clean_build_directory(
    build_base_dir: Path, logger: logging.Logger, *, force: bool = False
) -> bool:
    """Clean build directory, handling permission errors gracefully."""
    if not build_base_dir.exists():
        return True

    def _on_rmtree_error(func, path, exc_info):  # type: ignore[no-untyped-def]
        # Windows may mark files read-only (or external tools may); clear the bit and retry.
        try:
            os.chmod(path, stat.S_IWRITE)
        except Exception:
            pass
        func(path)

    try:
        # Retry a couple of times in case of transient file locks (indexer/AV/IDE).
        last_err: Optional[BaseException] = None
        for attempt in range(3):
            try:
                shutil.rmtree(build_base_dir, onerror=_on_rmtree_error)
                logger.debug("Cleaned build directory: %s", build_base_dir)
                return True
            except PermissionError as e:
                last_err = e
                if attempt < 2:
                    time.sleep(0.25)
                    continue
                raise
            except OSError as e:
                last_err = e
                if attempt < 2:
                    time.sleep(0.25)
                    continue
                raise

        if last_err:
            raise last_err
        logger.debug("Cleaned build directory: %s", build_base_dir)
        return True
    except PermissionError as e:
        if force:
            logger.warning("Could not delete %s: %s", build_base_dir, e)
            logger.warning("Some files may be locked. Trying to continue...")
            return False
        logger.error("Could not delete %s: %s", build_base_dir, e)
        logger.error("Please close any programs using files in this directory")
        return False
    except Exception as e:
        logger.warning("Error cleaning build directory: %s", e)
        return False


def _default_build_base_dir(project_root: Path) -> Path:
    # Long-term default: keep "build/" as canonical build output directory.
    # (Users can always override via --output.)
    return project_root / "build"


def _clean_patch_build_dir(build_dir: Path, logger: logging.Logger) -> None:
    """
    Clean only key generated artifacts for one patch build directory.

    We intentionally do NOT delete the whole patch directory (or the log file),
    to avoid Windows file-lock issues (e.g. build_firmware.log opened in an editor).
    """
    if not build_dir.exists():
        return

    # Remove Heavy output and manifests
    heavy_dir = build_dir / "c"
    if heavy_dir.exists():
        clean_build_directory(heavy_dir, logger, force=True)

    # Remove firmware build dirs (including timestamped fallbacks)
    for fw_dir in [build_dir / "firmware-build", *sorted(build_dir.glob("firmware-build-*"))]:
        if fw_dir.exists():
            clean_build_directory(fw_dir, logger, force=True)


def create_build_dir(
    patch_name: str,
    logger: logging.Logger,
    *,
    output_dir: Optional[Path] = None,
) -> Path:
    """Create a build directory for this patch."""
    project_root = get_project_root()

    if output_dir is None:
        build_base_dir = _default_build_base_dir(project_root)
    else:
        build_base_dir = Path(output_dir)

    build_dir = build_base_dir / patch_name
    build_dir.mkdir(parents=True, exist_ok=True)
    return build_dir


def compile_patch(
    pd_file: Path,
    build_dir: Path,
    patch_name: str,
    logger: logging.Logger,
    log_level: str,
) -> bool:
    """Compile PD patch using hvcc."""
    logger.debug("Compiling PD patch: %s", pd_file)

    hvcc_cmd = ["hvcc", str(pd_file), "-o", str(build_dir), "-n", "patch"]

    search_paths = get_hvcc_search_paths()
    if search_paths:
        logger.debug("hvcc search paths:")
        for p in search_paths:
            logger.debug("  - %s", p)
        hvcc_cmd.extend(["-p", *search_paths])

    try:
        run_cmd(
            hvcc_cmd,
            logger,
            log_level,
            label="hvcc compile",
        )
        logger.debug("hvcc compilation successful")
        return True
    except subprocess.CalledProcessError:
        logger.error("hvcc compilation failed")
        return False
    except FileNotFoundError:
        logger.error("hvcc not found. Make sure it's installed and in PATH.")
        logger.error("Install with: pip install -r requirements.txt")
        logger.error(
            "If you use the repo venv, activate it first (Windows: .\\venv\\Scripts\\Activate.ps1 | macOS/Linux: source venv/bin/activate)."
        )
        return False


def write_heavy_sources_manifest(
    heavy_dir: Path, logger: logging.Logger
) -> Optional[Path]:
    """Write a CMake manifest listing Heavy-generated sources."""
    heavy_dir = heavy_dir.resolve()
    c_files = sorted(heavy_dir.glob("*.c"))
    cpp_files = sorted(heavy_dir.glob("*.cpp"))
    sources = c_files + cpp_files
    if not sources:
        logger.error("No Heavy source files found in: %s", heavy_dir)
        return None

    manifest_path = heavy_dir / "heavy_sources.cmake"
    lines = [
        "# Auto-generated by build_firmware.py. Do not edit.",
        "set(HEAVY_SOURCES",
    ]
    for path in sources:
        lines.append(f'  "{path.as_posix()}"')
    lines.append(")")
    lines.append("set(HEAVY_INCLUDE_DIRS")
    lines.append(f'  "{heavy_dir.as_posix()}"')
    lines.append(")")
    manifest_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    logger.debug("Heavy sources manifest: %s", manifest_path)
    return manifest_path


def resolve_pico_sdk_path(project_root: Path, logger: logging.Logger) -> Optional[str]:
    """Resolve Pico SDK path with preference for local submodule."""
    local_sdk_path = project_root / "sdk" / "pico-sdk"
    pico_sdk_path = os.environ.get("PICO_SDK_PATH")

    if local_sdk_path.exists() and (local_sdk_path / "pico_sdk_init.cmake").exists():
        resolved = str(local_sdk_path.resolve())
        logger.debug("Using local pico-sdk from submodule: %s", resolved)
        return resolved

    if pico_sdk_path:
        logger.debug("Using PICO_SDK_PATH from environment: %s", pico_sdk_path)
        logger.warning("Consider using local submodule for better version compatibility")
        return pico_sdk_path

    logger.warning("PICO_SDK_PATH not set and local SDK not found.")
    logger.warning("Expected SDK at: %s", local_sdk_path)
    logger.warning("Initialize submodule with: git submodule update --init --recursive")
    logger.warning("Or set PICO_SDK_PATH environment variable")
    return None


def resolve_pico_extras_path(project_root: Path) -> Optional[str]:
    """Resolve Pico extras path if local submodule exists."""
    local_extras_path = project_root / "sdk" / "pico-extras"
    if local_extras_path.exists() and (local_extras_path / "CMakeLists.txt").exists():
        return str(local_extras_path.resolve())
    return None


def resolve_host_compilers(logger: logging.Logger) -> Tuple[Optional[str], Optional[str]]:
    """Prefer GCC for host tools compilation if available."""
    gcc_path = shutil.which("gcc")
    gxx_path = shutil.which("g++")
    if gcc_path and gxx_path:
        logger.debug("Using GCC for host tools: %s", gcc_path)
        return gcc_path, gxx_path
    return None, None


def _exe_suffix() -> str:
    return ".exe" if os.name == "nt" else ""


def _venv_bin_dir(venv_root: Path) -> Path:
    return venv_root / ("Scripts" if os.name == "nt" else "bin")


def _is_python_launcher_exe(exe_path: Path) -> bool:
    """
    Detect distlib launcher stubs (pip-generated *.exe wrappers on Windows).

    We prefer a real ninja binary for CMAKE_MAKE_PROGRAM to avoid launcher
    recursion in mixed Python installations.
    """
    if os.name != "nt":
        return False
    try:
        with exe_path.open("rb") as f:
            # Launcher stubs contain this stable marker near the front.
            head = f.read(262_144)
        return b"Fatal error in launcher" in head
    except OSError:
        return False


def resolve_ninja_make_program(logger: logging.Logger) -> Optional[str]:
    """
    Resolve a standalone ninja executable path (not a Python launcher stub).

    Returns:
      - absolute path to usable ninja executable
      - None if no safe candidate is found
    """
    ninja_name = f"ninja{_exe_suffix()}"
    candidates: List[Path] = []
    seen = set()

    def _add_candidate(path_like: Optional[str]) -> None:
        if not path_like:
            return
        try:
            p = Path(path_like).resolve()
        except Exception:
            p = Path(path_like)
        if not p.exists():
            return
        key = str(p).lower() if os.name == "nt" else str(p)
        if key in seen:
            return
        seen.add(key)
        candidates.append(p)

    # Prefer isolated env first (active venv, then repo-local venv).
    active_venv = os.environ.get("VIRTUAL_ENV", "").strip().strip('"')
    if active_venv:
        _add_candidate(str(_venv_bin_dir(Path(active_venv)) / ninja_name))

    _add_candidate(str(_venv_bin_dir(get_project_root() / "venv") / ninja_name))

    # Then PATH / interpreter-adjacent locations.
    _add_candidate(shutil.which("ninja"))
    _add_candidate(shutil.which("ninja-build"))
    _add_candidate(str(Path(sys.executable).resolve().parent / ninja_name))
    if os.name == "nt":
        # Typical system Ninja location on Windows.
        _add_candidate(r"C:\ProgramData\chocolatey\bin\ninja.exe")

    for candidate in candidates:
        if _is_python_launcher_exe(candidate):
            logger.warning("Skipping Python launcher stub for Ninja: %s", candidate)
            continue
        logger.debug("Using Ninja executable: %s", candidate)
        return str(candidate)

    if candidates:
        logger.error("Found only Python launcher stubs for Ninja, no standalone ninja binary.")
        for candidate in candidates:
            logger.error("  candidate: %s", candidate)
    else:
        logger.error("ninja executable not found.")
    logger.error(
        "Install/use a real Ninja binary (e.g. venv Scripts/ninja.exe or Chocolatey Ninja) and retry."
    )
    return None


def _find_in_pico_toolchain_path(exe_stem: str, logger: logging.Logger) -> Optional[str]:
    """
    Search for `exe_stem` under PICO_TOOLCHAIN_PATH.

    Pico SDK expects PICO_TOOLCHAIN_PATH to point to the toolchain root and searches
    `${PICO_TOOLCHAIN_PATH}/bin` for compilers.
    """
    pico_toolchain_path = os.environ.get("PICO_TOOLCHAIN_PATH", "").strip().strip('"')
    if not pico_toolchain_path:
        return None

    toolchain_root = Path(pico_toolchain_path)
    candidate = toolchain_root / "bin" / f"{exe_stem}{_exe_suffix()}"
    if candidate.exists():
        return str(candidate)

    # Common mistake: set PICO_TOOLCHAIN_PATH to ".../bin" instead of the toolchain root.
    candidate = toolchain_root / f"{exe_stem}{_exe_suffix()}"
    if candidate.exists():
        logger.warning(
            "PICO_TOOLCHAIN_PATH looks like a bin directory; expected toolchain root (containing bin/). Value: %s",
            toolchain_root,
        )
        return str(candidate)

    return None


def preflight_check_toolchains(
    logger: logging.Logger,
    forced_make_program: Optional[str] = None,
) -> Tuple[bool, Optional[str]]:
    """
    Check for external toolchains that are not installed via requirements.txt.

    - ARM cross compiler: required for firmware build (arm-none-eabi-*)
    - Host C/C++ compiler: required to build Pico SDK host tools (e.g. pioasm)
    """
    ok = True

    arm_gcc = shutil.which("arm-none-eabi-gcc") or _find_in_pico_toolchain_path(
        "arm-none-eabi-gcc", logger
    )
    arm_gxx = shutil.which("arm-none-eabi-g++") or _find_in_pico_toolchain_path(
        "arm-none-eabi-g++", logger
    )

    if arm_gcc:
        logger.info("ARM GCC: %s", arm_gcc)
    else:
        logger.error("arm-none-eabi-gcc not found (required).")
        ok = False

    if arm_gxx:
        logger.info("ARM G++: %s", arm_gxx)
    else:
        logger.error("arm-none-eabi-g++ not found (required).")
        ok = False

    if not ok:
        logger.error("Install GNU Arm Embedded Toolchain and ensure it's in PATH, or set PICO_TOOLCHAIN_PATH.")

    ninja_make_program: Optional[str] = None
    if forced_make_program:
        candidate = Path(forced_make_program).expanduser()
        if candidate.exists():
            candidate = candidate.resolve()
            if _is_python_launcher_exe(candidate):
                logger.warning("Configured CMAKE_MAKE_PROGRAM looks like a Python launcher stub: %s", candidate)
            ninja_make_program = str(candidate)
            logger.info("Ninja (from CMAKE_MAKE_PROGRAM): %s", ninja_make_program)
        else:
            logger.error("Configured CMAKE_MAKE_PROGRAM does not exist: %s", candidate)
            ok = False
    else:
        ninja_make_program = resolve_ninja_make_program(logger)
        if ninja_make_program:
            logger.info("Ninja: %s", ninja_make_program)
        else:
            ok = False

    # Host compiler for Pico SDK tools (pioasm, etc.). The build prefers GCC if present;
    # otherwise CMake will pick a suitable toolchain (e.g. clang++ or MSVC).
    host_cc = shutil.which("gcc")
    host_cxx = shutil.which("g++")
    if host_cc and host_cxx:
        logger.info("Host compiler (GCC): %s / %s", host_cc, host_cxx)
    else:
        detected_cxx = (
            shutil.which("clang++")
            or shutil.which("cl")
            or shutil.which("g++")
        )
        if detected_cxx:
            logger.info("Host C++ compiler (CMake default): %s", detected_cxx)
        else:
            logger.warning(
                "Host C++ compiler not found on PATH (clang++/g++/cl). Pico SDK builds host tools (e.g. pioasm) during configure."
            )
            logger.warning(
                "Install LLVM (clang) or Visual Studio Build Tools, or run from a Developer Command Prompt."
            )

    return ok, ninja_make_program


def find_uf2_file(build_dir: Path) -> Optional[Path]:
    """Find UF2 output file with common naming patterns."""
    preferred = [
        build_dir / "rp2350_puredata_firmware.elf.uf2",
        build_dir / "rp2350_puredata_firmware.uf2",
    ]
    for path in preferred:
        if path.exists():
            return path
    candidates = sorted(build_dir.glob("*.uf2"))
    return candidates[0] if candidates else None


def build_firmware(
    source_dir: Path,
    build_dir: Path,
    heavy_sources_file: Path,
    logger: logging.Logger,
    ninja_make_program: str,
    log_level: str = DEFAULT_LOG_LEVEL,
    cmake_defines: Optional[List[str]] = None,
) -> bool:
    """Build firmware using CMake."""
    logger.debug("Building firmware with CMake...")

    build_dir.mkdir(parents=True, exist_ok=True)
    project_root = get_project_root()

    pico_sdk_path = resolve_pico_sdk_path(project_root, logger)
    pico_extras_path = resolve_pico_extras_path(project_root)
    gcc_path, gxx_path = resolve_host_compilers(logger)
    cmake_args = [
        "cmake",
        "-G",
        "Ninja",
        "-S",
        str(source_dir),
        "-B",
        str(build_dir),
        "-DPICO_BOARD=waveshare_rp2350_zero",
        f"-DCMAKE_MAKE_PROGRAM={ninja_make_program}",
    ]

    if pico_sdk_path:
        cmake_args.append(f"-DPICO_SDK_PATH={pico_sdk_path}")

    if pico_extras_path:
        cmake_args.append(f"-DPICO_EXTRAS_PATH={pico_extras_path}")

    cmake_args.append(f"-DPROJECT_ROOT={project_root.resolve()}")
    cmake_args.append(f"-DHEAVY_SOURCES_FILE={heavy_sources_file.resolve()}")

    if cmake_defines:
        for define in cmake_defines:
            if define.startswith("CMAKE_MAKE_PROGRAM=") or define.startswith("CMAKE_MAKE_PROGRAM:"):
                logger.debug("Ignoring duplicate user define for CMAKE_MAKE_PROGRAM; using preflight-selected path.")
                continue
            cmake_args.append(f"-D{define}")
            logger.debug("CMake define: %s", define)

    env = os.environ.copy()
    if gcc_path and gxx_path:
        env["CC"] = gcc_path
        env["CXX"] = gxx_path

    if log_level == "debug":
        cmake_args.append("--log-level=DEBUG")
    elif log_level == "verbose":
        cmake_args.append("--log-level=VERBOSE")
    else:
        cmake_args.append("--log-level=WARNING")

    try:
        # Run cmake/ninja with captured output so normal mode stays quiet (filtered).
        # Run the build from a normal terminal or with sandbox disabled so CMake can
        # delete its temp files; otherwise configure may fail with access denied.
        run_cmd(
            cmake_args,
            logger,
            log_level,
            env=env,
            label="CMake configure",
        )
        logger.debug("CMake configuration successful")

        build_args = ["cmake", "--build", str(build_dir)]
        if log_level == "debug":
            build_args.append("--verbose")

        if log_level == "normal":
            logger.info("Building firmware... (this may take a while)")

        run_cmd(
            build_args,
            logger,
            log_level,
            env=env,
            label="CMake build",
        )
        logger.debug("Firmware build successful")

        uf2_file = find_uf2_file(build_dir)
        if uf2_file:
            logger.info("UF2 file: %s", uf2_file)

        return True
    except subprocess.CalledProcessError:
        logger.error("CMake build failed")
        return False
    except FileNotFoundError:
        logger.error("CMake not found. Please install CMake.")
        return False


def _upsert_cmake_define(defines: List[str], var: str, value: str) -> None:
    """
    Replace any existing definition of `var` and append the new value.

    We accept either:
    - `VAR=VALUE`
    - CMake typed cache form `VAR:TYPE=VALUE`
    """
    prefixes = (f"{var}=", f"{var}:")
    defines[:] = [d for d in defines if not d.startswith(prefixes)]
    defines.append(f"{var}={value}")


def _extract_cmake_define(defines: Sequence[str], var: str) -> Optional[str]:
    """
    Return the last explicit value of `var` from a list of `-D` defines.

    Supports:
    - VAR=VALUE
    - VAR:TYPE=VALUE
    """
    for define in reversed(list(defines)):
        if define.startswith(f"{var}="):
            return define.split("=", 1)[1]
        if define.startswith(f"{var}:"):
            left, sep, right = define.partition("=")
            if sep and left.startswith(f"{var}:"):
                return right
    return None


def _cmake_on_off(value: bool) -> str:
    return "ON" if value else "OFF"


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build RP2350 firmware from Pure Data patch"
    )
    parser.add_argument(
        "pd_file",
        type=str,
        help="Path to the Pure Data patch file (.pd)",
    )
    parser.add_argument(
        "-o",
        "--output",
        type=str,
        default=None,
        help="Output directory for build artifacts (default: build/ in project root)",
    )
    parser.add_argument(
        "-n",
        "--name",
        type=str,
        default=None,
        help="Patch name (default: derived from PD file name)",
    )
    parser.add_argument(
        "--log-level",
        choices=["quiet", "normal", "verbose", "debug"],
        default=DEFAULT_LOG_LEVEL,
        help="Logging level (default: normal)",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_const",
        const="verbose",
        dest="log_level",
        help="Verbose output: shows build progress with file names",
    )
    parser.add_argument(
        "-d",
        "--debug",
        action="store_const",
        const="debug",
        dest="log_level",
        help="Debug output: shows all commands and full details",
    )
    parser.add_argument(
        "-q",
        "--quiet",
        action="store_const",
        const="quiet",
        dest="log_level",
        help="Quiet output: only errors and critical warnings",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        default=False,
        help="Clean build directory before building (default: False)",
    )
    parser.add_argument(
        "--no-clean",
        dest="clean",
        action="store_false",
        help="Don't clean build directory before building (default)",
    )
    parser.add_argument(
        "-D",
        "--define",
        action="append",
        dest="cmake_defines",
        metavar="VAR=VALUE",
        help="Pass CMake defines (e.g., -D ENABLE_WS2812=ON). Can be used multiple times.",
    )

    feature_group = parser.add_argument_group("Feature flags (CMake options)")
    for feature in FEATURE_OPTIONS:
        flag = feature["flag"]
        cmake_var = feature["cmake_var"]
        help_text = feature["help"]
        dest = cmake_var.lower()

        # Single-argument flags: when omitted, defer to CMake defaults.
        feature_group.add_argument(
            flag,
            dest=dest,
            action=feature["action"],
            default=None,
            help=help_text,
        )

    clock_group = parser.add_argument_group("Clock options")
    clock_group.add_argument(
        "--overclocked",
        action="store_true",
        default=False,
        help=(
            "Force RP2350 OC profile (240 MHz) via CMake override "
            "(FW_SYS_CLOCK_PROFILE_OVERRIDE=FW_SYS_CLOCK_PROFILE_OC_240MHZ). "
            "Also sets PICO_FLASH_SPI_CLKDIV=4 unless already provided via -D."
        ),
    )
    return parser.parse_args(argv)


def validate_pd_file(pd_file: Path, logger: logging.Logger) -> None:
    if not pd_file.exists():
        logger.error("PD file not found: %s", pd_file)
        raise SystemExit(1)
    if pd_file.suffix != ".pd":
        logger.error("File must have .pd extension: %s", pd_file)
        raise SystemExit(1)


def main(argv: Optional[Sequence[str]] = None) -> None:
    args = parse_args(argv)

    log_level = getattr(args, "log_level", DEFAULT_LOG_LEVEL)
    logger = configure_logging(log_level)
    if log_level != "quiet":
        logger.info(
            "Log level: %s (quiet|normal|verbose|debug, flags: -q/-v/-d)",
            log_level,
        )

    pd_file = Path(args.pd_file)
    validate_pd_file(pd_file, logger)

    patch_name = args.name or pd_file.stem
    if not patch_name:
        logger.error("Could not determine patch name")
        raise SystemExit(1)

    output_dir = None
    if args.output:
        output_dir = Path(args.output).resolve()
        logger.debug("Using custom output directory: %s", output_dir)

    build_dir = create_build_dir(
        patch_name,
        logger,
        output_dir=output_dir,
    )

    # Clean as early as possible (before attaching file logger / running any build steps).
    if args.clean:
        logger.info("Cleaning patch build directory: %s", build_dir)
        _clean_patch_build_dir(build_dir, logger)

    logger.info("Build directory: %s", build_dir)

    log_file = build_dir / "build_firmware.log"
    attach_file_logger(logger, log_file, log_level)
    logger.info("Full log file: %s", log_file)

    with log_step(logger, f"Compile PD patch ({pd_file.name})"):
        if not compile_patch(pd_file, build_dir, patch_name, logger, log_level):
            raise SystemExit(1)

    heavy_dir = build_dir / "c"
    if not heavy_dir.exists():
        logger.error("Heavy output directory not found: %s", heavy_dir)
        raise SystemExit(1)

    with log_step(logger, "Generate Heavy sources manifest"):
        manifest_path = write_heavy_sources_manifest(heavy_dir, logger)
        if not manifest_path:
            raise SystemExit(1)

    firmware_source_dir = get_project_root() / "core"
    if not firmware_source_dir.exists():
        logger.error("Core project not found: %s", firmware_source_dir)
        raise SystemExit(1)

    cmake_defines = args.cmake_defines or []
    for feature in FEATURE_OPTIONS:
        cmake_var = feature["cmake_var"]
        dest = cmake_var.lower()
        value = getattr(args, dest, None)
        if value is None:
            continue
        _upsert_cmake_define(cmake_defines, cmake_var, _cmake_on_off(bool(value)))

    if args.overclocked:
        _upsert_cmake_define(
            cmake_defines,
            "FW_SYS_CLOCK_PROFILE_OVERRIDE",
            FW_SYS_CLOCK_PROFILE_OC_240MHZ,
        )
        if _extract_cmake_define(cmake_defines, "PICO_FLASH_SPI_CLKDIV") is None:
            _upsert_cmake_define(
                cmake_defines,
                "PICO_FLASH_SPI_CLKDIV",
                DEFAULT_OC_FLASH_SPI_CLKDIV,
            )
            logger.info(
                "--overclocked: forcing FW_SYS_CLOCK_PROFILE override and defaulting PICO_FLASH_SPI_CLKDIV=%s",
                DEFAULT_OC_FLASH_SPI_CLKDIV,
            )
        else:
            logger.info(
                "--overclocked: forcing FW_SYS_CLOCK_PROFILE override (keeping user-provided PICO_FLASH_SPI_CLKDIV)",
            )
    elif _extract_cmake_define(cmake_defines, "FW_SYS_CLOCK_PROFILE_OVERRIDE") is None:
        # Keep default behavior deterministic across re-configures:
        # if --overclocked is not used, explicitly clear override so config.h/local config is respected.
        _upsert_cmake_define(cmake_defines, "FW_SYS_CLOCK_PROFILE_OVERRIDE", "")

    forced_make_program = _extract_cmake_define(cmake_defines, "CMAKE_MAKE_PROGRAM")

    firmware_build_dir = build_dir / "firmware-build"

    # If build dir is locked and --clean is enabled, we already tried cleaning above.
    # As a last resort (still under --clean), fall back to a fresh firmware build dir.
    if args.clean and firmware_build_dir.exists():
        alt_build_dir = build_dir / f"firmware-build-{int(time.time())}"
        logger.warning(
            "firmware-build exists after clean; using fresh build dir: %s",
            alt_build_dir,
        )
        firmware_build_dir = alt_build_dir

    with log_step(logger, "Preflight: toolchains"):
        preflight_ok, ninja_make_program = preflight_check_toolchains(
            logger,
            forced_make_program,
        )
        if not preflight_ok or not ninja_make_program:
            raise SystemExit(1)

    with log_step(logger, "Build firmware"):
        if not build_firmware(
            firmware_source_dir,
            firmware_build_dir,
            manifest_path,
            logger,
            ninja_make_program,
            log_level,
            cmake_defines,
        ):
            logger.error("Firmware build failed")
            raise SystemExit(1)

    logger.info("Build completed successfully!")
    logger.info("Generated files in: %s", build_dir)
    logger.info("C/C++ code in: %s", heavy_dir)
    logger.info("Firmware in: %s", firmware_build_dir)


if __name__ == "__main__":
    main()
