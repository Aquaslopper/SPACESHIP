# Uses --reference-fit for the original fully fitted HTA, not the new starter.
# ============================================================================
#  run-save-stage.cmake — one save stage, on its own.
#  ---------------------------------------------------------------------------
#  Invoked by CTest with:
#    -DNODE -DCONSOLE -DSOURCE_DIR -DSTAGE=<setup|advance|write|reload|utf16>
#    -DWORK=<scratch dir, unique to this test>
#
#  The five save-stage scripts are a chain, not five independent cases: advance
#  needs the port's rt-a.json, reload needs the reference's rt-b.json, and so
#  on. Each test therefore builds its own prerequisites inside its own WORK and
#  then compares the trace of its own stage. That keeps five separate gates
#  without any shared file for `ctest -j` to race on.
#
#  The prerequisite stages are run for their files, not their traces; the full
#  bidirectional flow, with every trace compared, is parity.save-roundtrip.
# ============================================================================

if(NOT DEFINED WORK OR WORK STREQUAL "")
  message(FATAL_ERROR "run-save-stage.cmake needs -DWORK=<unique scratch dir>")
endif()
set(SAVES "${WORK}/saves")
file(MAKE_DIRECTORY "${SAVES}")

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
    message(FATAL_ERROR "reference harness failed on ${script} (${rc}):\n${err}")
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
    message(FATAL_ERROR "spaceship_console failed on ${script} (${rc}):\n${err}")
  endif()
  set(${outvar} "${out}" PARENT_SCOPE)
endfunction()

# ---- the prerequisites, each in this test's own saves ----------------------
function(require_stage stage)
  stage_script(${stage} script)
  run_port(${script} out)
endfunction()

if(STAGE STREQUAL "setup")
  # nothing to prepare: the port writes rt-a.json below, in this very test
elseif(STAGE STREQUAL "advance")
  require_stage(save-setup)
elseif(STAGE STREQUAL "write" OR STAGE STREQUAL "reload" OR STAGE STREQUAL "utf16")
  require_stage(save-setup)
  stage_script(save-advance advance_script)
  run_port(${advance_script} advance_out)
  if(STAGE STREQUAL "reload" OR STAGE STREQUAL "utf16")
    stage_script(save-write write_script)
    run_node(${write_script} write_out)
    if(NOT EXISTS ${SAVES}/rt-b.json)
      message(FATAL_ERROR "the reference did not write ${SAVES}/rt-b.json")
    endif()
  endif()
else()
  message(FATAL_ERROR "unknown save stage '${STAGE}'")
endif()

# ---- now the stage itself, reference against port -------------------------
stage_script(save-${STAGE} mine)

if(STAGE STREQUAL "setup")
  # Setup is the port's own writing half; the reference does not have a save
  # writer for a voyage it did not fly. Its result is the file the next stage
  # loads, which is exactly what parity.save-advance then does.
  run_port(${mine} mine_out)
  if(NOT EXISTS ${SAVES}/rt-a.json)
    message(FATAL_ERROR "the port did not write ${SAVES}/rt-a.json")
  endif()
  message(STATUS "save setup: the port wrote rt-a.json")
  return()
endif()

if(STAGE STREQUAL "utf16")
  run_port(${mine} mine_out)
  string(REPLACE "\n" ";" lines "${mine_out}")
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
  message(STATUS "save utf16: slot 1 reads back identically")
  return()
endif()

run_node(${mine} js_out)
run_port(${mine} cpp_out)
if(NOT js_out STREQUAL cpp_out)
  message(FATAL_ERROR "the two editions diverge in stage '${STAGE}'")
endif()
string(REGEX MATCHALL "TRACE" traces "${js_out}")
list(LENGTH traces count)
message(STATUS "save ${STAGE}: ${count} checkpoints identical")
