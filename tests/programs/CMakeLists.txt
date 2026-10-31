# Programmi di prova: una sessione della CLI confrontata con l'output atteso e, se ndisasm è
# installato, il disassemblatore confrontato con ndisasm. L'esecuzione dei programmi con il
# controllo dei risultati è in tests/unit/programs_test.cpp.
set(PROGRAMS_DIR ${PROJECT_SOURCE_DIR}/programs)

add_test(NAME cli_session_fibonacci
  COMMAND ${CMAKE_COMMAND}
    -DEMU=$<TARGET_FILE:i8086emu>
    -DPROGRAM=fibonacci.bin
    -DWORKDIR=${PROGRAMS_DIR}
    -DCOMMANDS=${CMAKE_CURRENT_SOURCE_DIR}/fibonacci_session.txt
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/fibonacci_session.expected
    -P ${CMAKE_CURRENT_SOURCE_DIR}/run_session.cmake)

find_program(NDISASM ndisasm)
if(NDISASM AND UNIX)
  foreach(program fibonacci sort strings)
    add_test(NAME disasm_vs_ndisasm_${program}
      COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/compare_disasm.sh
        $<TARGET_FILE:i8086emu> ${PROGRAMS_DIR}/${program}.bin ${NDISASM})
  endforeach()
else()
  message(STATUS "ndisasm non trovato: confronto del disassemblatore con ndisasm disattivato")
endif()
