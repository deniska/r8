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
    ldx MOUSE_BTN
    beq .not_pressed

    jsr line_reset
    ldy MOUSE_Y
.loop:
    dey
    bmi .out
    jsr line_next
    jmp .loop
.out

    ldy MOUSE_X
    lda #$00
    cpx #$01
    bne .skip_set_white
    lda #$FF
.skip_set_white:
    sta (LINE),Y
.not_pressed:
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
