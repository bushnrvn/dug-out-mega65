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

; the files, in attic order: name, length of the name
names:  .byte "tiles"                ; lower case here is the disk's upper case (the assembler maps text to PETSCII)
NAMES_END = *

file_len: .byte 5

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

        ; ---- load TILES (file 0): SETLFS(2, 8, 0), SETNAM("TILES"), SETBNK(5, 0), LOAD(0, $0100)
        lda     #2
        ldx     #8
        ldy     #0
        jsr     SETLFS
        lda     file_len
        ldx     #<names
        ldy     #>names
        jsr     SETNAM
        lda     #5                  ; data into bank 5 ...
        ldx     #0                  ; ... the name is in bank 0
        jsr     SETBNK
        lda     #0                  ; load (not verify)
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
        lda     #0
        sta     dm_dbk              ; attic bank 0
        sta     $D702               ; the list is in bank 0
        lda     #>dmalist
        sta     $D701
        lda     #<dmalist
        sta     $D705               ; run it

        lda     $D030
        and     #$C7                ; clear ROM8 ($8000), ROMA ($A000), ROMC ($C000)
        sta     $D030
        rts

fail:   sta     $0800
        stx     $0801
        sty     $0802
        lda     #$42
        sta     $D6CF
f2:     jmp     f2
