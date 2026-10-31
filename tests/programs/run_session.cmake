# Esegue la CLI con i comandi letti da un file e confronta l'output con quello atteso.
# Si lancia con cmake -P; variabili: EMU, PROGRAM, WORKDIR, COMMANDS, EXPECTED.

execute_process(
  COMMAND "${EMU}" "${PROGRAM}"
  WORKING_DIRECTORY "${WORKDIR}"
  INPUT_FILE "${COMMANDS}"
  OUTPUT_VARIABLE actual
  RESULT_VARIABLE result)

if(NOT result EQUAL 0)
  message(FATAL_ERROR "i8086emu exited with ${result}")
endif()

file(READ "${EXPECTED}" expected)
if(NOT actual STREQUAL expected)
  message(FATAL_ERROR "output differs from ${EXPECTED}:\n${actual}")
endif()
