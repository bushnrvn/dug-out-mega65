; Runs first, as a constructor (the ONCE segment sits low, below $8000, where the ROMs are not laid over RAM):
;
;   1. Load the data files from the disk (device 8) into spare memory. The KERNAL is used for this, so the ROMs must still
;      be switched on. Each file is loaded into bank 5, then copied into attic RAM ($8000000 + 64K * its number) where it
;      stays for the rest of the run. The game copies pieces of it to where they are used.
;   2. Switch off the ROMs laid over RAM at $8000-$BFFF so that the program can use that memory.
;
; If a file cannot be loaded the border turns red and the machine stops.
        .export hide_roms
        .export _hs_buf, _save_hiscore
        .importzp ptr1
        .constructor hide_roms, 1

SETLFS  = $FFBA
SETNAM  = $FFBD
LOAD    = $FFD5
SETBNK  = $FF6B
SAVE    = $FFD8

                .segment "ONCE"

; the files, in attic order (file n goes to attic bank n). Lower case here is the disk's upper case (the assembler maps
; text to PETSCII).
name0:  .byte "tiles"
name1:  .byte "title"
name2:  .byte "over"
name3:  .byte "win"
name4:  .byte "sound"
name5:  .byte "sprites"
N_FILES = 6
name_lo:  .byte <name0, <name1, <name2, <name3, <name4, <name5
name_hi:  .byte >name0, >name1, >name2, >name3, >name4, >name5
name_len: .byte 5, 5, 4, 3, 5, 7
fidx:   .byte 0

; the best score file: 'D', 'O', hundreds low, hundreds high, tens digit. It lives in this low segment, which stays visible
; whatever is mapped over $8000 and up.
_hs_buf: .byte 0, 0, 0, 0, 0
HS_END = *
hs_name: .byte "@0:hiscore"
HS_NAME_LEN = * - hs_name
hs_load_name: .byte "hiscore"
HS_LOAD_LEN = * - hs_load_name

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

        ; the best score, if there is one: LOAD "hiscore" into bank 0 at _hs_buf (ignore a missing file)
        lda     #2
        ldx     #8
        ldy     #0
        jsr     SETLFS
        lda     #HS_LOAD_LEN
        ldx     #<hs_load_name
        ldy     #>hs_load_name
        jsr     SETNAM
        lda     #0
        ldx     #0
        jsr     SETBNK
        lda     #0
        ldx     #<_hs_buf
        ldy     #>_hs_buf
        jsr     LOAD

        lda     $D030
        and     #$C7                ; clear ROM8 ($8000), ROMA ($A000), ROMC ($C000)
        sta     $D030
        rts

fail:   lda     #2                  ; red border, then stop
        sta     $D020
        jmp     fail

; Save the best score file. The KERNAL is happiest in the state it was in when the data files were loaded: interrupts off and the
; ROMs overlaid on $8000-$CFFF. This routine, the file name and the buffer are all in this low segment, so they stay in sight; the
; sound player's data (above $8000) is out of sight for the moment, which is why the interrupt must stay off until the end.
_save_hiscore:
        php
        sei
        lda     #$A5                ; the video chip back in its C65 mode (the KERNAL's disk code expects it) ...
        sta     $D02F
        lda     #$96
        sta     $D02F
        lda     $D030
        pha
        ora     #$38                ; ROM8, ROMA, ROMC on
        sta     $D030
        lda     #2
        ldx     #8
        ldy     #0
        jsr     SETLFS
        lda     #HS_NAME_LEN
        ldx     #<hs_name
        ldy     #>hs_name
        jsr     SETNAM
        lda     #0                  ; data and name are in bank 0
        ldx     #0
        jsr     SETBNK
        lda     #<_hs_buf
        sta     ptr1
        lda     #>_hs_buf
        sta     ptr1+1
        lda     #ptr1               ; SAVE: the zero page address of the start pointer, then the end address
        ldx     #<HS_END
        ldy     #>HS_END
        jsr     SAVE
        lda     #$7F                ; the KERNAL's disk code may have switched CIA interrupts on; the game wants only the raster one
        sta     $DC0D
        sta     $DD0D
        lda     $DC0D
        lda     $DD0D
        pla
        sta     $D030
        lda     #$47                ; ... and back to the MEGA65 mode the game uses
        sta     $D02F
        lda     #$53
        sta     $D02F
        plp
        rts


