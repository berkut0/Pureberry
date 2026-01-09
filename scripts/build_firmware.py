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


def create_build_dir(patch_name: str) -> Path:
    """Create a temporary build directory for this patch."""
    project_root = get_project_root()
    build_dir = project_root / "build" / patch_name
    
    # Clean build directory if it exists
    if build_dir.exists():
        shutil.rmtree(build_dir)
    
    build_dir.mkdir(parents=True, exist_ok=True)
    return build_dir


def compile_patch(pd_file: Path, build_dir: Path, patch_name: str) -> bool:
    """Compile PD patch using hvcc."""
    print(f"Compiling PD patch: {pd_file}")
    
    try:
        result = subprocess.run(
            ["hvcc", str(pd_file), "-o", str(build_dir), "-n", patch_name],
            check=True,
            capture_output=True,
            text=True
        )
        print("[OK] hvcc compilation successful")
        return True
    except subprocess.CalledProcessError as e:
        print(f"[ERROR] hvcc compilation failed: {e.stderr}")
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
        shutil.copytree(core_dir, firmware_dir)
        
        # Copy Heavy files to firmware/heavy directory
        heavy_dest = firmware_dir / "heavy"
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
    
    # Write updated CMakeLists.txt
    with open(cmake_file, 'w', encoding='utf-8') as f:
        f.write(content)
    
    print("[OK] CMakeLists.txt updated")
    return True


def update_main_c(firmware_dir: Path, patch_name: str) -> bool:
    """Update main.c to include Heavy headers and initialize context."""
    main_file = firmware_dir / "src" / "main.c"
    
    if not main_file.exists():
        print(f"[ERROR] main.c not found: {main_file}")
        return False
    
    # Read main.c
    with open(main_file, 'r', encoding='utf-8') as f:
        content = f.read()
    
    # Uncomment Heavy includes
    content = content.replace(
        "// #include \"Heavy_heavy.h\"",
        f"#include \"heavy/Heavy_{patch_name}.h\""
    )
    content = content.replace(
        "// #include \"HvHeavy.h\"",
        "#include \"heavy/HvHeavy.h\""
    )
    
    # Uncomment Heavy context initialization
    content = content.replace(
        "// static HeavyContextInterface *heavy_context = NULL;",
        f"static HeavyContextInterface *heavy_context = NULL;"
    )
    
    # Uncomment context creation
    content = content.replace(
        "    // heavy_context = hv_heavy_new(SAMPLE_RATE);",
        f"    heavy_context = hv_{patch_name}_new(SAMPLE_RATE);"
    )
    content = content.replace(
        "    // if (heavy_context == NULL) {",
        "    if (heavy_context == NULL) {"
    )
    content = content.replace(
        "    //     printf(\"ERROR: Failed to create Heavy context\\n\");",
        "        printf(\"ERROR: Failed to create Heavy context\\n\");"
    )
    content = content.replace(
        "    //     return -1;",
        "        return -1;"
    )
    content = content.replace(
        "    // }",
        "    }"
    )
    content = content.replace(
        "    // printf(\"Heavy context created (sample rate: %.1f Hz)\\n\", SAMPLE_RATE);",
        "    printf(\"Heavy context created (sample rate: %.1f Hz)\\n\", SAMPLE_RATE);"
    )
    
    # Uncomment process call (will need proper implementation later)
    # content = content.replace(
    #     "        // hv_process(heavy_context, audio_in_buffer, audio_out_buffer, AUDIO_BLOCK_SIZE);",
    #     "        hv_process(heavy_context, audio_in_buffer, audio_out_buffer, AUDIO_BLOCK_SIZE);"
    # )
    
    # Uncomment cleanup
    content = content.replace(
        "    // if (heavy_context != NULL) {",
        "    if (heavy_context != NULL) {"
    )
    content = content.replace(
        "    //     hv_heavy_free(heavy_context);",
        f"        hv_{patch_name}_free(heavy_context);"
    )
    content = content.replace(
        "    // }",
        "    }"
    )
    
    # Write updated main.c
    with open(main_file, 'w', encoding='utf-8') as f:
        f.write(content)
    
    print("[OK] main.c updated")
    return True


def build_firmware(firmware_dir: Path, verbose: bool = False) -> bool:
    """Build firmware using CMake."""
    print("Building firmware with CMake...")
    
    # Create build subdirectory
    cmake_build_dir = firmware_dir / "build"
    cmake_build_dir.mkdir(exist_ok=True)
    
    # Check for PICO_SDK_PATH
    pico_sdk_path = os.environ.get("PICO_SDK_PATH")
    if not pico_sdk_path:
        print("[WARNING] PICO_SDK_PATH not set. CMake may fail.")
        print("  Set it with: set PICO_SDK_PATH=<path_to_pico_sdk>")
    
    try:
        # Configure CMake
        cmake_args = [
            "cmake",
            "-S", str(firmware_dir),
            "-B", str(cmake_build_dir),
            "-DPICO_BOARD=pico"
        ]
        
        result = subprocess.run(
            cmake_args,
            check=True,
            capture_output=not verbose,
            text=True
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
        help="Output directory (default: build/<patch_name>)"
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
    
    # Create build directory
    build_dir = create_build_dir(patch_name)
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
    
    # Update CMakeLists.txt
    if not update_cmake_lists(firmware_dir, patch_name):
        sys.exit(1)
    
    # Update main.c
    if not update_main_c(firmware_dir, patch_name):
        sys.exit(1)
    
    # Build firmware
    if not build_firmware(firmware_dir, args.verbose):
        print("[ERROR] Firmware build failed")
        sys.exit(1)
    
    print(f"\n[OK] Build completed successfully!")
    print(f"  Generated files in: {build_dir}")
    print(f"  C/C++ code in: {heavy_dir}")
    print(f"  Firmware in: {firmware_dir / 'build'}")


if __name__ == "__main__":
    main()
