cmake_minimum_required(VERSION 3.21)
project(stunt_car_racer VERSION 0.1.0 LANGUAGES C CXX)
include(CTest)
option(SCR_BUILD_DEV_TOOLS "Build development replay and direct-race viewers" ON)
option(SCR_BUILD_ORACLE "Build differential tests against original 68000 bytes" OFF)
option(SCR_EMBED_LAUNCHER_LOGO "Embed the hash-verified launcher logo when available locally" ON)
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

add_library(scr_simulation STATIC
  src/physics/wheel_coordinates.cpp
  src/physics/transform_matrices.cpp
  src/physics/expected_wheel_heights.cpp
  src/track/track_interpolation.cpp
  src/physics/suspension_springs.cpp
  src/physics/wheel_contact.cpp
  src/physics/suspension_response.cpp
  src/track/track_sampling.cpp
  src/physics/road_suspension_step.cpp
  src/physics/motion_transforms.cpp
  src/physics/wheel_motion.cpp
  src/physics/motion_integration.cpp
  src/physics/physics_prefix.cpp
  src/physics/motion_tail.cpp
  src/physics/steering_response.cpp
  src/physics/car_dynamics.cpp
  src/physics/suspension_effects.cpp
  src/audio/audio_playback.cpp
  src/physics/physics_update.cpp
  src/race/player_controls.cpp
  src/track/track_progress.cpp
  src/race/opponent_collision.cpp
  src/race/driving_effects.cpp
  src/race/race_timing.cpp
  src/race/race_transitions.cpp
  src/race/race_restart.cpp
  src/track/track_initialization.cpp
  src/track/draw_bridge.cpp
  src/race/opponent_driving.cpp
)
target_include_directories(scr_simulation PUBLIC src)
target_compile_features(scr_simulation PUBLIC cxx_std_17)
if(MSVC)
  target_compile_options(scr_simulation PRIVATE /W4 /WX /permissive-)
