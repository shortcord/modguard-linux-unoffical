#!/usr/bin/env bash
# Build the smoke-mod variants from the shared companion-mod tree.
#
# Inputs:
#   <repo>/smoke-mod/_shared/  shared companion-mod tree (metadata, truck
#                              hookups, automat materials, original particle
#                              definition and sprite)
#   <repo>/smoke-mod/<variant> overlay sources (soft-haze, subtle)
#
# The shared tree is never modified. Each variant copies the full shared tree,
# then replaces the particle definition and the smoke sprite with the variant
# overlay, and repackages the result as a new archive under smoke-mod/build/.
#
# Outputs:
#   smoke-mod/build/PM_389_Smoke_soft-haze.zip
#   smoke-mod/build/PM_389_Smoke_subtle.zip

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
shared_dir="$repo_root/smoke-mod/_shared"
mod_dir="$repo_root/smoke-mod"
build_dir="$repo_root/smoke-mod/build"

variants=(soft-haze subtle)

if [[ ! -d "$shared_dir/unit/hookup" || ! -d "$shared_dir/model/particle" ]]; then
    echo "error: shared tree '$shared_dir' is incomplete" >&2
    exit 1
fi

mkdir -p "$build_dir"

for variant in "${variants[@]}"; do
    overlay="$mod_dir/$variant"
    for required in \
        "unit/hookup/smoke_new.sii" \
        "model/particle/pipe.dds"; do
        if [[ ! -f "$overlay/$required" ]]; then
            echo "error: $variant overlay missing $required" >&2
            exit 1
        fi
    done

    stage="$(mktemp -d)"
    trap 'rm -rf "$stage"' EXIT

    cp -a "$shared_dir/." "$stage/"

    # Replace the particle definition and the smoke sprite. Keep the original
    # pipe.tobj: it references /model/particle/pipe.dds, the path we overwrite.
    rm -f "$stage/unit/hookup/smoke_new.sii" \
          "$stage/model/particle/pipe.dds"

    cp -a "$overlay/unit/." "$stage/unit/"
    cp -a "$overlay/model/particle/pipe.dds" "$stage/model/particle/pipe.dds"

    archive="$build_dir/PM_389_Smoke_$variant.zip"
    rm -f "$archive"
    (cd "$stage" && zip -q -r "$archive" .)
    trap - EXIT
    rm -rf "$stage"

    echo "built $archive"
done