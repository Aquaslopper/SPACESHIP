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
        DESCRIPTION "Spaceship Command, native port of the Legacy code_Spaceship.txt"
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
  if(UNIX AND NOT APPLE AND NOT WIN32)
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
  # Keep config.h's numeric defaults. Raylib 6's custom CMake parser treats
  # even '#define SUPPORT_CUSTOM_FRAME_CONTROL 0' as ON, disabling normal
  # screen swapping, pacing and input polling. Never enable that broad mode.
  set(CUSTOMIZE_BUILD OFF CACHE BOOL "" FORCE)

  FetchContent_MakeAvailable(raylib imgui rlImGui)
  # config.h guards JPG with #ifndef, so override this one format directly
  # without replacing every other raylib feature flag.
  target_compile_definitions(raylib PRIVATE SUPPORT_FILEFORMAT_JPG=1)
  # Compile these guards INSIDE raylib with its actual private definitions.
  target_sources(raylib PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/cpp/tests/raylib_config_guard.c")
  target_include_directories(raylib PRIVATE "${raylib_SOURCE_DIR}/src")

  # FetchContent's SOURCE_DIR spelling has varied with declaration casing
  # across CMake releases. Resolve both spellings before forming source paths.
  set(SPACESHIP_IMGUI_SOURCE_DIR "${IMGUI_SOURCE_DIR}")
  if(NOT SPACESHIP_IMGUI_SOURCE_DIR)
    set(SPACESHIP_IMGUI_SOURCE_DIR "${imgui_SOURCE_DIR}")
  endif()
  set(SPACESHIP_RLIMGUI_SOURCE_DIR "${RLIMGUI_SOURCE_DIR}")
  if(NOT SPACESHIP_RLIMGUI_SOURCE_DIR)
    set(SPACESHIP_RLIMGUI_SOURCE_DIR "${rlimgui_SOURCE_DIR}")
  endif()
  if(NOT SPACESHIP_IMGUI_SOURCE_DIR OR NOT SPACESHIP_RLIMGUI_SOURCE_DIR)
    message(FATAL_ERROR "Spaceship: FetchContent did not provide ImGui/rlImGui source directories")
  endif()
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
# Core simulation and game logic
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
  cpp/core/AutoSave.cpp
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
  cpp/core/Signal.cpp
  cpp/audio/Audio.cpp
  cpp/ui/RoomsScreen.cpp
  cpp/ui/SimCoreScreen.cpp
  cpp/ui/AwardsScreen.cpp
  cpp/ui/EventScreen.cpp
  cpp/ui/StationScreen.cpp
  cpp/ui/StoresScreen.cpp
  cpp/ui/NavScreen.cpp
  cpp/ui/WikiScreen.cpp
  cpp/ui/TitleScreen.cpp)

target_include_directories(spaceship_core PUBLIC cpp)
target_link_libraries(spaceship_core PUBLIC spaceship_json)
target_compile_definitions(spaceship_core PUBLIC
  SPACESHIP_ASSET_DIR=\"assets\")

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
    "${SPACESHIP_IMGUI_SOURCE_DIR}/imgui.cpp"
    "${SPACESHIP_IMGUI_SOURCE_DIR}/imgui_draw.cpp"
    "${SPACESHIP_IMGUI_SOURCE_DIR}/imgui_tables.cpp"
    "${SPACESHIP_IMGUI_SOURCE_DIR}/imgui_widgets.cpp"
    "${SPACESHIP_RLIMGUI_SOURCE_DIR}/rlImGui.cpp")

  target_include_directories(spaceship_imgui PUBLIC
    "${SPACESHIP_IMGUI_SOURCE_DIR}"
    "${SPACESHIP_RLIMGUI_SOURCE_DIR}"
    "${SPACESHIP_RLIMGUI_SOURCE_DIR}/extras")
  target_link_libraries(spaceship_imgui PUBLIC raylib)
  set_target_properties(spaceship_imgui PROPERTIES CXX_STANDARD 17)

  add_executable(spaceship
    cpp/app/main.cpp
    cpp/audio/AudioDevice.cpp
    cpp/ui/Draw.cpp)

  target_link_libraries(spaceship PRIVATE spaceship_core spaceship_imgui raylib)
  set_target_properties(spaceship PROPERTIES WIN32_EXECUTABLE TRUE)
  if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(spaceship PRIVATE -mwindows)
  endif()

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
    cpp/tests/simulation_tests.cpp
    cpp/tests/world_tests.cpp
    cpp/tests/jsnumber_tests.cpp
    cpp/tests/spam_audit_tests.cpp
    cpp/tests/phase9_tests.cpp
    cpp/tests/task_tests.cpp
    cpp/tests/nav_tests.cpp
    cpp/tests/crew_tests.cpp
    cpp/tests/station_tests.cpp
    cpp/tests/starter_tests.cpp)
  target_link_libraries(spaceship_tests PRIVATE spaceship_core Catch2::Catch2WithMain)

  # Playback state-machine test with a tiny fake raylib audio device; it does
  # not require a sound card and does not replace the real client build.
  add_executable(spaceship_audio_tests cpp/tests/audio_device_tests.cpp cpp/audio/AudioDevice.cpp)
  target_include_directories(spaceship_audio_tests PRIVATE cpp/tests/audio_stub)
  target_link_libraries(spaceship_audio_tests PRIVATE spaceship_core Catch2::Catch2WithMain)

  include(Catch)
  catch_discover_tests(spaceship_audio_tests WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
  catch_discover_tests(spaceship_tests
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

  # The drawing code cannot be linked here without X11/OpenGL, but it can be
  # parsed. Skips (77) on a machine with no headers fetched yet.
  add_test(NAME ui.gui-syntax
           COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-gui-syntax.sh"
           WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
  set_tests_properties(ui.gui-syntax PROPERTIES SKIP_RETURN_CODE 77)
  add_test(NAME ui.imgui-close-input
           COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-imgui-input.sh"
           WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
  set_tests_properties(ui.imgui-close-input PROPERTIES SKIP_RETURN_CODE 77)
  add_test(NAME ui.native-draw-recorder
           COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-draw.sh" "${CMAKE_CURRENT_BINARY_DIR}"
           WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
  set_tests_properties(ui.native-draw-recorder PROPERTIES SKIP_RETURN_CODE 77)

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
