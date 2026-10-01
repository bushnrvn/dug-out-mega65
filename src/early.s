; Runs first, as a constructor (the ONCE segment sits low, below $8000, where the ROMs are not laid over RAM):
;
;   1. Load the data files from the disk (device 8) into spare memory. The KERNAL is used for this, so the ROMs must still
;      be switched on. Each file is loaded into bank 5, then copied into attic RAM ($8000000 + 64K * its number) where it
;      stays for the rest of the run. The game copies pieces of it to where they are used.
;   2. Switch off the ROMs laid over RAM at $8000-$BFFF so that the program can use that memory.
;
; If a file cannot be loaded the border turns red and the machine stops.
        .export hide_roms
        .constructor hide_roms, 1

SETLFS  = $FFBA
SETNAM  = $FFBD
LOAD    = $FFD5
SETBNK  = $FF6B

                .segment "ONCE"

; the files, in attic order (file n goes to attic bank n). Lower case here is the disk's upper case (the assembler maps
; text to PETSCII).
name0:  .byte "tiles"
name1:  .byte "title"
name2:  .byte "over"
name3:  .byte "win"
name4:  .byte "sound"
N_FILES = 5
name_lo:  .byte <name0, <name1, <name2, <name3, <name4
name_hi:  .byte >name0, >name1, >name2, >name3, >name4
name_len: .byte 5, 5, 4, 3, 5
fidx:   .byte 0

dmalist:                            ; one enhanced DMA job, copy bank 5 -> attic
        .byte $0B                   ; 11-byte list format
        .byte $81, $80              ; destination MB = $80 (attic)
        .byte $00                   ; end of options
        .byte $00                   ; copy
dm_cnt: .word 0
        .word $0100                 ; source address
        .byte $05                   ; source bank
dm_dst: .word 0
dm_dbk: .byte 0                     ; destination bank (the file's number)
        .byte 0, 0, 0

hide_roms:
        sei
        lda     #$47                ; unlock the VIC-IV registers
        sta     $D02F
        lda     #$53
        sta     $D02F

        ldx     #0
next:   stx     fidx
        lda     #2                  ; SETLFS(2, 8, 0)
        ldx     #8
        ldy     #0
        jsr     SETLFS
        ldx     fidx                ; SETNAM(name, length)
        lda     name_lo,x
        pha
        ldy     name_hi,x
        lda     name_len,x
        plx
        jsr     SETNAM
        lda     #5                  ; SETBNK: data into bank 5 ...
        ldx     #0                  ; ... the name is in bank 0
        jsr     SETBNK
        lda     #0                  ; LOAD (not verify) at $0100
        ldx     #<$0100
        ldy     #>$0100
        jsr     LOAD
        bcs     fail
        ; X/Y = the end address: the byte count is end - $0100
        stx     dm_cnt
        tya
        sec
        sbc     #$01
        sta     dm_cnt+1
        lda     fidx
        sta     dm_dbk              ; attic bank = file number
        lda     #0
        sta     $D702               ; the list is in bank 0
        lda     #>dmalist
        sta     $D701
        lda     #<dmalist
        sta     $D705               ; run it
        ldx     fidx
        inx
        cpx     #N_FILES
        bne     next

        lda     $D030
        and     #$C7                ; clear ROM8 ($8000), ROMA ($A000), ROMC ($C000)
        sta     $D030
        rts

fail:   lda     #2                  ; red border, then stop
        sta     $D020
        jmp     fail
