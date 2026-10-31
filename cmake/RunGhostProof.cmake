foreach(input PROOF_CHIP PROOF_PAYLOAD PROOF_INPUTS PROOF_REFERENCE)
  if(NOT EXISTS "${${input}}")
    message("SKIP: missing local ghost proof input ${${input}}")
    return()
  endif()
endforeach()
execute_process(COMMAND "${PROOF_EXECUTABLE}" "${PROOF_CHIP}" "${PROOF_PAYLOAD}"
  "${PROOF_INPUTS}" "${PROOF_REFERENCE}" "${PROOF_OUTPUT}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
message("${output}")
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Ghost proof failed (${result}): ${error}")
endif()
