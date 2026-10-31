# Libreria core: CPU, memoria, decoder. Nessun I/O e nessuna dipendenza esterna,
# così si può testare da sola e riusare da debugger, CLI e harness.
add_library(emu_core STATIC
  alu.cpp
  cpu.cpp
  decoder.cpp
  disassembler.cpp
  loader.cpp
  memory.cpp
  opcode_table.cpp
)
target_include_directories(emu_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_features(emu_core PUBLIC cxx_std_20)
emu_set_options(emu_core)
