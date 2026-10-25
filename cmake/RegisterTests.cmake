# CTest runs already-built executables. It never builds or imports private media.
set(SCR_TEST_ASSET_PACK "${PROJECT_SOURCE_DIR}/analysis/assets/stunt-car-racer.scr-assets"
  CACHE FILEPATH "Local asset pack for native integration tests")
set(SCR_TEST_ADF "${PROJECT_SOURCE_DIR}/Stunt Car Racer (MicroStyle).adf"
  CACHE FILEPATH "Optional local ADF for launcher import tests")
set(SCR_TEST_PAYLOAD "${PROJECT_SOURCE_DIR}/analysis/reference/vesuri/offline-extracted-payload.bin"
  CACHE FILEPATH "Local original payload for test-only comparisons")
set(test_output "${PROJECT_BINARY_DIR}/test-output/$<CONFIG>")

function(scr_test target label input)
  add_test(NAME ${target} COMMAND "${CMAKE_COMMAND}"
    "-DTEST_EXECUTABLE=$<TARGET_FILE:${target}>"
    "-DTEST_INPUT=${input}"
    "-DTEST_ARGUMENTS=${ARGN}"
    "-DTEST_OUTPUT=${test_output}/${target}"
    "-DTEST_BUILD_ROOT=${PROJECT_BINARY_DIR}"
    "-DTEST_ADF=${SCR_TEST_ADF}"
    "-DTEST_PAYLOAD=${SCR_TEST_PAYLOAD}"
    -P "${PROJECT_SOURCE_DIR}/cmake/RunNativeTest.cmake")
  set_tests_properties(${target} PROPERTIES
    LABELS "${label}" TIMEOUT 600 SKIP_REGULAR_EXPRESSION "SKIP: missing local test input")
endfunction()

foreach(target paula_mixer_tests replay_interpolation_tests live_drive_tests)
  scr_test(${target} "native;unit" "")
endforeach()
scr_test(software_renderer_tests "native;assets" "${SCR_TEST_PAYLOAD}" "${SCR_TEST_PAYLOAD}")

if(WIN32)
  scr_test(native_effects_interpolation_tests "native;unit" "")
  scr_test(native_save_store_tests "native;unit" "" "${test_output}/native_save_store_tests")
  foreach(target native_game_session_tests native_opening_session_tests native_startup_tests
      draw_bridge_driving_tests native_opponent_driving_tests native_opponent_session_tests
      native_multiplayer_session_tests native_career_session_tests native_save_session_tests
      native_display_tests native_launcher_tests native_navigation_tests native_autosave_tests native_extended_view_tests native_lap_timer_tests)
    scr_test(${target} "native;assets" "${SCR_TEST_ASSET_PACK}"
      "${SCR_TEST_ASSET_PACK}" "${test_output}/${target}")
  endforeach()
  # These suites create native windows. Serialize their use of the desktop.
  set_tests_properties(native_display_tests native_launcher_tests PROPERTIES
    RESOURCE_LOCK windows_ui LABELS "native;assets;ui")
  # Match the existing wrappers' host checks: atomic replacement/read-only-file
  # exercises and native windows run without competing test processes.
  set_tests_properties(native_save_store_tests native_save_session_tests
    native_display_tests native_launcher_tests PROPERTIES RUN_SERIAL TRUE)
endif()

if(SCR_BUILD_ORACLE)
  foreach(target track_initialization_oracle draw_bridge_oracle
      native_menu_render_tests native_menu_flow_tests native_palette_fade_tests)
    scr_test(${target} "oracle;assets" "${SCR_TEST_PAYLOAD}" "${SCR_TEST_PAYLOAD}")
  endforeach()
  if(WIN32)
    foreach(stage native_opening_flow native_player_name native_opening_render
        native_hall_of_fame native_hall_records native_save_state native_save_menu
        native_multiplayer_state native_multiplayer_render native_opponent_race
        native_competition_render native_career_state)
      scr_test(${stage}_tests "oracle;assets" "${SCR_TEST_PAYLOAD}" "${SCR_TEST_PAYLOAD}")
    endforeach()
    foreach(target native_track_preview_tests replay_scene_commands_tests)
      scr_test(${target} "oracle;assets" "${SCR_TEST_ASSET_PACK}" "${SCR_TEST_ASSET_PACK}")
    endforeach()
    scr_test(native_render_audit "oracle;assets" "${SCR_TEST_ASSET_PACK}"
      "${SCR_TEST_ASSET_PACK}" "${test_output}/native_render_audit")
  endif()
endif()

# Python is a verification tool, never a dependency of the player executable.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME repository_asset_policy COMMAND "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/scripts/check_repository_assets.py" --worktree)
  foreach(suite asset_policy driving_replay_wrapper replay_lap)
    add_test(NAME ${suite}_tests COMMAND "${Python3_EXECUTABLE}"
      -m unittest discover -s tests/unit -p test_${suite}.py)
    set_tests_properties(${suite}_tests PROPERTIES
      ENVIRONMENT "SCR_TEST_OUTPUT_ROOT=${test_output}/python")
  endforeach()
  set_tests_properties(repository_asset_policy asset_policy_tests driving_replay_wrapper_tests replay_lap_tests PROPERTIES
    LABELS "source" WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" TIMEOUT 120)
else()
  message(STATUS "Python3 not found: source-policy CTests are unavailable")
endif()
