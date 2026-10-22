#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
name="$(basename -- "$root")"
out="$root/dist/archives"
mkdir -p "$out"
tar -C "$root/.." --exclude="$name/build" --exclude="$name/dist" --exclude="$name/data" \
  --exclude="$name/.git" --exclude="$name/node_modules" --exclude="$name/.ruff_cache" \
  --exclude='__pycache__' --exclude='*.pyc' \
  -czf "$out/nopiskl_video_source.tar.gz" "$name"
if [[ -x "$root/dist/uclibc/SmartIPC" ]]; then
  tar -C "$root/dist" --exclude='uclibc/data' -czf "$out/SmartIPC_uclibc.tar.gz" uclibc
fi
(cd "$out" && sha256sum ./*.tar.gz > SHA256SUMS)
echo "Archives: $out"
