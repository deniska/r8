FPS_CONFIG    = $FFFD
CANVAS        = $1000
CANVAS_WIDTH  = 64
CANVAS_HEIGHT = 64
KEYBOARD      = $2000
MOUSE_BTN     = $2080
MOUSE_X       = $2081
MOUSE_Y       = $2082
LINE          = $00

    org $8000

init:
    lda #<update
    sta $FFFE
    lda #>update
    sta $FFFE+1

    lda #30
    sta FPS_CONFIG

    rts


update:
    jsr clear_canvas
    ldx MOUSE_BTN
    beq .not_pressed

    jsr line_reset
    ldx MOUSE_Y
.loop:
    dex
    bmi .out
    jsr line_next
    jmp .loop
.out

    ldy MOUSE_X
    lda #$FF
    sta (LINE),Y
.not_pressed:
    rts


clear_canvas:
    jsr line_reset
    ldx #00
.loop_row:
    cpx #CANVAS_HEIGHT
    beq .over_row

    ldy #00
.loop:
    cpy #CANVAS_WIDTH
    beq .over
    lda #00
    sta (LINE),Y
    iny
    jmp .loop
.over:

    jsr line_next
    inx
    jmp .loop_row
.over_row:
    rts


line_next:
    clc
    lda #64
    adc LINE
    sta LINE
    lda #00
    adc LINE+1
    sta LINE+1
    rts


line_reset:
    lda #<CANVAS
    sta LINE
    lda #>CANVAS
    sta LINE+1
    rts
