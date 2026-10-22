# Optional development entry points reuse the player's production libraries.
if(WIN32)
  add_executable(native_replay_viewer WIN32 tools/replay/native_replay_viewer.cpp
    app/windows_drive_input.cpp app/windows_audio_output.cpp)
  target_link_libraries(native_replay_viewer PRIVATE scr_game user32 gdi32 shell32 xinput9_1_0 winmm)
  target_compile_features(native_replay_viewer PRIVATE cxx_std_17)
  target_compile_definitions(native_replay_viewer PRIVATE UNICODE _UNICODE)
  if(MSVC)
    target_compile_options(native_replay_viewer PRIVATE /W4 /utf-8 /permissive-)
  endif()
endif()

add_executable(driving_replay_native tools/replay/driving_replay_native.cpp)
target_link_libraries(driving_replay_native PRIVATE scr_physics scr_state)
target_compile_features(driving_replay_native PRIVATE cxx_std_17)
