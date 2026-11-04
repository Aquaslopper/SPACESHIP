cmake_minimum_required(VERSION 3.20)
project(Spaceship LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Ship a standalone MSVC runtime: players should not need Visual Studio DLLs.
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

enable_testing()

add_library(spaceship_core cpp/src/game.cpp)
target_include_directories(spaceship_core PUBLIC cpp/include)

# Keep the incomplete port explicitly separate from the player-facing Windows EXE.
add_executable(spaceship_console cpp/src/main.cpp)
target_link_libraries(spaceship_console PRIVATE spaceship_core)

if(WIN32)
  add_executable(spaceship WIN32 cpp/src/windows_launcher.cpp)
  target_link_libraries(spaceship PRIVATE user32)
  add_custom_command(TARGET spaceship POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/Spaceship.hta" "$<TARGET_FILE_DIR:spaceship>/Spaceship.hta"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/WINDOWS-README.txt" "$<TARGET_FILE_DIR:spaceship>/WINDOWS-README.txt")
  add_test(NAME spaceship_windows_package COMMAND spaceship --check)
endif()

add_executable(spaceship_tests cpp/tests/game_tests.cpp)
target_link_libraries(spaceship_tests PRIVATE spaceship_core)
add_test(NAME spaceship_native_tests COMMAND spaceship_tests)

if(MSVC)
  target_compile_options(spaceship_core PRIVATE /W4 /permissive-)
  target_compile_options(spaceship_console PRIVATE /W4 /permissive-)
  target_compile_options(spaceship_tests PRIVATE /W4 /permissive-)
else()
  target_compile_options(spaceship_core PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(spaceship_console PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(spaceship_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
