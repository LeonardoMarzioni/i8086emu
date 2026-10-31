; Fibonacci: scrive i primi N numeri della successione in un array di parole.
;
; Risultato: fib[0..N-1] a DS:0200h (F(0) = 0, F(1) = 1, ..., F(23) = 28657).
; Usa solo il sottoinsieme dell'emulatore e termina con HLT.
;
; Assemblare con: nasm -f bin -o fibonacci.bin fibonacci.asm

        bits 16
        cpu 8086
        org 0x100

N       equ 24                  ; F(23) = 28657 è l'ultimo valore calcolato

start:  mov di, fib             ; DI punta al prossimo elemento da scrivere
        mov word [di], 0        ; F(0)
        mov word [di+2], 1      ; F(1)
        add di, 4
        mov cx, N - 2           ; elementi ancora da calcolare

next:   mov ax, [di-4]          ; F(n-2)
        add ax, [di-2]          ; + F(n-1)
        mov [di], ax            ; = F(n)
        add di, 2
        dec cx
        jnz next

        hlt                     ; AX contiene l'ultimo valore, F(N-1)

; I dati iniziano a 0200h: indirizzo fisso, così i test e il debugger li trovano.
        times 0x100 - ($ - $$) db 0
fib:    times N dw 0xFFFF       ; sovrascritto dal programma
