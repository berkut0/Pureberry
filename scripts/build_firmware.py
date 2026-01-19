#!/usr/bin/env python3
"""
Build script for compiling Pure Data patches to RP2350 firmware.

This script:
1. Takes a PD patch file as input
2. Creates a temporary build directory
3. Compiles the patch using hvcc
4. Integrates the generated code with the core firmware project
5. Builds the firmware using CMake/pico-sdk
6. Optionally flashes the firmware
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Tuple, List


def get_project_root():
    """Get the project root directory."""
    script_dir = Path(__file__).parent
    return script_dir.parent


def get_hvcc_search_paths() -> List[str]:
    """Get default hvcc abstraction search paths.

    hvcc can resolve unknown objects as Pd abstractions if their directories are
    provided via -p/--search_paths.

    This repo primarily uses vanilla Pd objects, but plugdata ships a 'heavylib'
    abstraction set (hv.*) that many patches rely on. If present, include it.
    """
    paths: List[Path] = []

    # Allow override via env var (semicolon-separated is convenient on Windows)
    env_paths = os.environ.get("HVCC_SEARCH_PATHS", "").strip()
    if env_paths:
        for p in env_paths.split(";"):
            p = p.strip().strip('"')
            if p:
                paths.append(Path(p))

    # Repo-local heavylib (recommended): avoids hardcoded absolute paths and
    # keeps builds reproducible across machines.
    project_root = get_project_root()
    vendored_heavylib = project_root / "third_party" / "heavylib"
    if vendored_heavylib.exists():
        paths.append(vendored_heavylib)
        # Note: hvcc does NOT recurse into subdirectories for abstractions, so we
        # must include common subfolders explicitly.
        paths.append(vendored_heavylib / "hv.lfo")
        paths.append(vendored_heavylib / "hv.osc")
        paths.append(vendored_heavylib / "hv.filters")

    # Optional local plugdata abstractions path (convenience on Windows).
    # Keep this as a fallback, but prefer the repo-local submodule above.
    plugdata_abstractions = Path(r"C:\Users\Public\Documents\plugdata\Abstractions")
    heavylib_dir = plugdata_abstractions / "heavylib"
    paths.append(heavylib_dir)
    paths.append(heavylib_dir / "hv.lfo")
    paths.append(heavylib_dir / "hv.osc")
    paths.append(heavylib_dir / "hv.filters")

    # Filter to existing directories, keep stable order, de-dupe
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
            # Ignore unreadable paths
            continue

    return existing


def clean_build_directory(build_base_dir: Path, force: bool = False):
    """Clean build directory, handling permission errors gracefully."""
    if not build_base_dir.exists():
        return True
    
    try:
        shutil.rmtree(build_base_dir)
        return True
    except PermissionError as e:
        if force:
            print(f"[WARNING] Could not delete {build_base_dir}: {e}")
            print("  Some files may be locked. Trying to continue...")
            return False
        else:
            print(f"[ERROR] Could not delete {build_base_dir}: {e}")
            print("  Please close any programs using files in this directory")
            return False
    except Exception as e:
        print(f"[WARNING] Error cleaning build directory: {e}")
        return False


def create_build_dir(patch_name: str, output_dir: Path = None) -> Path:
    """Create a build directory for this patch.
    
    Args:
        patch_name: Name of the patch (used as subdirectory name)
        output_dir: Optional output directory. If None, uses project_root/build
    
    Returns:
        Path to the build directory for this patch
    """
    project_root = get_project_root()
    
    if output_dir is None:
        # Default: use build/ directory in project root
        build_base_dir = project_root / "build"
    else:
        build_base_dir = Path(output_dir)
    
    # Clean entire build directory before starting (like rp2040-playground)
    if build_base_dir.exists():
        print(f"Cleaning build directory: {build_base_dir}")
        if not clean_build_directory(build_base_dir, force=True):
            print("[WARNING] Some files could not be deleted, continuing anyway...")
    
    # Create build directory for this specific patch
    build_dir = build_base_dir / patch_name
    build_dir.mkdir(parents=True, exist_ok=True)
    return build_dir


def compile_patch(pd_file: Path, build_dir: Path, patch_name: str) -> bool:
    """Compile PD patch using hvcc.
    
    Note: Always use 'patch' as the Heavy project name for simplicity.
    This way all generated files have consistent names (Heavy_patch.h, hv_patch_new, etc.)
    regardless of the input .pd filename.
    """
    print(f"Compiling PD patch: {pd_file}")
    
    try:
        # Always use 'patch' as project name for consistent generated filenames
        hvcc_cmd = ["hvcc", str(pd_file), "-o", str(build_dir), "-n", "patch"]

        search_paths = get_hvcc_search_paths()
        if search_paths:
            print("hvcc search paths:")
            for p in search_paths:
                print(f"  - {p}")
            hvcc_cmd.extend(["-p", *search_paths])

        result = subprocess.run(
            hvcc_cmd,
            check=True,
            capture_output=True,
            text=True
        )
        print("[OK] hvcc compilation successful")
        return True
    except subprocess.CalledProcessError as e:
        print("[ERROR] hvcc compilation failed")
        if e.stdout:
            print(e.stdout)
        if e.stderr:
            print(e.stderr)
        return False
    except FileNotFoundError:
        print("[ERROR] hvcc not found. Make sure it's installed and in PATH.")
        print("  Install with: pip install -r requirements.txt")
        return False


def copy_core_project(build_dir: Path, heavy_dir: Path) -> bool:
    """Copy core project to build directory and integrate Heavy files."""
    project_root = get_project_root()
    core_dir = project_root / "core"
    firmware_dir = build_dir / "firmware"
    
    if not core_dir.exists():
        print(f"[ERROR] Core project not found: {core_dir}")
        return False
    
    print("Copying core project...")
    try:
        # Remove existing firmware directory if it exists (partial cleanup)
        if firmware_dir.exists():
            try:
                # Try to remove, but don't fail if some files are locked
                shutil.rmtree(firmware_dir, ignore_errors=True)
            except Exception:
                pass  # Ignore errors
        
        # Use copytree with dirs_exist_ok for Python 3.8+
        try:
            shutil.copytree(core_dir, firmware_dir, dirs_exist_ok=True)
        except TypeError:
            # Fallback for Python < 3.8
            if firmware_dir.exists():
                shutil.rmtree(firmware_dir)
            shutil.copytree(core_dir, firmware_dir)
        
        # Copy Heavy files to firmware/heavy directory
        heavy_dest = firmware_dir / "heavy"
        if heavy_dest.exists():
            try:
                shutil.rmtree(heavy_dest, ignore_errors=True)
            except Exception:
                pass  # Ignore errors
        
        try:
            shutil.copytree(heavy_dir, heavy_dest, dirs_exist_ok=True)
        except TypeError:
            # Fallback for Python < 3.8
            if heavy_dest.exists():
                shutil.rmtree(heavy_dest, ignore_errors=True)
            shutil.copytree(heavy_dir, heavy_dest)
        
        print("[OK] Core project and Heavy files copied")
        return True
    except Exception as e:
        print(f"[ERROR] Failed to copy core project: {e}")
        return False


def get_heavy_source_files(heavy_dir: Path) -> Tuple[List[Path], List[Path]]:
    """Get lists of Heavy source files (C and C++)."""
    c_files = []
    cpp_files = []
    
    if not heavy_dir.exists():
        return c_files, cpp_files
    
    for file in heavy_dir.glob("*.c"):
        c_files.append(file)
    for file in heavy_dir.glob("*.cpp"):
        cpp_files.append(file)
    
    return sorted(c_files), sorted(cpp_files)


def update_cmake_lists(firmware_dir: Path, patch_name: str) -> bool:
    """Update CMakeLists.txt to include Heavy-generated files."""
    cmake_file = firmware_dir / "CMakeLists.txt"
    heavy_dir = firmware_dir / "heavy"
    
    if not cmake_file.exists():
        print(f"[ERROR] CMakeLists.txt not found: {cmake_file}")
        return False
    
    # Get Heavy source files
    c_files, cpp_files = get_heavy_source_files(heavy_dir)
    
    if not c_files and not cpp_files:
        print("[ERROR] No Heavy source files found")
        return False
    
    # Read CMakeLists.txt
    with open(cmake_file, 'r', encoding='utf-8') as f:
        content = f.read()
    
    # Generate source file list using GLOB
    # This is simpler and more maintainable
    heavy_glob = """
