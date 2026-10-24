cmake_minimum_required(VERSION 3.21)
project(stunt_car_racer LANGUAGES C CXX)
include(CTest)
option(SCR_BUILD_DEV_TOOLS "Build development replay and direct-race viewers" ON)
option(SCR_BUILD_ORACLE "Build differential tests against original 68000 bytes" OFF)
set(MUSASHI_DIR "" CACHE PATH "Pinned, locally downloaded Musashi source")
if(SCR_BUILD_ORACLE AND NOT BUILD_TESTING)
  message(FATAL_ERROR "SCR_BUILD_ORACLE requires BUILD_TESTING=ON")
endif()
if(MSVC)
  # The player executable can be copied without a Visual C++ runtime install.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  # Unoptimized native state/snapshot copies exceed MSVC's 1 MiB default.
  # Reserve room for Debug application/tests without changing their sources.
  add_link_options("$<$<CONFIG:Debug>:/STACK:8388608>")
endif()

add_library(scr_physics STATIC src/wheel_coordinates.cpp src/transform_matrices.cpp
  src/expected_wheel_heights.cpp src/track_interpolation.cpp src/suspension_springs.cpp
  src/wheel_contact.cpp src/suspension_response.cpp src/track_sampling.cpp
  src/road_suspension_step.cpp src/motion_transforms.cpp src/wheel_motion.cpp
  src/motion_integration.cpp src/physics_prefix.cpp src/motion_step.cpp
  src/steering_response.cpp src/car_dynamics.cpp src/suspension_effects.cpp
  src/audio_playback.cpp src/physics_update.cpp src/player_controls.cpp
  src/track_progress.cpp src/opponent_collision.cpp src/driving_effects.cpp src/race_timing.cpp
  src/race_transitions.cpp src/race_restart.cpp src/track_initialization.cpp src/draw_bridge.cpp
  src/opponent_driving.cpp)
target_include_directories(scr_physics PUBLIC src)
target_compile_features(scr_physics PUBLIC cxx_std_17)
if(MSVC)
  target_compile_options(scr_physics PRIVATE /W4 /WX /permissive-)
