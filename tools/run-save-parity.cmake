# ============================================================================
#  run-save-parity.cmake — the save files must be each other's.
#  ---------------------------------------------------------------------------
#  Four stages, each one a real boundary crossing:
#
#   1. the port flies a voyage and writes the save        (C++)
#   2. the reference loads that file and spends 20 actions (Node VM)
#      the port loads the same file and spends 20 actions  (C++)   -> compare
#   3. the reference re-saves what it loaded and advanced  (Node VM)
#   4. both editions load the reference's save and advance (both)  -> compare
#   5. slot 1's UTF-16LE encoding survives a write and read (C++)
#
#  Any stage failing means the two editions disagree about the format, which is
#  the one thing Decision 5 does not allow.
# ============================================================================

set(ASSETS ${SOURCE_DIR}/assets)
set(WORK ${SOURCE_DIR}/runtime/saves)
file(MAKE_DIRECTORY ${WORK})

function(run_node script outvar)
  execute_process(
    COMMAND ${NODE} ${SOURCE_DIR}/tools/trace-voyage.cjs ${script}
    WORKING_DIRECTORY ${SOURCE_DIR}
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "reference harness failed on ${script} (${rc}):\n${err}")
  endif()
  set(${outvar} "${out}" PARENT_SCOPE)
endfunction()

function(run_port script outvar)
  execute_process(
    COMMAND ${CONSOLE} ${script}
    WORKING_DIRECTORY ${SOURCE_DIR}
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "spaceship_console failed on ${script} (${rc}):\n${err}")
  endif()
  set(${outvar} "${out}" PARENT_SCOPE)
endfunction()

# ---- 1. the port writes a voyage -------------------------------------------
run_port(${SOURCE_DIR}/tests/parity/save-setup.script setup_out)
if(NOT EXISTS ${WORK}/rt-a.json)
  message(FATAL_ERROR "the port did not write runtime/saves/rt-a.json")
endif()

# ---- 2. both editions load it and advance ----------------------------------
run_node(${SOURCE_DIR}/tests/parity/save-advance.script js_advance)
run_port(${SOURCE_DIR}/tests/parity/save-advance.script cpp_advance)
if(NOT js_advance STREQUAL cpp_advance)
  message(FATAL_ERROR "the two editions diverge after loading the port's save")
endif()

# ---- 3. the reference re-saves what it advanced ----------------------------
run_node(${SOURCE_DIR}/tests/parity/save-write.script js_write)
if(NOT EXISTS ${WORK}/rt-b.json)
  message(FATAL_ERROR "the reference did not write runtime/saves/rt-b.json")
endif()

# ---- 4. both editions load the reference's save and advance ----------------
run_node(${SOURCE_DIR}/tests/parity/save-reload.script js_reload)
run_port(${SOURCE_DIR}/tests/parity/save-reload.script cpp_reload)
if(NOT js_reload STREQUAL cpp_reload)
  message(FATAL_ERROR "the two editions diverge after loading the reference's save")
endif()

# ---- 5. UTF-16LE, the encoding slot 1 is written in ------------------------
run_port(${SOURCE_DIR}/tests/parity/save-utf16.script utf16_out)
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

message(STATUS "save parity: port -> reference -> port, and UTF-16LE slot 1")
