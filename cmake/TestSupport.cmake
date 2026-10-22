# Test-only include paths and compatibility namespaces never reach the player.
add_library(scr_test_support INTERFACE)
target_include_directories(scr_test_support INTERFACE
  "${PROJECT_SOURCE_DIR}/tests/support" "${PROJECT_SOURCE_DIR}/app")
target_link_libraries(scr_test_support INTERFACE scr_state)

function(scr_add_test_executable target)
  add_executable(${target} ${ARGN})
  target_link_libraries(${target} PRIVATE scr_test_support)
endfunction()
