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

# ---------------------------------------------------------------------------
# Windows / MSVC
# ---------------------------------------------------------------------------
# The catalogue is full of UTF-8 (m³, O₂, ×, —, →), and MSVC reads source files
# in the local code page unless told otherwise: /utf-8 sets both the source and
# the execution charset, which is what stops C4819 and the mojibake that comes
# with it. It is applied to every C++ target, dependencies included, because the
# narrow string literals have to mean the same thing all the way down.
#
# NOMINMAX keeps <windows.h> from defining min/max as macros (which would break
# every std::min/std::max in the port), and WIN32_LEAN_AND_MEAN drops the
# sockets/RPC/COM half of <windows.h> so nothing it declares can collide with a
# raylib symbol.
if(MSVC)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
endif()
if(WIN32)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
if(MINGW)
  # A downloadable build must not ask the player for libstdc++-6.dll.
  add_link_options(-static)
endif()

option(SPACESHIP_BUILD_GUI   "Build the Raylib + Dear ImGui client" ON)
option(SPACESHIP_BUILD_TESTS "Build the CTest suite"               ON)
option(SPACESHIP_DATA_GATE   "Verify assets/data against Spaceship.hta via node" ON)
# A directory holding a prebuilt raylib (include/raylib.h and lib/libraylib.a),
# as the official release archives ship it. The Windows build downloads the
# win64-mingw archive and points this at it, which is faster and more portable
# than compiling the whole of raylib for Windows on every run.
set(SPACESHIP_RAYLIB_PREBUILT "" CACHE PATH "Use a prebuilt raylib instead of fetching it")

include(FetchContent)
include(CTest)

