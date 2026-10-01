; Music and sound effects on the SID chips, played from a 60 Hz raster interrupt so the tempo does not depend on how long
; the game takes to draw a frame.
;
;   SID 1 ($D400): voice 1 melody (pulse), voice 2 bass (triangle), voice 3 snare (noise)
;   SID 2 ($D420): voice 1 harmony (pulse)
;   SID 3 ($D440): voice 1 sound effects (the effect's volume is the chip's master volume)
;
; The interrupt comes in through the KERNAL (hooked at $0314), so the KERNAL stays usable. The data (tools/make_sound.py) is in chip RAM at $1A000, read with 28-bit pointers. The game talks to the player through
; the request bytes below: it sets them, the interrupt handler acts on them at the start of the next frame.
;
;   snd_req_song  song number to start (or $FF); snd_req_loop  1 = repeat it
;   snd_req_stop  1 = stop the music
;   snd_req_sfx   effect number to start (or $FF); snd_req_pri  its priority (an effect only replaces one of lower or
;                 equal priority)

        .export _snd_init
        .export _snd_req_song, _snd_req_loop, _snd_req_stop, _snd_req_sfx, _snd_req_pri
        .export _snd_log

N_SONGS = 4
SND_MID = $A0                       ; the data starts at $01A000

        .zeropage
wp:     .res 4                      ; a stream pointer, while a voice is being stepped
tp:     .res 4                      ; a pointer for table lookups

        .bss
vp0:    .res 4                      ; each voice's stream pointer, low, middle and bank bytes
vp1:    .res 4
vp2:    .res 4
sofflo: .res 4                      ; each voice's stream offset (to restart the song when it loops)
soffhi: .res 4
vremlo: .res 4                      ; frames left in the voice's current note
vremhi: .res 4
vact:   .res 4                      ; the voice has a song
vplay:  .res 4                      ; a note is sounding (gate on)
loopf:  .res 1
sfx_left: .res 1
sfx_pri:  .res 1
sfx_p:  .res 4                      ; the next effect frame
tmp:    .res 1
wd_lo:  .res 1
wd_hi:  .res 1
_snd_req_song: .res 1
_snd_req_loop: .res 1
_snd_req_stop: .res 1
_snd_req_sfx:  .res 1
_snd_req_pri:  .res 1
_snd_log: .res 16                   ; for tests: the last note of each voice, and a frame counter at [15]

        .rodata
vreg:   .byte $00, $07, $0E, $20    ; each voice's register block, as an offset from $D400
vwave:  .byte $40, $10, $80, $40    ; its waveform: pulse, triangle, noise, pulse

        .code

; ---------------------------------------------------------------------------------------------- setup
_snd_init:
        sei
        lda     #0
        tax
@clr:   sta     $D400,x             ; all four SIDs silent
        sta     $D420,x
        sta     $D440,x
        sta     $D460,x
        inx
        cpx     #$19
        bne     @clr
        lda     #$0F
        sta     $D418               ; full volume on SID 1 and SID 2
        sta     $D438
        ; voice patches: pulse width, attack/decay, sustain/release
        lda     #$08
        sta     $D403               ; melody: pulse width $0800 (square)
        sta     $D405               ;   attack 0, decay 8
        lda     #$A9
        sta     $D406               ;   sustain 10, release 9
        lda     #$08
        sta     $D40C               ; bass: attack 0, decay 8
        lda     #$B8
        sta     $D40D               ;   sustain 11, release 8
        lda     #$09
        sta     $D413               ; snare: attack 0, decay 9, no sustain
        lda     #$00
        sta     $D414
        lda     #$04
        sta     $D423               ; harmony: thinner pulse
        lda     #$08
        sta     $D425
        lda     #$98
        sta     $D426
        lda     #$08
        sta     $D443               ; effects: square, instant attack, full sustain
        lda     #$00
        sta     $D445
        lda     #$F0
        sta     $D446
        sta     $D458               ; (the effect volume is set per frame; start at 0 below)
        lda     #0
        sta     $D458
        ; player state
        ldx     #3
