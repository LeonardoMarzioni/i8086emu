# Logica del debugger: comandi, breakpoint, esecuzione. Scrive su uno stream ricevuto
# dall'esterno, quindi si prova con i test senza un terminale.
add_library(emu_debugger STATIC
  debugger.cpp
  parse.cpp
)
target_link_libraries(emu_debugger PUBLIC emu_core)
emu_set_options(emu_debugger)
