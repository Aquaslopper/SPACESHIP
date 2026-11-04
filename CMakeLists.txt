cmake_minimum_required(VERSION 3.20)
project(Spaceship LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

add_library(spaceship_core cpp/src/game.cpp)
target_include_directories(spaceship_core PUBLIC cpp/include)

add_executable(spaceship cpp/src/main.cpp)
target_link_libraries(spaceship PRIVATE spaceship_core)

add_executable(spaceship_tests cpp/tests/game_tests.cpp)
target_link_libraries(spaceship_tests PRIVATE spaceship_core)
add_test(NAME spaceship_native_tests COMMAND spaceship_tests)

if(MSVC)
  target_compile_options(spaceship_core PRIVATE /W4 /permissive-)
  target_compile_options(spaceship PRIVATE /W4 /permissive-)
  target_compile_options(spaceship_tests PRIVATE /W4 /permissive-)
else()
  target_compile_options(spaceship_core PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(spaceship PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(spaceship_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
