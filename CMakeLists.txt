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

add_library(spaceship_core cpp/src/voyage.cpp cpp/src/voyage_views.cpp)
target_include_directories(spaceship_core PUBLIC cpp/include)
target_include_directories(spaceship_core SYSTEM PUBLIC cpp/vendor)

# Portable text client shares the native GUI simulation; no duplicate rules.
add_executable(spaceship_console cpp/src/main.cpp)
target_link_libraries(spaceship_console PRIVATE spaceship_core)

if(WIN32)
  add_executable(spaceship WIN32 cpp/src/windows_native.cpp)
  target_link_libraries(spaceship PRIVATE spaceship_core user32 gdi32 shell32)
  target_compile_definitions(spaceship PRIVATE UNICODE _UNICODE)
  add_custom_command(TARGET spaceship POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/WINDOWS-README.txt" "$<TARGET_FILE_DIR:spaceship>/WINDOWS-README.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/NATIVE-PORT.md" "$<TARGET_FILE_DIR:spaceship>/NATIVE-PORT.md"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/cpp/vendor/LICENSE.json" "$<TARGET_FILE_DIR:spaceship>/JSON-LICENSE.txt")
  add_test(NAME spaceship_native_window COMMAND spaceship --smoke-test)
  set_tests_properties(spaceship_native_window PROPERTIES TIMEOUT 30)
  if(MSVC)
    target_compile_options(spaceship PRIVATE /W4 /permissive- /utf-8)
  endif()
endif()

add_executable(voyage_tests cpp/tests/voyage_tests.cpp)
target_link_libraries(voyage_tests PRIVATE spaceship_core)
add_test(NAME spaceship_voyage_tests COMMAND voyage_tests)


if(MSVC)
  target_compile_options(spaceship_core PRIVATE /W4 /permissive- /utf-8)
  target_compile_options(spaceship_console PRIVATE /W4 /permissive- /utf-8)
  target_compile_options(voyage_tests PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(spaceship_core PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(spaceship_console PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(voyage_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