@st:    lda     #0
        sta     vact,x
        sta     vplay,x
        sta     vremlo,x
        sta     vremhi,x
        dex
        bpl     @st
        sta     loopf
        sta     sfx_left
        sta     sfx_pri
        sta     _snd_req_loop
        sta     _snd_req_stop
        lda     #$FF
        sta     _snd_req_song
        sta     _snd_req_sfx
        ; the interrupt: only the raster interrupt, at line 0; the handler lives in RAM (the KERNAL is switched off)
        lda     #$7F
        sta     $DC0D
        sta     $DD0D
        lda     $DC0D
        lda     $DD0D
        lda     $D011
        and     #$7F
        sta     $D011
        lda     #0
        sta     $D012
        lda     #$01
        sta     $D01A
        sta     $D019
        lda     #<irq               ; the KERNAL's interrupt entry pushes A, X, Y, Z and B and then jumps through ($0314);
        sta     $0314               ; this handler takes over from the KERNAL's own (keyboard scan, cursor, ...), and
        lda     #>irq               ; leaves the KERNAL mapped, so the game can still use it to save the best score
        sta     $0315
        cli
        rts

irq:    lda     $D019
        sta     $D019               ; acknowledge
        jsr     music_frame

.ifndef WD_HI
WD_HI = 40
.endif
.ifdef TEST_WD
        inc     wd_lo               ; test builds: a watchdog, so a hang cannot stall a test run (about 170 s)
        bne     @nowd
        inc     wd_hi
        lda     wd_hi
        cmp     #WD_HI
        bcc     @nowd
        lda     #$42
        sta     $D6CF
@nowd:
.endif
        pla                         ; the same way out as the KERNAL's: B, Z, Y, X, A
        tab
        plz
        ply
        plx
        pla
        rti

; ---------------------------------------------------------------------------------------------- one frame
music_frame:
        inc     _snd_log+15
        lda     _snd_req_stop
        beq     @nostop
        lda     #0
        sta     _snd_req_stop
        jsr     stop_all
@nostop:
        lda     _snd_req_song
        cmp     #$FF
        beq     @nosong
        pha
        lda     #$FF
        sta     _snd_req_song
        pla
        jsr     start_song
@nosong:
        lda     _snd_req_sfx
        cmp     #$FF
        beq     @nosfx
        pha
        lda     #$FF
        sta     _snd_req_sfx
        pla
        jsr     start_sfx
@nosfx:
        ldx     #3
@v:     lda     vact,x
        beq     @next
        jsr     voice_step
@next:  dex
        bpl     @v
        jmp     sfx_step

; ---------------------------------------------------------------------------------------------- music
; read the next byte of the stream at wp
rd:     ldz     #0
        lda     [wp],z
        inc     wp
        bne     @1
        inc     wp+1
        bne     @1
        inc     wp+2
@1:     rts

load_wp:
        lda     vp0,x
        sta     wp
        lda     vp1,x
        sta     wp+1
        lda     vp2,x
        sta     wp+2
        lda     #0
        sta     wp+3
        rts

save_wp:
        lda     wp
        sta     vp0,x
        lda     wp+1
        sta     vp1,x
        lda     wp+2
        sta     vp2,x
        rts

; voice X goes back to the start of its stream
restart_voice:
        lda     sofflo,x
        sta     vp0,x
        lda     soffhi,x
        clc
        adc     #SND_MID
        sta     vp1,x
        lda     #1
        adc     #0
        sta     vp2,x
        rts

gate_off:
        ldy     vreg,x
        lda     vwave,x
        sta     $D404,y
        lda     #0
        sta     vplay,x
        rts

stop_all:
        ldx     #3
@l:     lda     #0
        sta     vact,x
        jsr     gate_off
        dex
        bpl     @l
        rts

; A = song number
start_song:
        pha
        jsr     stop_all
        lda     _snd_req_loop
        sta     loopf
        pla
        asl     a
        asl     a
        asl     a                   ; 8 bytes of offsets per song
        taz
        lda     #0                  ; tp = $01A100, the song table
        sta     tp
        lda     #SND_MID+1
        sta     tp+1
        lda     #1
        sta     tp+2
        lda     #0
        sta     tp+3
        ldx     #0
