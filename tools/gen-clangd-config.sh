#!/usr/bin/env bash
# Generate .clangd config for clangd on NixOS.
#
# The CMake build uses GCC, but clangd uses clang internally. GCC's implicit
# system include paths aren't available to clang, so clangd can't find headers
# like stddef.h. This script discovers the correct clang include paths from the
# current Nix environment and writes a .clangd config that adds them.
#
# Run this after entering nix-shell / nix develop, or after updating Nix deps.

set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

# Find clang's resource directory (provides stddef.h, stdint.h, etc.)
resource_dir=$(clang -print-resource-dir) || {
    echo "error: clang not found — run this inside nix-shell" >&2
    exit 1
}

# Find glibc dev headers (provides assert.h, stdio.h, string.h, etc.)
search_paths=$(clang -v -x c -E /dev/null 2>&1)
glibc_include=$(echo "$search_paths" | grep -oP '\S*glibc\S*-dev/include' | head -1)

if [ -z "$glibc_include" ]; then
    echo "error: could not find glibc include path from clang -v output" >&2
    exit 1
fi

cat > .clangd <<EOF
CompileFlags:
  Add:
    - -Wno-unused-command-line-argument
    - -isystem${resource_dir}/include
    - -isystem${glibc_include}
    - -Isrc/mos/include
    - -Isrc/tess/include
    - -Ivendor/libdeflate-1.25
  Remove:
    - -flto=auto
    - -fno-fat-lto-objects
    - -fzero-call-used-regs=*
    - -fstack-clash-protection
    - -frandom-seed=*
EOF

echo "wrote .clangd (resource-dir: ${resource_dir}, glibc: ${glibc_include})"
