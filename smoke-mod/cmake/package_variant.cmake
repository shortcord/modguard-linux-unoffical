# Pack one smoke-mod variant archive from the shared companion-mod tree.
#
# Invoked by ../CMakeLists.txt via `cmake -P` with these variables set:
#   variant     name of the preset (soft-haze, subtle)
#   shared_dir  absolute path to smoke-mod/_shared
#   overlay_dir absolute path to smoke-mod/<variant>
#   output      absolute path to the zip archive to produce
#
# The shared tree is never modified. This script copies the full shared
# tree into a staging directory, replaces the particle definition and the
# smoke sprite with the variant overlay, then zips the result with the
# portable `cmake -E tar --format=zip` command.

cmake_policy(SET CMP0011 NEW)

if(NOT variant OR NOT shared_dir OR NOT overlay_dir OR NOT output)
    message(FATAL_ERROR
        "package_variant.cmake requires: variant, shared_dir, overlay_dir, output")
endif()

set(smoke_sii "unit/hookup/smoke_new.sii")
set(pipe_dds "model/particle/pipe.dds")

set(stage "${CMAKE_BINARY_DIR}/stage/${variant}")
file(REMOVE_RECURSE "${stage}")
file(MAKE_DIRECTORY "${stage}")

# Copy the full shared tree verbatim, including directories and metadata.
file(COPY "${shared_dir}/" DESTINATION "${stage}" FOLLOW_SYMLINK_CHAIN)

# Remove the two files the overlay replaces. Keep pipe.tobj: it references
# /model/particle/pipe.dds, the path we overwrite.
file(REMOVE "${stage}/${smoke_sii}" "${stage}/${pipe_dds}")

# Apply the overlay.
file(COPY "${overlay_dir}/unit/" DESTINATION "${stage}/unit/" FOLLOW_SYMLINK_CHAIN)
file(COPY "${overlay_dir}/${pipe_dds}" DESTINATION "${stage}/model/particle" FOLLOW_SYMLINK_CHAIN)

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