@v:     lda     [tp],z
        sta     sofflo,x
        inz
        lda     [tp],z
        sta     soffhi,x
        inz
        ora     sofflo,x
        beq     @unused
        jsr     restart_voice
        lda     #1
        sta     vact,x
        lda     #0
        sta     vremlo,x
        sta     vremhi,x
        sta     vplay,x
@unused:
        inx
        cpx     #4
        bne     @v
        rts

; step voice X by one frame
voice_step:
        lda     vremhi,x            ; the last frame of a note: let go of the key, so the next note starts afresh
        bne     @chk
        lda     vremlo,x
        cmp     #1
        bne     @chk
        lda     vplay,x
        beq     @chk
        jsr     gate_off
@chk:   lda     vremlo,x
        ora     vremhi,x
        bne     @dec
        jsr     fetch_event
        lda     vact,x
        beq     @done               ; the song ended
@dec:   lda     vremlo,x
        bne     @1
        dec     vremhi,x
@1:     dec     vremlo,x
@done:  rts

fetch_event:
        jsr     load_wp
        jsr     rd                  ; the note: 0 = rest, $FF = end of the song
        cmp     #$FF
        beq     @end
        pha
        jsr     rd
        sta     vremlo,x
        jsr     rd
        sta     vremhi,x
        jsr     save_wp
        pla
        beq     @rest
        jmp     note_on
@rest:  jmp     gate_off
@end:   lda     loopf
        beq     @stop
        jsr     restart_voice
        jmp     fetch_event
@stop:  lda     #0
        sta     vact,x
        jmp     gate_off

; A = MIDI note, X = voice: set the pitch and open the gate
note_on:
        sta     _snd_log,x
        asl     a
        taz
        lda     #0                  ; tp = $01A000, the frequency table
        sta     tp
        lda     #SND_MID
        sta     tp+1
        lda     #1
        sta     tp+2
        lda     #0
        sta     tp+3
        lda     [tp],z
        pha
        inz
        lda     [tp],z
        sta     tmp
        ldy     vreg,x
        pla
        sta     $D400,y
        lda     tmp
        sta     $D401,y
        lda     vwave,x
        ora     #1
        sta     $D404,y
        lda     #1
        sta     vplay,x
        rts

; ---------------------------------------------------------------------------------------------- effects
; A = effect number (the priority is in snd_req_pri)
start_sfx:
        sta     tmp
        lda     sfx_left
        beq     @go
        lda     _snd_req_pri
        cmp     sfx_pri
        bcc     @skip               ; lower priority than the one playing
@go:    lda     tmp
        asl     a
        taz
        lda     #<(N_SONGS * 8)     ; tp = the effect table: $01A100 + 8 * songs
        sta     tp
        lda     #SND_MID+1
        sta     tp+1
        lda     #1
        sta     tp+2
        lda     #0
        sta     tp+3
        lda     [tp],z              ; its offset
        pha
        inz
        lda     [tp],z
        clc
        adc     #SND_MID
        sta     tp+1
        lda     #1
        adc     #0
        sta     tp+2
        pla
        sta     tp
        ldz     #0
        lda     [tp],z              ; the frame count
        sta     sfx_left
        clc
        lda     tp
        adc     #1
        sta     sfx_p
        lda     tp+1
        adc     #0
        sta     sfx_p+1
        lda     tp+2
        adc     #0
        sta     sfx_p+2
        lda     #0
        sta     sfx_p+3
        lda     _snd_req_pri
        sta     sfx_pri
@skip:  rts

sfx_step:
        lda     sfx_left
        beq     @idle
        lda     sfx_p
        sta     tp
        lda     sfx_p+1
        sta     tp+1
        lda     sfx_p+2
        sta     tp+2
        lda     #0
        sta     tp+3
        ldz     #0
        lda     [tp],z
        sta     $D440
        inz
        lda     [tp],z
        sta     $D441
        inz
        lda     [tp],z
        sta     $D444
        inz
        lda     [tp],z
        sta     $D458
        clc
        lda     sfx_p
        adc     #4
        sta     sfx_p
        bcc     @1
        inc     sfx_p+1
        bne     @1
        inc     sfx_p+2
@1:     dec     sfx_left
        bne     @idle
        lda     #0                  ; the effect is over: silence
        sta     $D444
        sta     $D458
@idle:  rts
