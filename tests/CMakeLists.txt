find_package(Threads REQUIRED)  # il test di Ctrl-C usa un secondo thread

add_executable(unit_tests
  unit/alu_test.cpp
  unit/cpu_flow_test.cpp
  unit/cpu_memory_test.cpp
  unit/cpu_test.cpp
  unit/debugger_test.cpp
  unit/decoder_test.cpp
  unit/disassembler_test.cpp
  unit/flags_test.cpp
  unit/loader_test.cpp
  unit/memory_test.cpp
  unit/parse_test.cpp
  unit/programs_test.cpp
  unit/registers_test.cpp
)
target_link_libraries(unit_tests PRIVATE emu_debugger Catch2::Catch2WithMain Threads::Threads)
target_compile_definitions(unit_tests PRIVATE I8086EMU_PROGRAMS_DIR="${PROJECT_SOURCE_DIR}/programs")
emu_set_options(unit_tests)

include(Catch)
catch_discover_tests(unit_tests)

add_subdirectory(programs)
add_subdirectory(ssts)
