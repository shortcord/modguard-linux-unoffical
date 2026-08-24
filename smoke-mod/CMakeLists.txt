cmake_minimum_required(VERSION 3.16)

project(smoke_mod_variants VERSION 1.0.0 LANGUAGES NONE)

# Clean replacement particle definitions and sprite texture, packaged as
# playable SCS mod archives by overlaying each preset onto the verbatim
# extracted companion-mod tree in smoke-mod/_shared/.
#
# This is a self-contained CMake project. Run it independently of the
# modguard-linux plugin build:
#
#   cmake -S smoke-mod -B smoke-mod/build-cmake
#   cmake --build smoke-mod/build-cmake --target smoke_mod_variants
#
# The default target builds nothing. Use `smoke_mod_variants` to package
# all presets, or `smoke_mod_<variant>` for a single preset.

set(SMOKE_MOD_VARIANTS soft-haze subtle)

set(shared_dir "${CMAKE_CURRENT_SOURCE_DIR}/_shared")

# Configure-time preflight: the shared tree must contain the directories
# the overlay replaces files inside.
if(NOT IS_DIRECTORY "${shared_dir}/unit/hookup"
        OR NOT IS_DIRECTORY "${shared_dir}/model/particle")
    message(FATAL_ERROR
        "shared tree '${shared_dir}' is incomplete: "
        "expected unit/hookup and model/particle")
endif()

set(overlay_files
    "unit/hookup/smoke_new.sii"
    "model/particle/pipe.dds")

set(all_variant_targets)
set(all_variant_outputs)

foreach(variant IN LISTS SMOKE_MOD_VARIANTS)
    set(overlay_dir "${CMAKE_CURRENT_SOURCE_DIR}/${variant}")

    foreach(required IN LISTS overlay_files)
        if(NOT EXISTS "${overlay_dir}/${required}")
            message(FATAL_ERROR
                "${variant} overlay missing ${required}")
        endif()
    endforeach()

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
        DEPENDS "${overlay_dir}/unit/hookup/smoke_new.sii"
                "${overlay_dir}/model/particle/pipe.dds"
                "${CMAKE_CURRENT_SOURCE_DIR}/cmake/package_variant.cmake"
        COMMENT "Packaging smoke-mod variant ${variant}"
        VERBATIM)

    add_custom_target(${target} DEPENDS "${output}")
    list(APPEND all_variant_targets ${target})
    list(APPEND all_variant_outputs "${output}")
endforeach()

add_custom_target(smoke_mod_variants
    DEPENDS ${all_variant_targets})