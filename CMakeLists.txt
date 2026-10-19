cmake_minimum_required(VERSION 3.20)
project(stunt_car_racer_recovery LANGUAGES C CXX)

add_library(scr_physics STATIC src/wheel_coordinates.cpp src/transform_matrices.cpp
  src/expected_wheel_heights.cpp src/track_interpolation.cpp src/suspension_springs.cpp
  src/wheel_contact.cpp src/suspension_response.cpp src/track_sampling.cpp
  src/road_suspension_step.cpp src/motion_transforms.cpp src/wheel_motion.cpp
  src/motion_integration.cpp src/physics_prefix.cpp src/motion_step.cpp
  src/steering_response.cpp src/car_dynamics.cpp src/suspension_effects.cpp
  src/audio_playback.cpp src/physics_update.cpp src/player_controls.cpp
  src/track_progress.cpp src/opponent_collision.cpp src/driving_effects.cpp src/race_timing.cpp
  src/race_transitions.cpp src/race_restart.cpp)
target_include_directories(scr_physics PUBLIC src)
target_compile_features(scr_physics PUBLIC cxx_std_17)
if(MSVC)
  target_compile_options(scr_physics PRIVATE /W4 /WX /permissive-)
else()
  target_compile_options(scr_physics PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

# Musashi belongs exclusively to the verification executable. scr_physics has
# no emulator dependency and remains a normal native C++ library.
add_executable(driving_replay_native tests/driving_replay_native.cpp)
target_link_libraries(driving_replay_native PRIVATE scr_physics)
target_compile_features(driving_replay_native PRIVATE cxx_std_17)
option(SCR_BUILD_ORACLE "Build differential tests against original 68000 bytes" OFF)
set(MUSASHI_DIR "" CACHE PATH "Pinned, locally downloaded Musashi source")
if(SCR_BUILD_ORACLE)
  if(NOT EXISTS "${MUSASHI_DIR}/m68kcpu.c")
    message(FATAL_ERROR "Set MUSASHI_DIR to the pinned source directory")
  endif()
  set(generated "${CMAKE_CURRENT_BINARY_DIR}/musashi-generated")
  file(MAKE_DIRECTORY "${generated}")
  add_executable(m68kmake "${MUSASHI_DIR}/m68kmake.c")
  target_compile_definitions(m68kmake PRIVATE _CRT_SECURE_NO_WARNINGS)
  add_custom_command(OUTPUT "${generated}/m68kops.c" "${generated}/m68kops.h"
    COMMAND m68kmake "${generated}" "${MUSASHI_DIR}/m68k_in.c"
    DEPENDS m68kmake "${MUSASHI_DIR}/m68k_in.c" VERBATIM)
  add_library(musashi_oracle STATIC "${MUSASHI_DIR}/m68kcpu.c"
    "${MUSASHI_DIR}/softfloat/softfloat.c" "${generated}/m68kops.c")
  target_include_directories(musashi_oracle PUBLIC "${MUSASHI_DIR}" PRIVATE "${generated}")
  target_compile_definitions(musashi_oracle PUBLIC
    M68K_EMULATE_010=0 M68K_EMULATE_EC020=0 M68K_EMULATE_020=0
    M68K_EMULATE_030=0 M68K_EMULATE_040=0 M68K_EMULATE_PMMU=0
    M68K_INSTRUCTION_HOOK=1 M68K_EMULATE_ADDRESS_ERROR=1
    PRIVATE _CRT_SECURE_NO_WARNINGS)
  add_executable(wheel_oracle tests/wheel_oracle.cpp)
  target_link_libraries(wheel_oracle PRIVATE scr_physics musashi_oracle)
  target_compile_features(wheel_oracle PRIVATE cxx_std_17)
  add_executable(transform_oracle tests/transform_oracle.cpp)
  target_link_libraries(transform_oracle PRIVATE scr_physics musashi_oracle)
  target_compile_features(transform_oracle PRIVATE cxx_std_17)
  add_executable(expected_wheel_heights_oracle tests/expected_wheel_heights_oracle.cpp)
  target_link_libraries(expected_wheel_heights_oracle PRIVATE scr_physics musashi_oracle)
  target_compile_features(expected_wheel_heights_oracle PRIVATE cxx_std_17)
  add_executable(track_interpolation_oracle tests/track_interpolation_oracle.cpp)
  target_link_libraries(track_interpolation_oracle PRIVATE scr_physics musashi_oracle)
  target_compile_features(track_interpolation_oracle PRIVATE cxx_std_17)
  add_executable(suspension_springs_oracle tests/suspension_springs_oracle.cpp)
  target_link_libraries(suspension_springs_oracle PRIVATE scr_physics musashi_oracle)
  target_compile_features(suspension_springs_oracle PRIVATE cxx_std_17)
  add_executable(wheel_contact_oracle tests/wheel_contact_oracle.cpp)
  target_link_libraries(wheel_contact_oracle PRIVATE scr_physics musashi_oracle)
  target_compile_features(wheel_contact_oracle PRIVATE cxx_std_17)
  add_executable(suspension_response_oracle tests/suspension_response_oracle.cpp)
  target_link_libraries(suspension_response_oracle PRIVATE scr_physics musashi_oracle)
  target_compile_features(suspension_response_oracle PRIVATE cxx_std_17)
  add_executable(track_sampling_oracle tests/track_sampling_oracle.cpp)
  target_link_libraries(track_sampling_oracle PRIVATE scr_physics musashi_oracle)
  target_compile_features(track_sampling_oracle PRIVATE cxx_std_17)
  add_executable(road_suspension_step_oracle tests/road_suspension_step_oracle.cpp)
  target_link_libraries(road_suspension_step_oracle PRIVATE scr_physics musashi_oracle)
  target_compile_features(road_suspension_step_oracle PRIVATE cxx_std_17)
  foreach(stage motion_transforms wheel_motion motion_integration physics_prefix motion_step
      steering_response car_dynamics suspension_effects audio_playback physics_update driving_replay
      track_progress opponent_collision player_controls race_timing driving_effects race_transitions race_restart)
    add_executable(${stage}_oracle tests/${stage}_oracle.cpp)
    target_link_libraries(${stage}_oracle PRIVATE scr_physics musashi_oracle)
    target_compile_features(${stage}_oracle PRIVATE cxx_std_17)
  endforeach()
endif()
