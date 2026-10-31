# Front-end a riga di comando: argomenti, prompt, Ctrl-C.
add_executable(i8086emu main.cpp)
target_link_libraries(i8086emu PRIVATE emu_debugger)
emu_set_options(i8086emu)
