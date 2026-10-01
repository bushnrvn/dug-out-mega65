; Inner loops of the software sprite drawing (see draw_soft in render.c).
;
; A run is up to 8 sprite pixels that fall in one character. br_scan says whether any of them is visible; blit_run copies
; the visible ones (0 = transparent) into the character copy at br_dst, once for each of the four screen rows a game
; pixel covers (the rows are 8 bytes apart in a character).

        .export _br_src, _br_bank, _br_dst, _br_n
        .export _br_scan, _blit_run
        .export _pk_base, _pk_off, _pk_val, _poke_cell
        .importzp ptr1, ptr2, ptr3, ptr4, sreg

        .zeropage
pk_ptr: .res 4                      ; a 32-bit pointer for poke_cell
sp32:   .res 4                      ; a 32-bit pointer to sprite pixels

        .bss
_pk_base: .res 4                    ; chip address of the screen map's first cell
_pk_off:  .res 2                    ; byte offset of the cell
_pk_val:  .res 2                    ; the character number to store
_br_src: .res 2
_br_bank: .res 1
_br_dst: .res 2
_br_n:   .res 1

        .code

; returns A = 0 if every pixel of the run is transparent. The pixels are in chip RAM: br_bank is the 64K bank and br_src the
; offset of the run's first pixel within it.
_br_scan:
        lda     _br_src
        sta     sp32
        lda     _br_src+1
        sta     sp32+1
        lda     _br_bank
        sta     sp32+2
        lda     #0
        sta     sp32+3
        ldz     #0
        lda     #0
@l:     ora     [sp32],z
        inz
        cpz     _br_n
        bne     @l
        ldx     #0
        ldz     #0
        rts

_blit_run:
        lda     _br_src
        sta     sp32
        lda     _br_src+1
        sta     sp32+1
        lda     _br_bank
        sta     sp32+2
        lda     #0
        sta     sp32+3
        lda     _br_dst
        sta     ptr2
        lda     _br_dst+1
        sta     ptr2+1
        clc
        lda     ptr2
        adc     #8
        sta     ptr3
        lda     ptr2+1
        adc     #0
        sta     ptr3+1
        clc
        lda     ptr3
        adc     #8
        sta     ptr4
        lda     ptr3+1
        adc     #0
        sta     ptr4+1
        clc
        lda     ptr4
        adc     #8
        sta     sreg
        lda     ptr4+1
        adc     #0
        sta     sreg+1
        ldy     #0
        ldz     #0
@l:     lda     [sp32],z
        beq     @skip
        sta     (ptr2),y
        sta     (ptr3),y
        sta     (ptr4),y
        sta     (sreg),y
@skip:  iny
        inz
        cpy     _br_n
        bne     @l
        ldz     #0
        rts

; Store the 16-bit character number pk_val at pk_base + pk_off in chip RAM, using the 45GS02's 32-bit indirect store.
_poke_cell:
        clc
        lda     _pk_base
        adc     _pk_off
        sta     pk_ptr
        lda     _pk_base+1
        adc     _pk_off+1
        sta     pk_ptr+1
        lda     _pk_base+2
        adc     #0
        sta     pk_ptr+2
        lda     _pk_base+3
        adc     #0
        sta     pk_ptr+3
        ldz     #0
        lda     _pk_val
        sta     [pk_ptr],z
        inz
        lda     _pk_val+1
        sta     [pk_ptr],z
        ldz     #0
        rts
