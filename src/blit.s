; Inner loops of the software sprite drawing (see draw_soft in render.c).
;
; A run is up to 8 sprite pixels that fall in one character. br_scan says whether any of them is visible; blit_run copies
; the visible ones (0 = transparent) into the character copy at br_dst, once for each of the four screen rows a game
; pixel covers (the rows are 8 bytes apart in a character).

        .export _br_src, _br_dst, _br_n
        .export _br_scan, _blit_run
        .importzp ptr1, ptr2, ptr3, ptr4, sreg

        .bss
_br_src: .res 2
_br_dst: .res 2
_br_n:   .res 1

        .code

; returns A = 0 if every pixel of the run is transparent
_br_scan:
        lda _br_src
        sta ptr1
        lda _br_src+1
        sta ptr1+1
        ldy #0
        lda #0
@l:     ora (ptr1),y
        iny
        cpy _br_n
        bne @l
        ldx #0
        rts

_blit_run:
        lda _br_src
        sta ptr1
        lda _br_src+1
        sta ptr1+1
        lda _br_dst
        sta ptr2
        lda _br_dst+1
        sta ptr2+1
        clc
        lda ptr2
        adc #8
        sta ptr3
        lda ptr2+1
        adc #0
        sta ptr3+1
        clc
        lda ptr3
        adc #8
        sta ptr4
        lda ptr3+1
        adc #0
        sta ptr4+1
        clc
        lda ptr4
        adc #8
        sta sreg
        lda ptr4+1
        adc #0
        sta sreg+1
        ldy #0
@l:     lda (ptr1),y
        beq @skip
        sta (ptr2),y
        sta (ptr3),y
        sta (ptr4),y
        sta (sreg),y
@skip:  iny
        cpy _br_n
        bne @l
        rts