else()
  target_compile_options(scr_simulation PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

# Recovered audio interrupts and native sample playback are presentation state.
add_library(scr_audio STATIC
  src/audio/engine_audio.cpp
  src/audio/paula_mixer.cpp
)
target_link_libraries(scr_audio PUBLIC scr_simulation)
target_include_directories(scr_audio PUBLIC src)
target_compile_features(scr_audio PUBLIC cxx_std_17)
if(MSVC)
  target_compile_options(scr_audio PRIVATE /W4 /WX /permissive-)
else()
  target_compile_options(scr_audio PRIVATE -Wall -Wextra -Werror -pedantic)
endif()
# Rendering consumes independently evolved native state and scene commands.
# It never links or calls the verification CPU.
add_library(scr_render STATIC
  src/render/track_mesh.cpp
  src/render/render_projection.cpp
  src/render/planar_image.cpp
  src/render/software_renderer.cpp
  src/render/track_sides.cpp
  src/render/mountain_horizon.cpp
  src/render/near_track_geometry.cpp
  src/render/near_track_visibility.cpp
  src/render/near_track_frame.cpp
  src/render/opponent_car.cpp
  src/render/opponent_far_track.cpp
  src/render/wheel_animation.cpp
)
target_link_libraries(scr_render PUBLIC scr_simulation)
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
  include(cmake/WindowsVersion.cmake)
  add_library(scr_runtime STATIC
    app/assets/local_asset_pack.cpp
    app/assets/native_startup.cpp
    app/assets/native_asset_initialization.cpp
    app/race/native_race_initialization.cpp
    app/presentation/native_menu_render.cpp
    app/presentation/native_track_preview.cpp
    app/presentation/native_palette_fade.cpp
    app/presentation/native_competition_render.cpp
    app/presentation/native_cockpit_instruments.cpp
    app/presentation/native_wheel_sprites.cpp
    app/presentation/native_cockpit_presentation.cpp
    app/menus/native_menu_flow.cpp
    app/menus/native_opening_flow.cpp
    app/menus/native_player_name.cpp
    app/presentation/native_opening_render.cpp
    app/input/native_keyboard_input.cpp
    app/presentation/native_hall_of_fame.cpp
    app/presentation/native_race_messages.cpp
    app/presentation/native_driving_particles.cpp
    app/presentation/native_cockpit_chains.cpp
    app/presentation/native_damage_holes.cpp
    app/persistence/native_save_state.cpp
    app/menus/native_save_menu.cpp
    app/session/native_multiplayer_state.cpp
    app/presentation/native_multiplayer_render.cpp
    app/presentation/camera_view.cpp
    app/presentation/scene_commands.cpp
  )
  target_include_directories(scr_runtime PUBLIC app)
  target_link_libraries(scr_runtime PUBLIC scr_render scr_audio scr_state bcrypt)
  target_compile_features(scr_runtime PUBLIC cxx_std_17)
  include(cmake/AssetSources.cmake)
  add_library(scr_game STATIC
    app/session/native_game_session.cpp
    app/session/native_career_session.cpp
    app/persistence/native_career_store.cpp
    app/persistence/native_career_checkpoint.cpp
    app/persistence/native_save_store.cpp
    app/race/race_scene.cpp
    app/presentation/race_render_assets.cpp
    app/presentation/presentation_interpolation.cpp
    app/input/drive_input.cpp
    app/audio/game_audio.cpp
  )
  target_include_directories(scr_game PUBLIC app)
  target_link_libraries(scr_game PUBLIC scr_runtime)
  target_compile_features(scr_game PUBLIC cxx_std_17)
  add_library(scr_host_ui STATIC
    app/launcher/native_display_settings.cpp
    app/platform/windows/game_display.cpp
    app/launcher/native_launcher.cpp
    app/platform/windows/launcher.cpp
  )
  target_include_directories(scr_host_ui PUBLIC app)
  target_include_directories(scr_host_ui PRIVATE "${PROJECT_BINARY_DIR}/generated")
  target_link_libraries(scr_host_ui PUBLIC scr_runtime user32 gdi32 gdiplus shell32 ole32 comctl32)
  target_compile_features(scr_host_ui PUBLIC cxx_std_17)
  if(MSVC)
    target_compile_options(scr_host_ui PRIVATE /W4 /utf-8 /permissive-)
  endif()
  add_executable(native_game WIN32 app/main.cpp app/platform/windows/drive_input.cpp app/platform/windows/audio_output.cpp
    "${PROJECT_BINARY_DIR}/player-version.rc")
  set_target_properties(native_game PROPERTIES OUTPUT_NAME "Stunt Car Racer")
  target_link_libraries(native_game PRIVATE scr_game scr_host_ui user32 gdi32 shell32 xinput9_1_0 winmm comdlg32 dwmapi)
  target_compile_features(native_game PRIVATE cxx_std_17)
  target_compile_definitions(native_game PRIVATE UNICODE _UNICODE)
  # The approved launcher logo is the sole embedded-artwork exception.
  if(SCR_EMBED_LAUNCHER_LOGO AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/logo.png")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/setup/branding.json" "${CMAKE_CURRENT_SOURCE_DIR}/logo.png")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/setup/branding.json" branding)
    string(JSON approved_logo_sha GET "${branding}" sha256)
    file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/logo.png" local_logo_sha)
    if(NOT local_logo_sha STREQUAL approved_logo_sha)
      message(FATAL_ERROR "Unreviewed launcher logo: verify its provenance and update setup/branding.json, or set SCR_EMBED_LAUNCHER_LOGO=OFF")
    endif()
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
  # Stage only the player and its notices, never fixtures or test tools.
  install(TARGETS native_game RUNTIME DESTINATION . COMPONENT Runtime)
  install(FILES LICENSE THIRD_PARTY_NOTICES.md DESTINATION . COMPONENT Runtime)
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
