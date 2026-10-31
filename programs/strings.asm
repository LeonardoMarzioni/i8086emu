; Manipolazione di stringhe terminate da 0: lunghezza, copia in maiuscolo, copia rovesciata.
;
; Esercita CALL/RET, gli indirizzamenti [SI], [DI], [DI+disp], MOV accumulatore-memoria
; (A1/A3), MOV con i registri di segmento (8C/8E) e il prefisso ES:.
;
; Dati a indirizzi fissi:
;   0200h  text      stringa di partenza (non modificata)
;   0220h  length    lunghezza di text (parola)
;   0230h  upper     copia di text in maiuscolo
;   0250h  reversed  copia di text rovesciata
;
; Assemblare con: nasm -f bin -o strings.bin strings.asm

        bits 16
        cpu 8086
        org 0x100

start:  mov si, text
        call strlen
        mov [length], ax        ; A3: MOV [moffs16], AX

        mov si, text
        mov di, upper
        call to_upper_copy

        mov ax, ds              ; ES = DS, impostato con 8C/8E: reverse_copy scrive
        mov es, ax              ; con il prefisso ES:
        mov si, text
        mov di, reversed
        mov cx, [length]
        call reverse_copy
        hlt

; Lunghezza della stringa in SI. Restituisce AX; modifica SI.
strlen: xor ax, ax
.loop:  cmp byte [si], 0
        jz .done
        inc ax
        inc si
        jmp .loop
.done:  ret

; Copia la stringa da SI a DI, convertendo 'a'..'z' in 'A'..'Z'. Modifica SI, DI, AX, DX.
; Senza JB/JA il confronto con 'a' e 'z' usa il bit di segno di una differenza a 16 bit.
to_upper_copy:
.loop:  mov al, [si]
        mov ah, 0               ; AX = carattere, tra 0 e FFh
        mov dx, ax
        sub dx, 'a'
        and dx, 0x8000          ; 1 se carattere < 'a'
        jnz .store
        mov dx, 'z'
        sub dx, ax
        and dx, 0x8000          ; 1 se carattere > 'z'
        jnz .store
        and al, 0xdf            ; minuscola -> maiuscola: azzera il bit 5
.store: mov [di], al
        inc si
        inc di
        cmp al, 0               ; copiato anche lo 0 finale?
        jnz .loop
        ret

; Copia CX caratteri da SI a ES:DI in ordine inverso e termina con 0. Modifica SI, DI, CX, AL.
reverse_copy:
        add si, cx
        dec si                  ; SI sull'ultimo carattere
        cmp cx, 0
        jz .end
.loop:  mov al, [si]
        mov [es:di], al
        dec si
        inc di
        dec cx
        jnz .loop
.end:   mov byte [es:di], 0
        ret

; I dati iniziano a 0200h: indirizzi fissi, così i test e il debugger li trovano.
        times 0x100 - ($ - $$) db 0
text:   db "Hello, 8086 world!", 0
        times 0x20 - ($ - text) db 0
length: dw 0xFFFF
        times 0x30 - ($ - text) db 0
upper:  times 0x20 db 0xFF
reversed:
        times 0x20 db 0xFF
