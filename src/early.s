; Runs first, as a constructor (the ONCE segment sits below $8000): switch off the ROMs laid over RAM at $8000-$BFFF
; so that the program can use that memory.
        .export hide_roms
        .constructor hide_roms, 1

        .segment "ONCE"
hide_roms:
        sei
        lda     #$47                ; unlock the VIC-IV registers
        sta     $D02F
        lda     #$53
        sta     $D02F
        lda     $D030
        and     #$C7                ; clear ROM8 ($8000), ROMA ($A000), ROMC ($C000)
        sta     $D030
        rts