set(SPACESHIP_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/assets")

# A cross-build ships next to its assets, so the compile-time fallback is the
# relative folder rather than this machine's path. main.cpp prefers ./assets
# anyway; this keeps the console harness and the tests working from the
# repository root (and under Wine, where the host path does not exist).
if(CMAKE_CROSSCOMPILING AND WIN32)
  set(SPACESHIP_ASSETS "assets")
endif()

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
  # WIN32 first, deliberately. A Windows target needs nothing beyond the
  # compiler (raylib carries its own GDI/OpenGL backend), and checking UNIX
  # before WIN32 made a MinGW cross-build ask a Windows target for X11.
  if(WIN32 OR SPACESHIP_RAYLIB_PREBUILT)
    # nothing to look for: the Win32 backend is self-contained
  elseif(APPLE)
    find_package(OpenGL QUIET)
    if(NOT OpenGL_FOUND)
      set(SPACESHIP_GUI_REASON "OpenGL framework not found")
    endif()
  else()
    find_package(OpenGL QUIET)
    find_package(X11 QUIET)
    if(NOT OpenGL_FOUND)
      set(SPACESHIP_GUI_REASON "OpenGL development files not found")
    elseif(NOT X11_FOUND)
      set(SPACESHIP_GUI_REASON "X11 development files not found (libx11-dev, libxrandr-dev, libxinerama-dev, libxcursor-dev, libxi-dev)")
    endif()
  endif()
endif()

if(SPACESHIP_BUILD_GUI AND SPACESHIP_GUI_REASON STREQUAL "")
  set(SPACESHIP_GUI ON)
  if(SPACESHIP_RAYLIB_PREBUILT)
    # The official release archive: include/raylib.h, lib/libraylib.a.
    add_library(raylib STATIC IMPORTED)
    set_target_properties(raylib PROPERTIES
      IMPORTED_LOCATION             "${SPACESHIP_RAYLIB_PREBUILT}/lib/libraylib.a"
      INTERFACE_INCLUDE_DIRECTORIES "${SPACESHIP_RAYLIB_PREBUILT}/include")
    if(WIN32)
      # What a static raylib asks of the Windows loader, as its own build files
      # list them. Listed here because an IMPORTED target has no build to read.
      set_target_properties(raylib PROPERTIES INTERFACE_LINK_LIBRARIES
        "opengl32;gdi32;winmm;ole32;shell32;user32")
    elseif(APPLE)
      set_target_properties(raylib PROPERTIES INTERFACE_LINK_LIBRARIES
        "-framework OpenGL;-framework Cocoa;-framework IOKit;-framework CoreAudio")
    else()
      set_target_properties(raylib PROPERTIES INTERFACE_LINK_LIBRARIES
        "GL;m;pthread;dl;rt;X11")
    endif()
    message(STATUS "Spaceship: using the prebuilt raylib at ${SPACESHIP_RAYLIB_PREBUILT}")
  else()
    FetchContent_Declare(raylib
      URL      https://codeload.github.com/raysan5/raylib/tar.gz/refs/tags/6.0
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  endif()
  FetchContent_Declare(imgui
    URL      https://codeload.github.com/ocornut/imgui/tar.gz/refs/tags/v1.92.7
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  # Lowercase by contract: FetchContent's variables are <name>_SOURCE_DIR with
  # the name lowercased (rlimgui_SOURCE_DIR). Declaring it "rlImGui" produced an
  # empty variable and an add_library with a source file of "//rlImGui.cpp",
  # which no machine with X11 had ever hit because the client never configured
  # here. The library target below is still called rlImGui: that name is ours.
  FetchContent_Declare(rlimgui
    URL      https://codeload.github.com/raylib-extras/rlImGui/tar.gz/refs/tags/Raylib_6_0
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

  set(BUILD_EXAMPLES        OFF CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS     OFF CACHE BOOL "" FORCE)
  set(CUSTOMIZE_BUILD       OFF CACHE BOOL "" FORCE)
  set(SUPPORT_FILEFORMAT_TTF ON CACHE BOOL "" FORCE)  # the bundled fonts

  if(SPACESHIP_RAYLIB_PREBUILT)
    FetchContent_MakeAvailable(imgui rlimgui)
  else()
    FetchContent_MakeAvailable(raylib imgui rlimgui)
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
  SPACESHIP_ASSET_DIR="${SPACESHIP_ASSETS}")

if(MSVC)
  # No /WX: a warning from a different MSVC servicing branch must not stop a
  # release build, and this codebase is warning-clean by discipline, not force.
  target_compile_options(spaceship_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(spaceship_core PRIVATE -Wall -Wextra -Wpedantic)
  # GCC 13 turned -Wdangling-reference on inside -Wall. It misfires on helpers
  # that return a reference into a JSON object while being handed a context
  # string; Catalog.cpp passes the context as a string_view now, but the warning
  # has changed shape across 13/14 and the fix should not depend on which image
  # the runner happens to use.
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
     CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 13)
    target_compile_options(spaceship_core PRIVATE -Wno-dangling-reference)
  endif()
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
    "${rlimgui_SOURCE_DIR}/rlImGui.cpp")

  target_include_directories(spaceship_imgui PUBLIC
    "${imgui_SOURCE_DIR}"
    "${rlimgui_SOURCE_DIR}"
    "${rlimgui_SOURCE_DIR}/extras")
  target_link_libraries(spaceship_imgui PUBLIC raylib)
  set_target_properties(spaceship_imgui PROPERTIES CXX_STANDARD 17)

  add_executable(spaceship
    cpp/app/main.cpp
    cpp/ui/Draw.cpp)

  target_link_libraries(spaceship PRIVATE spaceship_core spaceship_imgui raylib)

  # A double-clicked spaceship.exe must open the game and nothing else. On MSVC
  # that means the Windows subsystem; because the entry point is still main(),
  # the CRT's mainCRTStartup is named explicitly, which is the pairing MSVC
  # documents for a GUI application written with main(argc, argv).
  if(WIN32 AND MSVC)
    set_target_properties(spaceship PROPERTIES WIN32_EXECUTABLE ON)
    target_link_options(spaceship PRIVATE
      /SUBSYSTEM:WINDOWS
      /ENTRY:mainCRTStartup)
  elseif(MINGW)
    # -mwindows by another name: same subsystem, same "no console behind the
    # game", and main() stays the entry point.
    set_target_properties(spaceship PROPERTIES WIN32_EXECUTABLE ON)
  endif()

  add_custom_command(TARGET spaceship POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:spaceship>/assets"
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
    cpp/tests/phase7_tests.cpp
    cpp/tests/phase8_tests.cpp
    cpp/tests/spam_audit_tests.cpp
    cpp/tests/phase9_tests.cpp
    cpp/tests/ui_tests.cpp
    cpp/tests/task_tests.cpp
    cpp/tests/nav_tests.cpp
    cpp/tests/crew_tests.cpp
    cpp/tests/station_tests.cpp)
  target_link_libraries(spaceship_tests PRIVATE spaceship_core Catch2::Catch2WithMain)

  include(Catch)
  if(NOT CMAKE_CROSSCOMPILING)
    catch_discover_tests(spaceship_tests
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
  else()
    # A cross-build's test binary is a Windows PE: the build machine cannot run
    # it, so discovery (which executes it) is skipped. The executable is still
    # built, and the Windows job runs it under Wine.
    message(STATUS "Spaceship: cross-compiling, so Catch2 discovery is skipped")
  endif()

  # The drawing code cannot be linked here without X11/OpenGL, but it can be
  # parsed. Skips (77) on a machine with no headers fetched yet.
  add_test(NAME ui.gui-syntax
           COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/check-gui-syntax.sh"
           WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
  set_tests_properties(ui.gui-syntax PROPERTIES SKIP_RETURN_CODE 77)

  # Phase 9's anti-spam gate: the report is generated from the audit case, so a
  # control that grows a double-click (or a report that drifts) fails here.
  add_test(NAME ui.spam-audit
           COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tools/audit-spam-buttons.sh" --check
           WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
  set_tests_properties(ui.spam-audit PROPERTIES
                       ENVIRONMENT "BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}")

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
    # The five save stages are a chain, not standalone cases: each needs the file
    # the previous one wrote. They are registered below, each in its own scratch
    # root, so `ctest -j` can run them at the same time without sharing a path.
    set(SPACESHIP_SAVE_STAGES setup advance write reload utf16)
    foreach(script IN LISTS SPACESHIP_PARITY_SCRIPTS)
      get_filename_component(name "${script}" NAME_WE)
      if(name MATCHES "^save-(setup|advance|write|reload|utf16)$")
        continue()
      endif()
      add_test(NAME parity.${name}
               COMMAND "${CMAKE_COMMAND}"
                       -DNODE=${SPACESHIP_NODE}
                       -DCONSOLE=$<TARGET_FILE:spaceship_console>
                       -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                       -DSCRIPT=${script}
                       -DWORK=${CMAKE_CURRENT_BINARY_DIR}/parity/${name}
                       -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/run-parity.cmake")
      set_tests_properties(parity.${name} PROPERTIES
                           RESOURCE_LOCK spaceship-parity-${name})
    endforeach()

    foreach(stage IN LISTS SPACESHIP_SAVE_STAGES)
      add_test(NAME parity.save-${stage}
               COMMAND "${CMAKE_COMMAND}"
                       -DNODE=${SPACESHIP_NODE}
                       -DCONSOLE=$<TARGET_FILE:spaceship_console>
                       -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                       -DSTAGE=${stage}
                       -DWORK=${CMAKE_CURRENT_BINARY_DIR}/parity/save-${stage}
                       -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/run-save-stage.cmake")
      # Each stage owns its scratch root, so they may run together; the lock is
      # belt-and-braces for a future stage that forgets to isolate itself.
      set_tests_properties(parity.save-${stage} PROPERTIES
                           RESOURCE_LOCK spaceship-saves)
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
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}/parity/save-roundtrip
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/run-save-parity.cmake")
    set_tests_properties(parity.save-roundtrip PROPERTIES
                         RESOURCE_LOCK spaceship-saves)
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
