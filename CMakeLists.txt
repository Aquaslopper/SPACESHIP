# ============================================================================
#  Spaceship Command — native C++ edition
#  --------------------------------------------------------------------------
#  Phase 0: build system, extracted catalogue, loader and the data gate.
#
#  Targets
#    spaceship_core     static library, no graphics, no window (the game)
#    spaceship          the Raylib + Dear ImGui client (optional, see below)
#    spaceship_tests    CTest suite over the core
#
#  The GUI target is only defined when the platform's development packages are
#  present (OpenGL + X11 on Linux, the system frameworks on macOS, nothing
#  extra on Windows). Configure prints a clear line either way; nothing else
#  in the build depends on it, so the simulation and its tests always build.
# ============================================================================

cmake_minimum_required(VERSION 3.20)

project(SpaceshipCommand
        VERSION 0.1.0
        DESCRIPTION "Spaceship Command, native port of the reference HTA edition"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(SPACESHIP_BUILD_GUI   "Build the Raylib + Dear ImGui client" ON)
option(SPACESHIP_BUILD_TESTS "Build the CTest suite"               ON)
option(SPACESHIP_DATA_GATE   "Verify assets/data against Spaceship.hta via node" ON)

include(FetchContent)
include(CTest)

set(SPACESHIP_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/assets")

# ---------------------------------------------------------------------------
# Dependencies. Every one is fetched, so a clean checkout needs nothing but a
# compiler, CMake and a network connection for the first configure.
# ---------------------------------------------------------------------------

# nlohmann/json: header-only, ships its own CMakeLists that predates the
# minimum-version policy of modern CMake, so we take only its headers.
FetchContent_Declare(nlohmann_json
  URL      https://codeload.github.com/nlohmann/json/tar.gz/refs/tags/v3.11.3
  SOURCE_SUBDIR do-not-add-this-project
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# raylib, Dear ImGui and rlImGui are only fetched when the GUI target is built.
set(SPACESHIP_GUI_REASON "")
if(SPACESHIP_BUILD_GUI)
  if(UNIX AND NOT APPLE)
    find_package(OpenGL QUIET)
    find_package(X11 QUIET)
    if(NOT OpenGL_FOUND)
      set(SPACESHIP_GUI_REASON "OpenGL development files not found")
    elseif(NOT X11_FOUND)
      set(SPACESHIP_GUI_REASON "X11 development files not found (libx11-dev, libxrandr-dev, libxinerama-dev, libxcursor-dev, libxi-dev)")
    endif()
  elseif(APPLE)
    find_package(OpenGL QUIET)
    if(NOT OpenGL_FOUND)
      set(SPACESHIP_GUI_REASON "OpenGL framework not found")
    endif()
  endif()
endif()

if(SPACESHIP_BUILD_GUI AND SPACESHIP_GUI_REASON STREQUAL "")
  set(SPACESHIP_GUI ON)
  FetchContent_Declare(raylib
    URL      https://codeload.github.com/raysan5/raylib/tar.gz/refs/tags/6.0
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_Declare(imgui
    URL      https://codeload.github.com/ocornut/imgui/tar.gz/refs/tags/v1.92.7
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_Declare(rlImGui
    URL      https://codeload.github.com/raylib-extras/rlImGui/tar.gz/refs/tags/Raylib_6_0
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

  set(BUILD_EXAMPLES        OFF CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS     OFF CACHE BOOL "" FORCE)
  set(CUSTOMIZE_BUILD       OFF CACHE BOOL "" FORCE)
  set(SUPPORT_FILEFORMAT_TTF ON CACHE BOOL "" FORCE)  # the bundled fonts

  FetchContent_MakeAvailable(raylib imgui rlImGui)
elseif(SPACESHIP_BUILD_GUI)
  set(SPACESHIP_GUI OFF)
  message(WARNING
    "Spaceship: GUI skipped -- ${SPACESHIP_GUI_REASON}.\n"
    "           The simulation, the catalogue and every test still build.\n"
    "           Install the development packages and reconfigure to build the client.")
else()
  set(SPACESHIP_GUI OFF)
  message(STATUS "Spaceship: GUI disabled by -DSPACESHIP_BUILD_GUI=OFF")
endif()

# ---------------------------------------------------------------------------
# The catalogue
# ---------------------------------------------------------------------------
FetchContent_MakeAvailable(nlohmann_json)

add_library(spaceship_json INTERFACE)
target_include_directories(spaceship_json INTERFACE
  "${nlohmann_json_SOURCE_DIR}/single_include")

# ---------------------------------------------------------------------------
# Core simulation (Phase 0: catalogue only, no game logic yet)
# ---------------------------------------------------------------------------
add_library(spaceship_core STATIC
  cpp/data/Catalog.cpp
  cpp/core/JsNumber.cpp
  cpp/core/Action.cpp
  cpp/core/Survival.cpp
  cpp/core/CrewLogic.cpp
  cpp/core/Tasks.cpp
  cpp/core/Cooldowns.cpp
  cpp/core/Incidents.cpp
  cpp/core/Simulacron.cpp
  cpp/core/Awards.cpp
  cpp/core/Trace.cpp
  cpp/core/Navigation.cpp
  cpp/core/Orders.cpp
  cpp/core/Economy.cpp
  cpp/core/Projects.cpp
  cpp/core/Ships.cpp
  cpp/core/SaveSystem.cpp
  # The cockpit's arithmetic: theme, layout, tab routing, briefing text and the
  # cross-section. Pure data - no window, no graphics library - so CTest can hold
  # every rectangle and every sentence to the reference.
  cpp/ui/Theme.cpp
  cpp/ui/Layout.cpp
  cpp/ui/Describe.cpp
  cpp/ui/Shell.cpp
  cpp/ui/ShipDiagram.cpp
  cpp/ui/IdleScreen.cpp
  cpp/ui/Fx.cpp
  cpp/ui/Widgets.cpp
  cpp/ui/EngineScreen.cpp
  cpp/ui/O2Screen.cpp
  cpp/ui/MessScreen.cpp
  cpp/ui/CrewSprites.cpp
  cpp/ui/CrewRecord.cpp
  cpp/ui/QuartersScreen.cpp
  cpp/ui/Popups.cpp
  cpp/ui/StationScreen.cpp
  cpp/ui/StoresScreen.cpp
  cpp/ui/NavScreen.cpp)

target_include_directories(spaceship_core PUBLIC cpp)
target_link_libraries(spaceship_core PUBLIC spaceship_json)
target_compile_definitions(spaceship_core PUBLIC
  SPACESHIP_ASSET_DIR="${SPACESHIP_ASSETS}")

if(MSVC)
  target_compile_options(spaceship_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(spaceship_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Console harness: replays command scripts against the core, for the
# differential parity test and for looking at a voyage without a window.
# ---------------------------------------------------------------------------
add_executable(spaceship_console cpp/app/console_main.cpp)
target_link_libraries(spaceship_console PRIVATE spaceship_core)

# ---------------------------------------------------------------------------
# Client
# ---------------------------------------------------------------------------
if(SPACESHIP_GUI)
  add_library(spaceship_imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${rlImGui_SOURCE_DIR}/rlImGui.cpp")

  target_include_directories(spaceship_imgui PUBLIC
    "${imgui_SOURCE_DIR}"
    "${rlImGui_SOURCE_DIR}"
    "${rlImGui_SOURCE_DIR}/extras")
  target_link_libraries(spaceship_imgui PUBLIC raylib)
  set_target_properties(spaceship_imgui PROPERTIES CXX_STANDARD 17)

  add_executable(spaceship
    cpp/app/main.cpp
    cpp/ui/Draw.cpp)

  target_link_libraries(spaceship PRIVATE spaceship_core spaceship_imgui raylib)
  set_target_properties(spaceship PROPERTIES WIN32_EXECUTABLE OFF)

  add_custom_command(TARGET spaceship POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${SPACESHIP_ASSETS}" "$<TARGET_FILE_DIR:spaceship>/assets"
    COMMENT "Copying assets beside the executable")
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(SPACESHIP_BUILD_TESTS)
  FetchContent_Declare(Catch2
    URL      https://codeload.github.com/catchorg/Catch2/tar.gz/refs/tags/v3.7.1
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(Catch2)

  add_executable(spaceship_tests
    cpp/tests/catalog_tests.cpp
    cpp/tests/simulation_tests.cpp
    cpp/tests/world_tests.cpp
    cpp/tests/jsnumber_tests.cpp
    cpp/tests/ui_tests.cpp
    cpp/tests/task_tests.cpp
    cpp/tests/nav_tests.cpp
    cpp/tests/crew_tests.cpp
    cpp/tests/station_tests.cpp)
  target_link_libraries(spaceship_tests PRIVATE spaceship_core Catch2::Catch2WithMain)

  include(Catch)
  catch_discover_tests(spaceship_tests
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

  # The drawing code cannot be linked here without X11/OpenGL, but it can be
  # parsed. Skips (77) on a machine with no headers fetched yet.
  add_test(NAME ui.gui-syntax
           COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-gui-syntax.sh"
           WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
  set_tests_properties(ui.gui-syntax PROPERTIES SKIP_RETURN_CODE 77)

  # The data gate: the committed catalogue must still match the HTA. This is
  # the test that stops a content edit shipping without its JSON.
  find_program(SPACESHIP_NODE node)
  if(SPACESHIP_DATA_GATE AND SPACESHIP_NODE)
    add_test(NAME catalogue.is-in-sync-with-hta
             COMMAND "${SPACESHIP_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/export-data.cjs" --check
             WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
  elseif(SPACESHIP_DATA_GATE)
    message(WARNING "Spaceship: node not found, the catalogue sync test is not registered")
  endif()

  # The fidelity gate: every script in tests/parity/ is replayed through the
  # reference (in a Node VM) and through spaceship_console, and the two trace
  # streams must match byte for byte. Needs node and the frozen HTA.
  if(SPACESHIP_DATA_GATE AND SPACESHIP_NODE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Spaceship.hta")
    file(GLOB SPACESHIP_PARITY_SCRIPTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/*.script")
    foreach(script IN LISTS SPACESHIP_PARITY_SCRIPTS)
      get_filename_component(name "${script}" NAME_WE)
      add_test(NAME parity.${name}
               COMMAND "${CMAKE_COMMAND}"
                       -DNODE=${SPACESHIP_NODE}
                       -DCONSOLE=$<TARGET_FILE:spaceship_console>
                       -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                       -DSCRIPT=${script}
                       -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/run-parity.cmake")
    endforeach()
  endif()

  # Save parity: the two editions must write and read each other's files, in
  # both encodings. This is the gate that Decision 5 lives or dies by.
  if(SPACESHIP_DATA_GATE AND SPACESHIP_NODE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Spaceship.hta")
    add_test(NAME parity.save-roundtrip
             COMMAND "${CMAKE_COMMAND}"
                     -DNODE=${SPACESHIP_NODE}
                     -DCONSOLE=$<TARGET_FILE:spaceship_console>
                     -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/run-save-parity.cmake")
  endif()
endif()

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "Spaceship Command ${PROJECT_VERSION}")
message(STATUS "  build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "  assets     : ${SPACESHIP_ASSETS}")
message(STATUS "  client     : ${SPACESHIP_GUI}")
message(STATUS "  tests      : ${SPACESHIP_BUILD_TESTS}")
message(STATUS "")
