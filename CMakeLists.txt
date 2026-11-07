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
option(SPACESHIP_DATA_GATE   "Compile the Phase 0 catalogue data gate (cpp/tests/catalog_tests.cpp) into the test suite" ON)
# A warning is the right outcome for `cmake -S . -B build` on a machine that
# simply has no graphics packages. A build that intends to produce the client
# (build.sh, CI) sets this ON, so a missing dependency fails at configure
# time instead of after a full build of the simulation.
option(SPACESHIP_REQUIRE_GUI "Fail configuration if the GUI cannot be built"  OFF)

include(FetchContent)
include(CTest)

# Archive timestamps, once. Do NOT put DOWNLOAD_EXTRACT_TIMESTAMP in the
# FetchContent_Declare calls below: that keyword only exists from CMake 3.24,
# and an older ExternalProject_Add does not reject it, it folds it and its
# value into the URL list. Every dependency then fails to download with
#     At least one entry of URL is a path (invalid in a list)
# which names no package, no version and no fix, and the build dies before it
# has configured a single line of the game. Policy CMP0135 (CMake 3.25) asks
# for the same thing through a channel that exists everywhere it is understood.
if(NOT CMAKE_VERSION VERSION_LESS 3.25)
  cmake_policy(SET CMP0135 NEW)
endif()

set(SPACESHIP_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/assets")

# ---------------------------------------------------------------------------
# Dependencies. Every one is fetched, so a clean checkout needs nothing but a
# compiler, CMake and a network connection for the first configure.
# ---------------------------------------------------------------------------

# nlohmann/json: header-only, ships its own CMakeLists that predates the
# minimum-version policy of modern CMake, so we take only its headers.
FetchContent_Declare(nlohmann_json
  URL      https://codeload.github.com/nlohmann/json/tar.gz/refs/tags/v3.11.3
  SOURCE_SUBDIR do-not-add-this-project)

# raylib, Dear ImGui and rlImGui are only fetched when the GUI target is built.
#
# The check below exists because the client's dependencies report their own
# needs from inside downloaded source: a missing X11 extension aborts with
# "RandR headers not found" out of _deps/raylib-src/src/external/glfw/src/,
# and a too-old CMake aborts with "CMake step for raylib failed". Both kill
# the whole configure, simulation and tests included, and neither message says
# what to install or which the culprit is. So ask the same questions here,
# where there is an answer to give, and skip the client politely instead.
set(SPACESHIP_GUI_REASON "")
if(SPACESHIP_BUILD_GUI)
  # The project, the core and the tests need only 3.20; raylib 6.0 opens with
  # cmake_minimum_required(VERSION 3.25).
  if(CMAKE_VERSION VERSION_LESS 3.25)
    set(SPACESHIP_GUI_REASON "raylib 6.0 requires CMake 3.25 or newer (this is ${CMAKE_VERSION}) -- upgrade CMake, or configure with -DSPACESHIP_BUILD_GUI=OFF to build the simulation and its tests only")
  elseif(UNIX AND NOT APPLE AND NOT WIN32)
    find_package(OpenGL QUIET)
    find_package(X11 QUIET)
    if(NOT OpenGL_FOUND)
      set(SPACESHIP_GUI_REASON "OpenGL development files not found (apt: libgl1-mesa-dev, dnf: mesa-libGL-devel)")
    elseif(NOT X11_FOUND)
      set(SPACESHIP_GUI_REASON "X11 development files not found (apt: libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxext-dev, dnf: libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel libXext-devel)")
    else()
      # X11_FOUND only means the core library was located; glfw additionally
      # requires each of these by name (see the same glfw CMakeLists). Xkb and
      # Xshape are header-only -- their symbols live in libX11 and libXext.
      set(SPACESHIP_X11_APT_Xrandr   "libxrandr-dev")
      set(SPACESHIP_X11_APT_Xinerama "libxinerama-dev")
      set(SPACESHIP_X11_APT_Xcursor  "libxcursor-dev")
      set(SPACESHIP_X11_APT_Xi       "libxi-dev")
      set(SPACESHIP_X11_APT_Xkb      "libx11-dev")
      set(SPACESHIP_X11_APT_Xshape   "libxext-dev")
      set(SPACESHIP_X11_MISSING "")
      set(SPACESHIP_X11_FIX "")
      foreach(ext IN ITEMS Xrandr Xinerama Xcursor Xi Xkb Xshape)
        if(NOT X11_${ext}_INCLUDE_PATH OR
           (NOT ext STREQUAL "Xkb" AND NOT ext STREQUAL "Xshape" AND NOT X11_${ext}_LIB))
          string(APPEND SPACESHIP_X11_MISSING " ${ext}")
          string(APPEND SPACESHIP_X11_FIX " ${SPACESHIP_X11_APT_${ext}}")
        endif()
      endforeach()
      if(NOT SPACESHIP_X11_MISSING STREQUAL "")
        string(STRIP "${SPACESHIP_X11_MISSING}" SPACESHIP_X11_MISSING)
        string(STRIP "${SPACESHIP_X11_FIX}" SPACESHIP_X11_FIX)
        set(SPACESHIP_GUI_REASON
          "X11 extensions not found: ${SPACESHIP_X11_MISSING} (apt: ${SPACESHIP_X11_FIX})")
      endif()
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
    URL      https://codeload.github.com/raysan5/raylib/tar.gz/refs/tags/6.0)
  FetchContent_Declare(imgui
    URL      https://codeload.github.com/ocornut/imgui/tar.gz/refs/tags/v1.92.7)
  FetchContent_Declare(rlImGui
    URL      https://codeload.github.com/raylib-extras/rlImGui/tar.gz/refs/tags/Raylib_6_0)

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
  if(SPACESHIP_REQUIRE_GUI)
    message(FATAL_ERROR
      "Spaceship: the client cannot be built -- ${SPACESHIP_GUI_REASON}.\n"
      "           Reconfigure with -DSPACESHIP_BUILD_GUI=OFF (or -DSPACESHIP_REQUIRE_GUI=OFF)\n"
      "           to build the simulation and its tests without the client.")
  endif()
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
  cpp/data/ChartExpansion.cpp
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
    URL      https://codeload.github.com/catchorg/Catch2/tar.gz/refs/tags/v3.7.1)
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
  # The Phase 0 data gate: the catalogue must hold the reference world. It was
  # left out of the suite while the CI flag that names it (-DSPACESHIP_DATA_GATE)
  # had no definition in this file, so the gate never ran. Now the option
  # controls it, and it is on by default.
  if(SPACESHIP_DATA_GATE)
    target_sources(spaceship_tests PRIVATE cpp/tests/catalog_tests.cpp)
  endif()
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
message(STATUS "  data gate  : ${SPACESHIP_DATA_GATE}")
message(STATUS "")
