# Pack one smoke-mod variant archive from the shared companion-mod tree.
#
# Invoked by ../CMakeLists.txt via `cmake -P` with these variables set:
#   variant     name of the preset (soft-haze, subtle, dark-long)
#   shared_dir  absolute path to smoke-mod/_shared
#   overlay_dir absolute path to smoke-mod/<variant>
#   output      absolute path to the zip archive to produce
#
# The shared tree is never modified. This script copies the full shared
# tree into a staging directory, flattens the base_game/workshop truck
# split back to the SCS-expected flat /vehicle/truck/<truck>/ layout,
# removes any .bak files, applies the variant overlay (which may replace
# any existing file in the shared tree, including meshes), then zips the
# result with the portable `cmake -E tar --format=zip` command.
#
# Overlay rules enforced here and in ../CMakeLists.txt:
#   - An overlay file may only replace a file that exists in the shared
#     tree at the same relative path. Additive paths are rejected.
#   - If an overlay supplies a .pmg, it must also supply the matching
#     .pmd. If the original shared .pmg had a paired .pmc, the overlay
#     must supply that too. Mismatched mesh pairs fail closed.

cmake_policy(SET CMP0011 NEW)

if(NOT variant OR NOT shared_dir OR NOT overlay_dir OR NOT output)
    message(FATAL_ERROR
        "package_variant.cmake requires: variant, shared_dir, overlay_dir, output")
endif()

set(stage "${CMAKE_BINARY_DIR}/stage/${variant}")
file(REMOVE_RECURSE "${stage}")
file(MAKE_DIRECTORY "${stage}")

# Copy the full shared tree verbatim, including directories and metadata.
file(COPY "${shared_dir}/" DESTINATION "${stage}" FOLLOW_SYMLINK_CHAIN)

# Flatten the base_game/workshop truck split back to the SCS-expected
# flat /vehicle/truck/<truck>/ layout. The split is a source-tree
# organization only; the shipped zip must use the game's flat paths.
set(truck_root "${stage}/vehicle/truck")
set(split_dirs "base_game" "workshop")
foreach(split IN LISTS split_dirs)
    set(src "${truck_root}/${split}")
    if(IS_DIRECTORY "${src}")
        file(GLOB truck_dirs LIST_DIRECTORIES true RELATIVE "${src}" "${src}/*")
        foreach(truck IN LISTS truck_dirs)
            if(IS_DIRECTORY "${src}/${truck}")
                file(COPY "${src}/${truck}/" DESTINATION "${truck_root}/${truck}" FOLLOW_SYMLINK_CHAIN)
            endif()
        endforeach()
        file(REMOVE_RECURSE "${src}")
    endif()
endforeach()

# The upgrade/exhaust pool uses the same base_game/workshop split.
set(exhaust_root "${truck_root}/upgrade/exhaust")
foreach(split IN LISTS split_dirs)
    set(src "${exhaust_root}/${split}")
    if(IS_DIRECTORY "${src}")
        file(GLOB truck_dirs LIST_DIRECTORIES true RELATIVE "${src}" "${src}/*")
        foreach(truck IN LISTS truck_dirs)
            if(IS_DIRECTORY "${src}/${truck}")
                file(COPY "${src}/${truck}/" DESTINATION "${exhaust_root}/${truck}" FOLLOW_SYMLINK_CHAIN)
            endif()
        endforeach()
        file(REMOVE_RECURSE "${src}")
    endif()
endforeach()

# Remove leftover .bak files from the staged tree so they do not ship
# in the mod archive.
file(GLOB_RECURSE bak_files "${stage}/*.bak")
foreach(bak IN LISTS bak_files)
    file(REMOVE "${bak}")
endforeach()

# Apply the overlay. The overlay is a recursive tree: every file under
# overlay_dir replaces the file at the same relative path in the staged
# tree. The relative path must already exist (enforced at configure time
# in CMakeLists.txt), so we remove the staged file first then copy.
file(GLOB_RECURSE overlay_files RELATIVE "${overlay_dir}" "${overlay_dir}/*")
foreach(rel IN LISTS overlay_files)
    if(IS_DIRECTORY "${overlay_dir}/${rel}")
        continue()
    endif()
    set(staged_file "${stage}/${rel}")
    if(NOT EXISTS "${staged_file}")
        message(FATAL_ERROR
            "${variant} overlay file '${rel}' has no matching file in the shared tree. "
            "The overlay may only replace existing files.")
    endif()
    file(REMOVE "${staged_file}")
    configure_file("${overlay_dir}/${rel}" "${staged_file}" COPYONLY)
endforeach()

foreach(rel IN LISTS overlay_files)
    if(IS_DIRECTORY "${stage}/${rel}" OR NOT EXISTS "${stage}/${rel}")
        message(FATAL_ERROR
            "${variant} overlay file '${rel}' is not a file at its staged path")
    endif()
endforeach()

file(REMOVE "${output}")

# Enumerate the top-level entries of the staged tree so `cmake -E tar`
# archives all of them. The glob pattern must be anchored inside ${stage};
# a bare "*" globs CMAKE_CURRENT_BINARY_DIR, not the staging directory.
file(GLOB stage_entries LIST_DIRECTORIES true RELATIVE "${stage}" "${stage}/*")
list(SORT stage_entries)

set(tar_args cf "${output}" --format=zip --)
foreach(entry IN LISTS stage_entries)
    list(APPEND tar_args "${entry}")
endforeach()

execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar ${tar_args}
    WORKING_DIRECTORY "${stage}"
    RESULT_VARIABLE tar_result)
if(NOT tar_result EQUAL 0)
    message(FATAL_ERROR "cmake -E tar failed for ${variant} (exit ${tar_result})")
endif()

file(REMOVE_RECURSE "${stage}")
message(STATUS "built ${output}")