else()
  target_compile_options(scr_physics PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

# Recovered audio interrupts and native sample playback are presentation state.
add_library(scr_audio STATIC src/engine_audio.cpp src/paula_mixer.cpp)
target_link_libraries(scr_audio PUBLIC scr_physics)
target_include_directories(scr_audio PUBLIC src)
target_compile_features(scr_audio PUBLIC cxx_std_17)
if(MSVC)
  target_compile_options(scr_audio PRIVATE /W4 /WX /permissive-)
else()
  target_compile_options(scr_audio PRIVATE -Wall -Wextra -Werror -pedantic)
endif()
# The first native rendering backend consumes independently evolved replay
# state. It never links or calls the verification CPU.
add_library(scr_render STATIC src/track_mesh.cpp src/render_projection.cpp
  src/planar_image.cpp src/software_renderer.cpp src/track_sides.cpp
  src/mountain_horizon.cpp src/near_track_geometry.cpp src/near_track_visibility.cpp
  src/near_track_frame.cpp src/opponent_car.cpp src/opponent_far_track.cpp src/wheel_animation.cpp)
target_link_libraries(scr_render PUBLIC scr_physics)
target_include_directories(scr_render PUBLIC src)
target_compile_features(scr_render PUBLIC cxx_std_17)
if(MSVC)
  target_compile_options(scr_render PRIVATE /W4 /WX /permissive-)
else()
  target_compile_options(scr_render PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

# Header-only adapters preserve the recovered layout without a CPU dependency.
add_library(scr_state INTERFACE)
target_include_directories(scr_state INTERFACE "${PROJECT_SOURCE_DIR}/src")
target_compile_features(scr_state INTERFACE cxx_std_17)

if(WIN32)
  add_library(scr_startup STATIC app/local_asset_pack.cpp app/native_startup.cpp
    app/native_asset_initialization.cpp app/native_race_initialization.cpp
    app/native_menu_render.cpp app/native_track_preview.cpp app/native_palette_fade.cpp
    app/native_competition_render.cpp app/native_cockpit_instruments.cpp app/native_wheel_sprites.cpp
    app/native_cockpit_presentation.cpp app/native_menu_flow.cpp
    app/native_opening_flow.cpp app/native_player_name.cpp app/native_opening_render.cpp
    app/native_keyboard_input.cpp app/native_hall_of_fame.cpp
    app/native_race_messages.cpp app/native_driving_particles.cpp app/native_cockpit_chains.cpp
    app/native_damage_holes.cpp
    app/native_save_state.cpp app/native_save_menu.cpp
    app/native_multiplayer_state.cpp app/native_multiplayer_render.cpp
    app/replay_view.cpp app/replay_scene_commands.cpp)
  target_include_directories(scr_startup PUBLIC app)
  target_link_libraries(scr_startup PUBLIC scr_render scr_audio scr_state bcrypt)
  target_compile_features(scr_startup PUBLIC cxx_std_17)
  add_library(scr_game STATIC app/native_game_session.cpp
    app/native_career_session.cpp
    app/native_career_store.cpp
    app/native_career_checkpoint.cpp
    app/native_save_store.cpp
    app/replay_scene.cpp app/replay_render_assets.cpp app/replay_interpolation.cpp
    app/live_drive.cpp app/game_audio.cpp)
  target_include_directories(scr_game PUBLIC app)
  target_link_libraries(scr_game PUBLIC scr_startup)
  target_compile_features(scr_game PUBLIC cxx_std_17)
  add_library(scr_display STATIC app/native_display_settings.cpp app/windows_game_display.cpp
    app/native_launcher.cpp app/windows_launcher.cpp)
  target_include_directories(scr_display PUBLIC app)
  target_link_libraries(scr_display PUBLIC scr_startup user32 gdi32 gdiplus shell32 ole32 comctl32)
  target_compile_features(scr_display PUBLIC cxx_std_17)
  if(MSVC)
    target_compile_options(scr_display PRIVATE /W4 /utf-8 /permissive-)
  endif()
  add_executable(native_game WIN32 app/native_game.cpp app/windows_drive_input.cpp app/windows_audio_output.cpp)
  set_target_properties(native_game PROPERTIES OUTPUT_NAME "Stunt Car Racer")
  target_link_libraries(native_game PRIVATE scr_game scr_display user32 gdi32 shell32 xinput9_1_0 winmm comdlg32 dwmapi)
  target_compile_features(native_game PRIVATE cxx_std_17)
  target_compile_definitions(native_game PRIVATE UNICODE _UNICODE)
  # Local copyrighted artwork remains ignored; only this resource recipe is tracked.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/logo.png")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/launcher-logo.rc" "101 RCDATA \"${CMAKE_CURRENT_SOURCE_DIR}/logo.png\"\n")
    set(SCR_LAUNCHER_RESOURCE "${CMAKE_CURRENT_BINARY_DIR}/launcher-logo.rc")
    target_sources(native_game PRIVATE "${SCR_LAUNCHER_RESOURCE}")
    set_property(SOURCE "${CMAKE_CURRENT_BINARY_DIR}/launcher-logo.rc" APPEND PROPERTY OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/logo.png")
  endif()
  if(MSVC)
    target_compile_options(native_game PRIVATE /W4 /utf-8 /permissive-)
  endif()
endif()

if(SCR_BUILD_DEV_TOOLS)
  include(cmake/DevTools.cmake)
endif()

if(WIN32)
  # Staging installs only the player application, never fixtures or test tools.
  install(TARGETS native_game RUNTIME DESTINATION . COMPONENT Runtime)
endif()

if(BUILD_TESTING)
  include(cmake/TestSupport.cmake)
  include(cmake/NativeTests.cmake)
  if(SCR_BUILD_ORACLE)
    # Musashi is test-only and never linked into the player application.
    include(cmake/OracleTests.cmake)
  endif()
  include(cmake/RegisterTests.cmake)
endif()
