#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
target=SmartIPC
libc=uclibc
jobs=2
sim=OFF
while (($#)); do
  case "$1" in
    --target|--libc|--jobs)
      [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 2; }
      case "$1" in --target) target="$2";; --libc) libc="$2";; --jobs) jobs="$2";; esac
      shift 2;;
    --host) sim=ON; shift;;
    --help) echo "Usage: ./build.sh [--target SmartIPC|nopiskl_rtsp_opencv|nopiskl_rtsp_opencv_capture|all] [--libc uclibc|glibc] [--host] [--jobs 2]"; exit 0;;
    *) echo "Unknown option: $1" >&2; exit 2;;
  esac
done
case "$target" in SmartIPC|nopiskl_rtsp_opencv|nopiskl_rtsp_opencv_capture|all) ;; *) echo "Unknown target" >&2; exit 2;; esac
case "$libc" in uclibc|glibc) ;; *) echo "Unknown libc" >&2; exit 2;; esac
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { echo "Invalid jobs" >&2; exit 2; }
profile="$libc"
args=()
if [[ "$sim" == ON ]]; then
  profile=host
  [[ "$target" == SmartIPC || "$target" == all ]] || { echo "Host supports SmartIPC only" >&2; exit 2; }
else
  args+=("-DCMAKE_TOOLCHAIN_FILE=$root/cmake/toolchains/$libc.cmake")
fi
build_dir="$root/build/$profile"
package="$root/dist/$profile"
cmake -S "$root" -B "$build_dir" "${args[@]}" -DNOPISKL_LIBC="$libc" -DNOPISKL_HOST_SIMULATOR="$sim" -DCMAKE_INSTALL_PREFIX="$package"
if [[ "$target" == all ]]; then
  cmake --build "$build_dir" --parallel "$jobs"
else
  cmake --build "$build_dir" --target "$target" --parallel "$jobs"
fi
# 只安装实际构建过的二进制，允许单目标部署。
cmake --install "$build_dir" --strip
echo "Package: $package"
