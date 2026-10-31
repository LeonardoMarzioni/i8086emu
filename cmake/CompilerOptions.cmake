# Opzioni di compilazione comuni, applicate target per target con emu_set_options().
# Così i warning valgono solo per il nostro codice e non per le dipendenze.

function(emu_set_options target)
  if(MSVC)
    # Su MSVC non esistono -Wpedantic né UBSan; ASan richiederebbe di togliere /RTC1.
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8
                           $<$<BOOL:${EMU_WERROR}>:/WX>)
    return()
  endif()

  # -Wconversion e -Wsign-conversion: in un emulatore uint8_t/uint16_t vengono promossi
  # a int nelle espressioni; ogni troncamento deve essere un cast esplicito.
  target_compile_options(${target} PRIVATE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
    $<$<BOOL:${EMU_WERROR}>:-Werror>)

  # Controllo dei limiti di std::array/std::vector nella libreria standard di GCC.
  target_compile_definitions(${target} PRIVATE $<$<CONFIG:Debug>:_GLIBCXX_ASSERTIONS>)

  if(EMU_SANITIZERS)
    set(sanitizers -fsanitize=address -fsanitize=undefined
                   -fno-sanitize-recover=all -fno-omit-frame-pointer)
    target_compile_options(${target} PRIVATE "$<$<CONFIG:Debug>:${sanitizers}>")
    target_link_options(${target}    PRIVATE "$<$<CONFIG:Debug>:${sanitizers}>")
  endif()
endfunction()
