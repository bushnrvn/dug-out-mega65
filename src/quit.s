; Quit to the MEGA65 Desktop (see the desktop's MEMORY-MAP.md, section 4).
;
; When the desktop starts a program it saves a copy of itself in attic RAM, with a header that includes everything needed to put the machine
; back (the sound player's interrupt, the CIA timer, the video chip) and the routine that does it. This routine:
;   1. checks that there is such a saved desktop (attic $87E0000 starts with "DESK" and version 1): if not it simply returns and the game
;      carries on, because nothing has been touched yet;
;   2. copies the whole 512-byte header to $CE00-$CFFF, which is the top of this game's C stack: the game never runs again, so that is
;      fine, but it means nothing below may rely on the C stack;
;   3. jumps to the restore routine at $CF00 (the header's last 256 bytes), which finishes the job and restarts the desktop.
; It uses only fixed addresses and its own data.
        .export _quit_to_desktop

        .bss
magic:  .res 5

        .code
_quit_to_desktop:
        lda     #0
        sta     $D702               ; the lists are in bank 0
        lda     #>jobmagic
        sta     $D701
        lda     #<jobmagic
        sta     $D705               ; copy the first 5 bytes of the saved header
        ldx     #4                  ; compare the 5 bytes with "DESK" and the version, 1
@c:     lda     magic,x
        cmp     wanted,x
        bne     none
        dex
        bpl     @c
        sei
        lda     #>jobhdr
        sta     $D701
        lda     #<jobhdr
        sta     $D705               ; copy the header (512 bytes) to $CE00
        jmp     $CF00               ; the restore routine, which is in the header's last 256 bytes
none:   rts

wanted: .byte $44, $45, $53, $4B, $01      ; "DESK", version 1

jobmagic: .byte $0B, $80, $87, $81, $00, $00       ; 12-byte list; source megabyte $87, destination megabyte 0, end of options
          .byte $00                                 ; copy
          .word 5                                   ; 5 bytes
          .byte $00, $00, $0E                       ; source: attic offset $E0000
          .word magic                               ; destination
          .byte $00
          .byte $00

jobhdr:   .byte $0B, $80, $87, $81, $00, $00
          .byte $00
          .word $0200                               ; 512 bytes
          .byte $00, $00, $0E                       ; source: attic offset $E0000
          .byte $00, $CE, $00                       ; destination: $CE00
          .byte $00
