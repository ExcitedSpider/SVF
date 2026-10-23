# Standalone regression: no external Test-Suite checkout is required.
cmake_minimum_required(VERSION 3.23)
foreach(required AE LLVM_BIN EXTAPI OUTPUT_DIR)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "Pass -D${required}=<path>")
  endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
set(inputs "${CMAKE_CURRENT_LIST_DIR}")
execute_process(
  COMMAND "${LLVM_BIN}/clang" -O0 -g -Xclang -disable-O0-optnone
          -S -emit-llvm "${inputs}/memory.c" -o "${OUTPUT_DIR}/memory.raw.ll"
  COMMAND_ERROR_IS_FATAL ANY)
execute_process(
  COMMAND "${LLVM_BIN}/opt" -S -passes=mem2reg
          "${OUTPUT_DIR}/memory.raw.ll" -o "${OUTPUT_DIR}/memory.ll"
  COMMAND_ERROR_IS_FATAL ANY)

foreach(mode dense semi-sparse)
  foreach(case null-base memory)
    if(case STREQUAL "null-base")
      set(input "${inputs}/null-base.ll")
      set(detector -null-deref)
    else()
      set(input "${OUTPUT_DIR}/memory.ll")
      set(detector "")
    endif()
    execute_process(
      COMMAND "${AE}" -stat=false "-ae-sparsity=${mode}" "-extapi=${EXTAPI}"
              ${detector} "${input}"
      WORKING_DIRECTORY "${OUTPUT_DIR}"
      TIMEOUT 30 RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    file(WRITE "${OUTPUT_DIR}/${case}-${mode}.log" "${out}${err}")
    if(NOT status STREQUAL "0")
      message(FATAL_ERROR "${case}/${mode}: ${status}\n${out}${err}")
    endif()
    if(case STREQUAL "null-base")
      if(NOT "${out}${err}" MATCHES "Nullptr Dereference \\(1 found\\)")
        message(FATAL_ERROR "${case}/${mode}: expected one null-dereference report")
      endif()
    else()
      string(REGEX MATCHALL "The assertion is successfully verified!!" verified "${out}${err}")
      list(LENGTH verified count)
      if(NOT count EQUAL 6)
        message(FATAL_ERROR "${case}/${mode}: expected six verified assertions, got ${count}\n${out}${err}")
      endif()
    endif()
    message(STATUS "PASS ${case}/${mode}")
  endforeach()
endforeach()
