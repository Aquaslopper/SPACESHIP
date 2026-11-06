# Uses --reference-fit for the original fully fitted HTA, not the new starter.
# ============================================================================
#  run-save-parity.cmake — the save files must be each other's.
#  ---------------------------------------------------------------------------
#  A chain of stages, each one a real boundary crossing:
#
#   setup    the port flies a voyage and writes rt-a.json        (C++)
#   advance  both editions load it and spend 20 actions            -> compare
#   write    the reference re-saves what it loaded and advanced    -> rt-b.json
#   reload   both editions load the reference's save and advance   -> compare
#   utf16    slot 1's UTF-16LE encoding survives a write and read   (C++)
#
#  Every stage is isolated: WORK is a scratch root unique to the test, the save
#  files live under WORK/saves, and each script is rewritten to point there.
#  That is what makes `ctest -j` safe — the stages used to share
#  runtime/saves/rt-*.json with the differentially-registered copies of the same
#  scripts, and two of those running at once could read a half-written file.
# ============================================================================

if(NOT DEFINED WORK OR WORK STREQUAL "")
  set(WORK "${CMAKE_CURRENT_BINARY_DIR}/save-parity")
endif()
set(SAVES "${WORK}/saves")
file(MAKE_DIRECTORY "${SAVES}")

# Copies a script under WORK with runtime/saves pointed at this test's SAVES.
function(stage_script stage outvar)
  set(copy "${WORK}/${stage}.script")
  file(READ "${SOURCE_DIR}/tests/parity/${stage}.script" text)
  string(REPLACE "runtime/saves" "${SAVES}" text "${text}")
  file(WRITE "${copy}" "${text}")
  set(${outvar} "${copy}" PARENT_SCOPE)
endfunction()

function(run_node script outvar)
  execute_process(
    COMMAND ${NODE} ${SOURCE_DIR}/tools/trace-voyage.cjs ${script}
    WORKING_DIRECTORY ${SOURCE_DIR}
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "reference harness failed (${rc}):\n${err}")
  endif()
  set(${outvar} "${out}" PARENT_SCOPE)
endfunction()

function(run_port script outvar)
  execute_process(
    COMMAND ${CONSOLE} --reference-fit ${script}
    WORKING_DIRECTORY ${SOURCE_DIR}
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "spaceship_console failed (${rc}):\n${err}")
  endif()
  set(${outvar} "${out}" PARENT_SCOPE)
endfunction()

# ---- setup: the port writes a voyage ---------------------------------------
stage_script(save-setup setup_script)
run_port(${setup_script} setup_out)
if(NOT EXISTS ${SAVES}/rt-a.json)
  message(FATAL_ERROR "the port did not write ${SAVES}/rt-a.json")
endif()

# ---- advance: both editions load it and advance ----------------------------
stage_script(save-advance advance_script)
run_node(${advance_script} js_advance)
run_port(${advance_script} cpp_advance)
if(NOT js_advance STREQUAL cpp_advance)
  message(FATAL_ERROR "the two editions diverge after loading the port's save")
endif()

# ---- write: the reference re-saves what it advanced ------------------------
stage_script(save-write write_script)
run_node(${write_script} js_write)
if(NOT EXISTS ${SAVES}/rt-b.json)
  message(FATAL_ERROR "the reference did not write ${SAVES}/rt-b.json")
endif()

# ---- reload: both editions load the reference's save and advance -----------
stage_script(save-reload reload_script)
run_node(${reload_script} js_reload)
run_port(${reload_script} cpp_reload)
if(NOT js_reload STREQUAL cpp_reload)
  message(FATAL_ERROR "the two editions diverge after loading the reference's save")
endif()

# ---- utf16: slot 1's encoding survives a write and a read ------------------
stage_script(save-utf16 utf16_script)
run_port(${utf16_script} utf16_out)
string(REPLACE "\n" ";" lines "${utf16_out}")
list(FILTER lines INCLUDE REGEX "^TRACE ")
list(LENGTH lines count)
if(count LESS 2)
  message(FATAL_ERROR "the UTF-16LE round trip produced no traces")
endif()
list(GET lines 0 first)
list(GET lines 1 second)
if(NOT first STREQUAL second)
  message(FATAL_ERROR "a save written as UTF-16LE does not read back identically")
endif()

message(STATUS "save parity: port -> reference -> port, and UTF-16LE slot 1 (${WORK})")