# Heavy-generated source files (auto-added by build script)
file(GLOB HEAVY_C_FILES "${CMAKE_CURRENT_SOURCE_DIR}/heavy/*.c")
file(GLOB HEAVY_CPP_FILES "${CMAKE_CURRENT_SOURCE_DIR}/heavy/*.cpp")
"""
    
    # Replace the placeholder comment
    import re
    pattern = r'(# Heavy-generated source files.*?)(# )'
    replacement_text = f"{heavy_glob}# "
    
    if re.search(pattern, content, flags=re.DOTALL):
        content = re.sub(pattern, replacement_text, content, flags=re.DOTALL)
    else:
        # If pattern not found, add after target_sources comment, before target_sources call
        insert_marker = "# Core firmware source files"
        if insert_marker in content:
            content = content.replace(
                insert_marker,
                heavy_glob + insert_marker
            )
    
    # Add Heavy files to target_sources (after GLOB definitions)
    heavy_sources_add = """
target_sources(${PROJECT_NAME}.elf PRIVATE
    ${HEAVY_C_FILES}
    ${HEAVY_CPP_FILES}
)"""
    
    # Find target_sources and add Heavy files after the first one
    if "target_sources(${PROJECT_NAME}.elf PRIVATE" in content:
        # Check if Heavy sources already added
        if "${HEAVY_C_FILES}" not in content:
            # Add after existing target_sources
            pattern = r'(target_sources\(\$\{PROJECT_NAME\}\.elf PRIVATE[^)]+\))'
            replacement = r'\1' + heavy_sources_add
            content = re.sub(pattern, replacement, content, flags=re.DOTALL)
    
    # Update include directories
    heavy_include = "    ${CMAKE_CURRENT_SOURCE_DIR}/heavy"
    if heavy_include not in content:
        # Add Heavy include directory
        include_pattern = r'(target_include_directories.*?src\n)'
        replacement_include = f"\\1{heavy_include}\n"
        content = re.sub(include_pattern, replacement_include, content, flags=re.DOTALL)
    
    # Note: We always compile patches with Heavy project name "patch".
    # That means the firmware code can always include Heavy_patch.h and call hv_patch_new/hv_patch_free.
    # No per-patch compile definitions or source-file rewriting is necessary.
    
    # Write updated CMakeLists.txt
    with open(cmake_file, 'w', encoding='utf-8') as f:
        f.write(content)
    
    print("[OK] CMakeLists.txt updated")
    return True


def build_firmware(firmware_dir: Path, verbose: bool = False, cmake_defines: List[str] = None) -> bool:
    """Build firmware using CMake."""
    print("Building firmware with CMake...")
    
    # Create build subdirectory
    cmake_build_dir = firmware_dir / "build"
    cmake_build_dir.mkdir(exist_ok=True)
    
    # Determine PICO_SDK_PATH
    # Priority: 1) Local submodule (preferred for version compatibility), 2) Environment variable
    project_root = get_project_root()
    local_sdk_path = project_root / "sdk" / "pico-sdk"
    pico_sdk_path = os.environ.get("PICO_SDK_PATH")
    
    # Prefer local submodule if available (ensures SDK 2.0+ compatibility with pico-extras)
    if local_sdk_path.exists() and (local_sdk_path / "pico_sdk_init.cmake").exists():
        pico_sdk_path = str(local_sdk_path.resolve())
        print(f"Using local pico-sdk from submodule: {pico_sdk_path}")
    elif pico_sdk_path:
        print(f"Using PICO_SDK_PATH from environment: {pico_sdk_path}")
        print("[WARNING] Consider using local submodule for better version compatibility")
    else:
        print("[WARNING] PICO_SDK_PATH not set and local SDK not found.")
        print(f"  Expected SDK at: {local_sdk_path}")
        print("  Initialize submodule with: git submodule update --init --recursive")
        print("  Or set PICO_SDK_PATH environment variable")
    
    try:
        # Configure CMake with Ninja generator (Python-friendly)
        cmake_args = [
            "cmake",
            "-G", "Ninja",  # Use Ninja generator (can be installed via pip)
            "-S", str(firmware_dir),
            "-B", str(cmake_build_dir),
            "-DPICO_BOARD=waveshare_rp2350_zero"  # RP2350-Zero board
        ]
        
        # Prefer GCC over Clang for host tools compilation (pioasm, picotool)
        # This avoids issues with old Clang versions on Windows
        # Set environment variables so subprojects inherit them
        import shutil
        gcc_path = shutil.which("gcc")
        gxx_path = shutil.which("g++")
        if gcc_path and gxx_path:
            # Set environment variables for subprojects
            os.environ["CC"] = gcc_path
            os.environ["CXX"] = gxx_path
            cmake_args.extend([
                f"-DCMAKE_C_COMPILER={gcc_path}",
                f"-DCMAKE_CXX_COMPILER={gxx_path}"
            ])
            print(f"Using GCC for host tools: {gcc_path}")
        
        # Set PICO_SDK_PATH if we determined it
        if pico_sdk_path:
            cmake_args.append(f"-DPICO_SDK_PATH={pico_sdk_path}")
        
        # Set PICO_EXTRAS_PATH to local submodule
        local_extras_path = project_root / "sdk" / "pico-extras"
        if local_extras_path.exists() and (local_extras_path / "CMakeLists.txt").exists():
            cmake_args.append(f"-DPICO_EXTRAS_PATH={local_extras_path.resolve()}")
        
        # Add any additional CMake defines
        if cmake_defines:
            for define in cmake_defines:
                cmake_args.append(f"-D{define}")
                print(f"CMake define: {define}")
        
        # Prepare environment with compiler settings
        env = os.environ.copy()
        if gcc_path and gxx_path:
            env["CC"] = gcc_path
            env["CXX"] = gxx_path
        
        result = subprocess.run(
            cmake_args,
            check=True,
            capture_output=not verbose,
            text=True,
            env=env
        )
        
        if verbose and result.stdout:
            print(result.stdout)
        
        print("[OK] CMake configuration successful")
        
        # Build
        build_args = ["cmake", "--build", str(cmake_build_dir)]
        if verbose:
            build_args.append("--verbose")
        
        result = subprocess.run(
            build_args,
            check=True,
            capture_output=not verbose,
            text=True
        )
        
        if verbose and result.stdout:
            print(result.stdout)
        
        print("[OK] Firmware build successful")
        
        # Find output files
        uf2_file = cmake_build_dir / f"{firmware_dir.name}.uf2"
        if uf2_file.exists():
            print(f"  UF2 file: {uf2_file}")
        
        return True
        
    except subprocess.CalledProcessError as e:
        print(f"[ERROR] CMake build failed")
        if e.stderr:
            print(e.stderr)
        if e.stdout:
            print(e.stdout)
        return False
    except FileNotFoundError:
        print("[ERROR] CMake not found. Please install CMake.")
        return False


def main():
    parser = argparse.ArgumentParser(
        description="Build RP2350 firmware from Pure Data patch"
    )
    parser.add_argument(
        "pd_file",
        type=str,
        help="Path to the Pure Data patch file (.pd)"
    )
    parser.add_argument(
        "-o", "--output",
        type=str,
        default=None,
        help="Output directory for build artifacts (default: build/ in project root)"
    )
    parser.add_argument(
        "-n", "--name",
        type=str,
        default=None,
        help="Patch name (default: derived from PD file name)"
    )
    parser.add_argument(
        "--flash",
        action="store_true",
        help="Flash firmware after build (requires OpenOCD or picotool)"
    )
    parser.add_argument(
        "-v", "--verbose",
        action="store_true",
        help="Verbose output"
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        default=True,
        help="Clean build directory before building (default: True)"
    )
    parser.add_argument(
        "--no-clean",
        dest="clean",
        action="store_false",
        help="Don't clean build directory before building"
    )
    parser.add_argument(
        "-D", "--define",
        action="append",
        dest="cmake_defines",
        metavar="VAR=VALUE",
        help="Pass CMake defines (e.g., -D ENABLE_WS2812=ON). Can be used multiple times."
    )
    
    args = parser.parse_args()
    
    # Validate PD file
    pd_file = Path(args.pd_file)
    if not pd_file.exists():
        print(f"[ERROR] PD file not found: {pd_file}")
        sys.exit(1)
    
    if not pd_file.suffix == ".pd":
        print(f"[ERROR] File must have .pd extension: {pd_file}")
        sys.exit(1)
    
    # Determine patch name
    patch_name = args.name or pd_file.stem
    if not patch_name:
        print("[ERROR] Could not determine patch name")
        sys.exit(1)
    
    # Determine output directory
    output_dir = None
    if args.output:
        output_dir = Path(args.output).resolve()
        print(f"Using custom output directory: {output_dir}")
    
    # Create build directory (with optional cleaning)
    if args.clean:
        build_dir = create_build_dir(patch_name, output_dir)
    else:
        project_root = get_project_root()
        if output_dir is None:
            build_base_dir = project_root / "build"
        else:
            build_base_dir = Path(output_dir)
        build_dir = build_base_dir / patch_name
        build_dir.mkdir(parents=True, exist_ok=True)
    
    print(f"Build directory: {build_dir}")
    
    # Compile PD patch with hvcc
    if not compile_patch(pd_file, build_dir, patch_name):
        sys.exit(1)
    
    heavy_dir = build_dir / "c"
    if not heavy_dir.exists():
        print(f"[ERROR] Heavy output directory not found: {heavy_dir}")
        sys.exit(1)
    
    # Copy core project and Heavy files
    if not copy_core_project(build_dir, heavy_dir):
        sys.exit(1)
    
    firmware_dir = build_dir / "firmware"
    
    # Update CMakeLists.txt (adds Heavy sources)
    if not update_cmake_lists(firmware_dir, patch_name):
        sys.exit(1)
    
    # Build firmware
    cmake_defines = args.cmake_defines or []
    if not build_firmware(firmware_dir, args.verbose, cmake_defines):
        print("[ERROR] Firmware build failed")
        sys.exit(1)
    
    print(f"\n[OK] Build completed successfully!")
    print(f"  Generated files in: {build_dir}")
    print(f"  C/C++ code in: {heavy_dir}")
    print(f"  Firmware in: {firmware_dir / 'build'}")


if __name__ == "__main__":
    main()
