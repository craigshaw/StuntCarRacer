cmake_minimum_required(VERSION 3.20)
project(stunt_car_racer_recovery LANGUAGES C CXX)

add_library(scr_physics STATIC src/wheel_coordinates.cpp)
target_include_directories(scr_physics PUBLIC src)
target_compile_features(scr_physics PUBLIC cxx_std_17)
if(MSVC)
  target_compile_options(scr_physics PRIVATE /W4 /WX /permissive-)
else()
  target_compile_options(scr_physics PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

# Musashi belongs exclusively to the verification executable. scr_physics has
# no emulator dependency and remains a normal native C++ library.
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
endif()
