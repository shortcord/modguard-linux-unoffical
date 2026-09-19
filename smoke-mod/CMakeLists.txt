cmake_minimum_required(VERSION 3.16)

project(smoke_mod_variants VERSION 1.0.0 LANGUAGES NONE)

# Clean replacement particle definitions, sprite textures, and optional
# mesh files, packaged as playable SCS mod archives by overlaying each
# preset onto the shared companion-mod tree in smoke-mod/_shared/.
#
# The shared tree is reorganized by truck source: _shared/vehicle/truck/
# has base_game/ and workshop/ subdirectories. The packaging script
# flattens these back to the SCS-expected /vehicle/truck/<truck>/ layout
# in the shipped archive. The split is a source-tree organization only.
#
# This is a self-contained CMake project. Run it independently of the
# modguard-linux plugin build:
#
#   cmake -S smoke-mod -B smoke-mod/build-cmake
#   cmake --build smoke-mod/build-cmake --target smoke_mod_variants
#
# The default target builds nothing. Use `smoke_mod_variants` to package
# all presets, or `smoke_mod_<variant>` for a single preset.

set(SMOKE_MOD_VARIANTS soft-haze subtle dark-long)

set(shared_dir "${CMAKE_CURRENT_SOURCE_DIR}/_shared")

# Configure-time preflight: the shared tree must contain the directories
# the overlay replaces files inside.
if(NOT IS_DIRECTORY "${shared_dir}/unit/hookup"
        OR NOT IS_DIRECTORY "${shared_dir}/model/particle")
    message(FATAL_ERROR
        "shared tree '${shared_dir}' is incomplete: "
        "expected unit/hookup and model/particle")
endif()

# Helper: enumerate the overlay files for a variant as a list of
# paths relative to the variant directory. Excludes directories.
# Helper: enumerate overlay files for a variant. Returns absolute paths
# in out_abs (for DEPENDS) and relative paths in out_rel (for checks).
function(enumerate_overlay_files overlay_dir out_abs out_rel)
    file(GLOB_RECURSE entries RELATIVE "${overlay_dir}" "${overlay_dir}/*")
    set(abs_result)
    set(rel_result)
    foreach(entry IN LISTS entries)
        if(NOT IS_DIRECTORY "${overlay_dir}/${entry}")
            list(APPEND abs_result "${overlay_dir}/${entry}")
            list(APPEND rel_result "${entry}")
        endif()
    endforeach()
    set(${out_abs} "${abs_result}" PARENT_SCOPE)
    set(${out_rel} "${rel_result}" PARENT_SCOPE)
endfunction()

# Helper: check that every overlay file has a matching file in the shared
# tree at the same relative path. The overlay may only replace existing
# files; it may not add new paths.
function(check_overlay_paths variant overlay_dir)
    enumerate_overlay_files("${overlay_dir}" _ overlay_files)
    foreach(rel IN LISTS overlay_files)
        if(NOT EXISTS "${shared_dir}/${rel}")
            message(FATAL_ERROR
                "${variant} overlay file '${rel}' has no matching file in the shared tree. "
                "The overlay may only replace existing files.")
        endif()
    endforeach()
endfunction()

# Helper: enforce the mesh-pair rule. If an overlay supplies a .pmg, it
# must also supply the matching .pmd. If the original shared .pmg had a
# paired .pmc, the overlay must supply that too. Mismatched mesh pairs
# would crash the game, so fail closed.
function(check_mesh_pairs variant overlay_dir)
    enumerate_overlay_files("${overlay_dir}" _ overlay_files)
    foreach(rel IN LISTS overlay_files)
        get_filename_component(ext "${rel}" EXT)
        if(NOT ext STREQUAL ".pmg")
            continue()
        endif()
        get_filename_component(stem "${rel}" NAME_WE)
        get_filename_component(dir "${rel}" DIRECTORY)
        set(pmd_rel "${dir}/${stem}.pmd")
        if(NOT EXISTS "${overlay_dir}/${pmd_rel}")
            message(FATAL_ERROR
                "${variant} overlay supplies '${rel}' but not its paired '${pmd_rel}'. "
                "A .pmg overlay must also supply the matching .pmd.")
        endif()
        set(pmc_rel "${dir}/${stem}.pmc")
        if(EXISTS "${shared_dir}/${pmc_rel}" AND NOT EXISTS "${overlay_dir}/${pmc_rel}")
            message(FATAL_ERROR
                "${variant} overlay supplies '${rel}' but the shared tree has a paired "
                "'${pmc_rel}' that the overlay does not replace. "
                "A .pmg overlay must also supply the matching .pmc when one exists.")
        endif()
    endforeach()
endfunction()

set(all_variant_targets)
set(all_variant_outputs)

foreach(variant IN LISTS SMOKE_MOD_VARIANTS)
    set(overlay_dir "${CMAKE_CURRENT_SOURCE_DIR}/${variant}")

    if(NOT IS_DIRECTORY "${overlay_dir}")
        message(FATAL_ERROR
            "${variant} overlay directory '${overlay_dir}' does not exist")
    endif()

    check_overlay_paths("${variant}" "${overlay_dir}")
    check_mesh_pairs("${variant}" "${overlay_dir}")

    # Enumerate overlay files for the DEPENDS list so edits trigger a
    # rebuild. Directories are excluded; only files are dependencies.
    enumerate_overlay_files("${overlay_dir}" overlay_deps _)

    set(output "${CMAKE_BINARY_DIR}/PM_389_Smoke_${variant}.zip")
    set(target "smoke_mod_${variant}")

    add_custom_command(
        OUTPUT "${output}"
        COMMAND ${CMAKE_COMMAND}
                -Dvariant=${variant}
                -Dshared_dir=${shared_dir}
                -Doverlay_dir=${overlay_dir}
                -Doutput=${output}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/package_variant.cmake
        DEPENDS ${overlay_deps}
                "${CMAKE_CURRENT_SOURCE_DIR}/cmake/package_variant.cmake"
                "${CMAKE_CURRENT_SOURCE_DIR}/truck_sources.toml"
        COMMENT "Packaging smoke-mod variant ${variant}"
        VERBATIM)

    add_custom_target(${target} DEPENDS "${output}")
    list(APPEND all_variant_targets ${target})
    list(APPEND all_variant_outputs "${output}")
endforeach()

add_custom_target(smoke_mod_variants
    DEPENDS ${all_variant_targets})