# Uses --reference-fit for the original fully fitted HTA, not the new starter.
# ============================================================================
#  run-parity.cmake — one differential case: reference vs port.
#  ---------------------------------------------------------------------------
#  Invoked by CTest with:
#    -DNODE=<node>  -DCONSOLE=<spaceship_console>  -DSOURCE_DIR=<repo>
#    -DSCRIPT=<tests/parity/*.script>  [-DWORK=<scratch dir>]
#
#  Both sides read the same script and print the same canonical trace. If a
#  single byte differs the case fails, and the first differing line is shown.
#
#  Isolation: the four save-stage scripts write `runtime/saves/rt-*.json`, and
#  several of them share file names. Running them concurrently under `ctest -j`
#  used to race on those paths, so every case is given its own scratch root
#  (WORK, unique per test) and the script is rewritten to point there. The
#  translation is textual and identity-preserving — the reference and the port
#  see exactly the same path — so the trace cannot move.
# ============================================================================

if(NOT DEFINED WORK OR WORK STREQUAL "")
  set(WORK "${CMAKE_CURRENT_BINARY_DIR}/parity")
endif()
set(SAVES "${WORK}/saves")
file(MAKE_DIRECTORY "${SAVES}")

# The script to run: a copy whose runtime/saves lives under WORK.
get_filename_component(script_name "${SCRIPT}" NAME_WE)
set(script_copy "${WORK}/${script_name}.script")
file(READ "${SCRIPT}" script_text)
string(REPLACE "runtime/saves" "${SAVES}" script_text "${script_text}")
file(WRITE "${script_copy}" "${script_text}")

execute_process(
  COMMAND ${NODE} ${SOURCE_DIR}/tools/trace-voyage.cjs ${script_copy}
  WORKING_DIRECTORY ${SOURCE_DIR}
  OUTPUT_VARIABLE js_out
  ERROR_VARIABLE js_err
  RESULT_VARIABLE js_rc)

if(NOT js_rc EQUAL 0)
  message(FATAL_ERROR "reference harness failed (${js_rc}):\n${js_err}")
endif()

execute_process(
  COMMAND ${CONSOLE} --reference-fit ${script_copy}
  WORKING_DIRECTORY ${SOURCE_DIR}
  OUTPUT_VARIABLE cpp_out
  ERROR_VARIABLE cpp_err
  RESULT_VARIABLE cpp_rc)

if(NOT cpp_rc EQUAL 0)
  message(FATAL_ERROR "spaceship_console failed (${cpp_rc}):\n${cpp_err}")
endif()

if(js_out STREQUAL cpp_out)
  string(REGEX MATCHALL "TRACE" traces "${js_out}")
  list(LENGTH traces count)
  message(STATUS "${SCRIPT}: ${count} checkpoints identical")
  return()
endif()

set(js_lines "${js_out}")
string(REPLACE "\n" ";" js_lines "${js_out}")
string(REPLACE "\n" ";" cpp_lines "${cpp_out}")

set(index 0)
foreach(js_line IN LISTS js_lines)
  list(GET cpp_lines ${index} cpp_line)
  if(NOT js_line STREQUAL cpp_line)
    math(EXPR human "${index} + 1")
    math(EXPR keep "${index} + 1")
    message(FATAL_ERROR
      "differential mismatch at line ${human}\n"
      "  reference: ${js_line}\n"
      "  port     : ${cpp_line}")
  endif()
  math(EXPR index "${index} + 1")
endforeach()

message(FATAL_ERROR "the two streams differ in length")
