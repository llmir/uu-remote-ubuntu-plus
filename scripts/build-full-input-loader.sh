#!/usr/bin/env bash
set -Eeuo pipefail

# The approved MinGW FreeRDP build prefixes dynamic addins with "lib".
# Forward to the canonical plugin so its strict module identity stays intact.
unset CFLAGS CPPFLAGS LDFLAGS CPATH C_INCLUDE_PATH LIBRARY_PATH CXXFLAGS \
    OBJC_INCLUDE_PATH COMPILER_PATH GCC_EXEC_PREFIX ASFLAGS
export SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-1}"
repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
output_dir="${1:-$repo_dir/build/compat}"
cc="${MINGW_CC:-x86_64-w64-mingw32-gcc}"
strip="${MINGW_STRIP:-x86_64-w64-mingw32-strip}"
mkdir -p "$output_dir"
"$cc" -shared -nostdlib -Wl,--no-insert-timestamp -Wl,--entry,0 \
    -o "$output_dir/libuurb-full-input-client.dll" \
    "$repo_dir/src/uurb_full_input_loader.def"
"$strip" "$output_dir/libuurb-full-input-client.dll"
