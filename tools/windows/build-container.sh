#!/bin/sh
set -eu

config=${1:-windows-x64-release}
case "$config" in
    windows|windows-x64-release) name=windows-x64-release; type=RelWithDebInfo ;;
    windows-x64) name=windows-x64; type=Debug ;;
    *) echo "Unknown Windows configuration: $config" >&2; exit 2 ;;
esac

cd /src
if [ ! -f tools/kazarov/include/kazarov/port.h ]; then
    git -c safe.directory=/src submodule update --init tools/kazarov
fi

build="/src/port/build/$name"
if [ "${CLEAN:-0}" = 1 ]; then rm -rf "$build"; fi
if [ -f "$build/CMakeCache.txt" ] && ! grep -q '^CMAKE_HOME_DIRECTORY:INTERNAL=/src$' "$build/CMakeCache.txt"; then
    rm -rf "$build"
fi

if [ ! -f "$build/CMakeCache.txt" ] ||
   ! grep -q "^CMAKE_BUILD_TYPE:STRING=$type$" "$build/CMakeCache.txt" ||
   [ /opt/chronicle/toolchain.cmake -nt "$build/CMakeCache.txt" ]; then
    cmake -S /src -B "$build" -G Ninja \
        -DPLATFORM=PC -DCMAKE_BUILD_TYPE="$type" -DDC_BUILD_TESTS=OFF \
        -DCMAKE_TOOLCHAIN_FILE=/opt/chronicle/toolchain.cmake \
        -DPython3_EXECUTABLE=/usr/bin/python3 \
        -DSDL3_DIR=/opt/sdl/SDL3-3.4.18/x86_64-w64-mingw32/lib/cmake/SDL3 \
        -DVulkan_INCLUDE_DIR=/opt/vulkan/Vulkan-Headers-vulkan-sdk-1.4.363.0/include \
        -DVulkan_LIBRARY=/opt/vulkan/libvulkan-1.dll.a \
        -DGLSLANG_VALIDATOR=/opt/glslang/bin/glslangValidator
fi

if [ -n "${JOBS:-}" ]; then
    case "$JOBS" in *[!0-9]*|'') echo "JOBS must be a positive integer" >&2; exit 2 ;; esac
    if [ "$JOBS" -lt 1 ]; then echo "JOBS must be a positive integer" >&2; exit 2; fi
    jobs=$JOBS
else
    jobs=$(nproc)
    if [ "$jobs" -gt 12 ]; then jobs=12; fi
    memory_limit=$(cat /sys/fs/cgroup/memory.max 2>/dev/null || true)
    case "$memory_limit" in
        *[!0-9]*|'') ;;
        *) memory_jobs=$((memory_limit / 1073741824))
           if [ "$memory_jobs" -lt 2 ]; then memory_jobs=2; fi
           if [ "$jobs" -gt "$memory_jobs" ]; then jobs=$memory_jobs; fi ;;
    esac
fi
echo "Building $name with $jobs parallel jobs"

cmake --build "$build" --target darkcloud dcdata -j "$jobs"
copy_if_changed() {
    source=$1
    destination="$build/$(basename "$source")"
    if [ ! -f "$destination" ] || ! cmp -s "$source" "$destination"; then
        cp "$source" "$destination"
    fi
}
copy_if_changed /opt/llvm-mingw/x86_64-w64-mingw32/bin/libc++.dll
copy_if_changed /opt/llvm-mingw/x86_64-w64-mingw32/bin/libunwind.dll
copy_if_changed /opt/llvm-mingw/x86_64-w64-mingw32/bin/libwinpthread-1.dll
copy_if_changed /opt/sdl/SDL3-3.4.18/x86_64-w64-mingw32/bin/SDL3.dll
echo "Built $build"
