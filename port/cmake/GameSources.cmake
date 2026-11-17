find_package(Git REQUIRED)

set(patch_root "${CMAKE_CURRENT_SOURCE_DIR}/../games/gangstar-wch-patches")
set(staging "${CMAKE_CURRENT_BINARY_DIR}/game-source-patches")
set(staged_game "${staging}/src/game")
set(WIN32_GAME_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/win32-game")
set(game_sources ai/StateMachine.cpp ai/StateMachine.h input/TouchScreenBase.cpp input/TouchScreenBase.h)

# Apply the patch to a copy. The Android matching build uses the game checkout.
foreach(file IN LISTS game_sources)
    configure_file("${WCH_SOURCE}/${file}" "${staged_game}/${file}" COPYONLY)
endforeach()

# Git must operate on relative paths under the staging directory even when
# the CMake binary directory is inside the OpenGangstar Git worktree.
set(patch_command "${CMAKE_COMMAND}" -E env --unset=GIT_DIR --unset=GIT_WORK_TREE
    "GIT_CEILING_DIRECTORIES=${CMAKE_CURRENT_BINARY_DIR}" "${GIT_EXECUTABLE}" apply)
foreach(name IN ITEMS 0002-win32-state-machine.patch 0003-win32-touch.patch)
    set(patch "${patch_root}/${name}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${patch}")
    execute_process(
        COMMAND ${patch_command} --reverse --check "${patch}"
        WORKING_DIRECTORY "${staging}"
        RESULT_VARIABLE already_patched
        OUTPUT_QUIET ERROR_QUIET
    )
    if(NOT already_patched EQUAL 0)
        execute_process(
            COMMAND ${patch_command} "${patch}"
            WORKING_DIRECTORY "${staging}"
            RESULT_VARIABLE patch_result
            ERROR_VARIABLE patch_error
        )
        if(NOT patch_result EQUAL 0)
            message(FATAL_ERROR "Could not apply ${name} to the Win32 build sources.\n${patch_error}")
        endif()
    endif()
endforeach()

# Only update the actual compiler inputs when their contents change.
file(MAKE_DIRECTORY "${WIN32_GAME_SOURCE}/ai" "${WIN32_GAME_SOURCE}/input")
foreach(file IN LISTS game_sources)
    file(COPY_FILE "${staged_game}/${file}" "${WIN32_GAME_SOURCE}/${file}" ONLY_IF_DIFFERENT)
endforeach()
foreach(file IN ITEMS core/FrameInterfaces.h core/StateBase.h core/Point2D.h core/Rct.h core/Rect.h)
    configure_file("${WCH_SOURCE}/${file}" "${WIN32_GAME_SOURCE}/${file}" COPYONLY)
endforeach()
