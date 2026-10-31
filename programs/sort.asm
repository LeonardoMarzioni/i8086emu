; Ordinamento a bolle di un array di parole, in ordine crescente.
;
; Il sottoinsieme ha solo JZ e JNZ come salti condizionati: "b < a" si ricava dal bit di
; segno di b - a. Con valori tra 0 e 7FFFh la differenza non va in overflow, quindi il bit
; di segno è affidabile. È il vincolo sui dati di questo programma.
;
; Risultato: array di COUNT parole ordinato a DS:0200h.
; Assemblare con: nasm -f bin -o sort.bin sort.asm

        bits 16
        cpu 8086
        org 0x100

COUNT   equ 10

start:  mov cx, COUNT - 1       ; numero di passate
outer:  push cx                 ; CX serve anche al ciclo interno
        mov si, array
inner:  call order_pair         ; mette in ordine [SI] e [SI+2]
        add si, 2
        dec cx
        jnz inner
        pop cx
        dec cx                  ; ogni passata porta in fondo il massimo rimasto:
        jnz outer               ; la successiva confronta una coppia in meno
        hlt

; Scambia [SI] e [SI+2] se [SI+2] < [SI]. Preserva tutti i registri.
order_pair:
        push ax
        push bx
        push dx
        mov ax, [si]            ; a
        mov bx, [si+2]          ; b
        mov dx, bx
        sub dx, ax              ; b - a
        and dx, 0x8000          ; bit di segno: 1 se b < a
        jz .done
        mov [si], bx            ; scambio
        mov [si+2], ax
.done:  pop dx
        pop bx
        pop ax
        ret

; I dati iniziano a 0200h: indirizzo fisso, così i test e il debugger li trovano.
        times 0x100 - ($ - $$) db 0
array:  dw 503, 87, 512, 61, 908, 170, 897, 275, 653, 426
