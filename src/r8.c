#include <stdio.h>
#include <stdint.h>
#define NOB_IMPLEMENTATION
#include "nob.h"

#include "raylib.h"
#include "raymath.h"
#include "no_rom.c"
#include "ui.h"

#define UI_FPS 60 // The FPS the UI is running at
#define DEFAULT_EMU_FPS 5 // Default FPS the 6502 emulator is running at
#include "layout.h"
#define MAX_VECTOR_STEPS (10*1000*1000)
#define BACKGROUND_COLOR 0x181818FF

#define SAMPLERATE 44100
#define AUDIO_BUFFER_SIZE 2048
#define WAVE_TABLE_SIZE 256
#define SOUNDCHIP_CLOCK_RATE 1.0f / 100.0f

// 0x1000 .. 0x2000

static uint8_t MEMORY[1<<16];

// Forward declarations for definitions from fake6502.c
void reset6502();
void step6502();
void rts();
void irq6502();
void push16(uint16_t pushval);
extern uint16_t pc;
extern uint8_t sp, a, x, y, status;
#define FLAG_INTERRUPT 0x04

enum ADSRStage {
    ADSR_NONE, // null stage to reset to after release
    ADSR_ATTACK,
    ADSR_DECAY,
    ADSR_SUSTAIN,
    ADSR_RELEASE,
};

struct AudioChannel {
    uint16_t freq;        // offset 0

    // extra byte for nicer alignment
    // currently does nothing
    // TODO: maybe use for panning
    uint8_t unused;      // offset 2

    // attack and decay control
    //   0000      0000
    // ^attack^   ^decay^
    uint8_t ad;          // offset 3

    // sustain and release control
    //   0000        0000
    // ^sustain^   ^release^
    uint8_t sr;          // offset 4

    uint8_t pulse_width; // offset 5

    uint8_t volume;      // offset 6

    // channel control
    // BITS
    // 0 -- GATE (1 means play, 0 means stop)
    // 0
    // 0
    // 0
    // 0 --+
    // 0   | waveform
    // 0   | control
    // 0 --+
    uint8_t control;     // offset 7

    // internal, not exposed to 6502
    uint32_t phase_accum;
    uint16_t noise_lfsr;
    bool gate_flipped;
    enum ADSRStage stage;
    float adsr_gain;
};

struct AudioChannel channels[4];
uint16_t soundchip_clock = 0;

uint8_t read6502(uint16_t address)
{
    return MEMORY[address];
}

void write6502(uint16_t address, uint8_t value)
{
    MEMORY[address] = value;
}

uint16_t read16(uint16_t address)
{
    return (uint16_t)read6502(address) | ((uint16_t)read6502(address + 1) << 8);
}

void load_rom_at(uint8_t *rom_bytes, size_t rom_count, uint16_t offset)
{
    for (size_t i = 0; i < rom_count; ++i) {
        uint8_t x = rom_bytes[i];
        assert(i + offset < sizeof(MEMORY));
        MEMORY[i + offset] = x;
    }
}

int8_t triangle_wave[WAVE_TABLE_SIZE];
int8_t sawtooth_wave[WAVE_TABLE_SIZE];

// attack duration in seconds
const float attack_envelope[16] = {
    0.002f, 0.008f, 0.016f, 0.024f,
    0.038f, 0.056f, 0.068f, 0.080f,
    0.1f,   0.25f,  0.5f,   0.8f,
    1.0f,   3.0f,   5.0f,   8.0f,
};

// decay & release duration in seconds
const float dr_envelope[16] = {
    0.006f, 0.024f, 0.048f, 0.072f,
    0.114f, 0.168f, 0.204f, 0.240f,
    0.3f,   0.75f,  1.5f,   2.4f,
    3.0f,   9.0f,   15.0f,  24.0f,
};

void generate_wave_tables(void) {
    for (int i = 0; i < WAVE_TABLE_SIZE; ++i) {
        if (i < 128) triangle_wave[i] = (i * 2) - 127;
        else triangle_wave[i] = 127 - ((i - 128) * 2);
    }

    for (int i = 0; i < WAVE_TABLE_SIZE; ++i) {
        sawtooth_wave[i] = (i * 2) - 127;
    }
}

int16_t generate_sample(struct AudioChannel* ch) {
    if (ch->volume == 0) return 0;

    // to achieve highest frequency resolution, utilize all the bits in phase accumulator.
    // effective precision: 4294967296 / 44100 = 0.00001026783138513565 Hz
    // as opposed to 16bit:      65536 / 44100 = 0.67291259765625000000 Hz
    ch->phase_accum += (ch->freq * (1ULL << 32)) / SAMPLERATE;
    uint8_t pos = (ch->phase_accum >> 24) & 0xFF;

    uint32_t attack_samples  = attack_envelope[(ch->ad >> 4)] * SAMPLERATE;
    uint32_t decay_samples   = dr_envelope[(ch->ad & 0x0F)] * SAMPLERATE;
    uint32_t release_samples = dr_envelope[(ch->sr & 0x0F)] * SAMPLERATE;

    // set sustain in increments of 1.0f / 15.0f
    float sustain_level = (float)(ch->sr >> 4) / 0x0F;

    float attack_inc  = 1.0f / attack_samples;
    float decay_inc   = (1.0f - sustain_level) / (float)decay_samples;
    float release_inc = sustain_level / (float)release_samples;

    if (ch->gate_flipped) {
        if (ch->control & 0x80) {
            ch->adsr_gain = 0.0f;
            ch->stage = ADSR_ATTACK;
        } else {
            ch->stage = ADSR_RELEASE;
        }
        ch->gate_flipped = false;
    }

    switch (ch->stage) {
        case ADSR_NONE: {} break;

        case ADSR_ATTACK: {
            ch->adsr_gain += attack_inc;
            if (ch->adsr_gain >= 1.0f) {
                ch->adsr_gain = 1.0f;
                ch->stage = ADSR_DECAY;
            }
        } break;

        case ADSR_DECAY: {
            ch->adsr_gain -= decay_inc;
            if (ch->adsr_gain <= sustain_level) {
                ch->adsr_gain = sustain_level;
                ch->stage = ADSR_SUSTAIN;
            }
        } break;

        case ADSR_SUSTAIN: {} break;

        case ADSR_RELEASE: {
            ch->adsr_gain -= release_inc;
            if (ch->adsr_gain <= 0.0f) {
                ch->adsr_gain = 0.0f;
                ch->stage = ADSR_NONE;
            }
        } break;
    }

    int8_t raw_sample = 0;

    // select instrument based on the lower 4 bits
    switch (ch->control & 0x0F) {
        case 0: break;
        case 1: {
            if (pos < ch->pulse_width) raw_sample = 127;
            else raw_sample = -127;
        } break;
        case 2: raw_sample = triangle_wave[pos]; break;
        case 3: raw_sample = sawtooth_wave[pos]; break;
        case 4: {
            uint16_t bit = ((ch->noise_lfsr >> 0) ^ (ch->noise_lfsr >> 2) ^ (ch->noise_lfsr >> 3) ^ (ch->noise_lfsr >> 5)) & 1;
            ch->noise_lfsr = (ch->noise_lfsr >> 1) | (bit << 15);
            raw_sample = (ch->noise_lfsr & 1) ? 127 : -127;
        } break;
    }

    return (raw_sample * ch->volume * ch->adsr_gain) / 0x0F;
}

void reset_audio_channels(void) {
    channels[0].noise_lfsr = 0xACE1;
    channels[1].noise_lfsr = 0xDEAD;
    channels[2].noise_lfsr = 0xBEEF;
    channels[3].noise_lfsr = 0x1337;
}

void update_audio_channels(void) {
    for (size_t ch = 0; ch < (sizeof(channels) / sizeof(channels[0])); ++ch) {
        // combine the low and high byte
        channels[ch].freq        = (MEMORY[SOUNDCHIP + 8*ch + 1] << 8) | (MEMORY[SOUNDCHIP + 8*ch]);
        channels[ch].unused      =  MEMORY[SOUNDCHIP + 8*ch + 2];
        channels[ch].ad          =  MEMORY[SOUNDCHIP + 8*ch + 3];
        channels[ch].sr          =  MEMORY[SOUNDCHIP + 8*ch + 4];
        channels[ch].pulse_width =  MEMORY[SOUNDCHIP + 8*ch + 5];
        channels[ch].volume      =  MEMORY[SOUNDCHIP + 8*ch + 6];
        uint8_t new_control      =  MEMORY[SOUNDCHIP + 8*ch + 7];

        if ((channels[ch].control & 0x80) != (new_control & 0x80)) {
            channels[ch].gate_flipped = true;
        }

        channels[ch].control = new_control;
    }
}

void play_audio_channels(AudioStream stream) {
    int16_t pcm_buffer[AUDIO_BUFFER_SIZE];
    for (int i = 0; i < AUDIO_BUFFER_SIZE; ++i) {
        int16_t mix_sample = 0;

        for (size_t ch = 0; ch < (sizeof(channels) / sizeof(channels[0])); ++ch) {
            mix_sample += generate_sample(&channels[ch]);
        }
        pcm_buffer[i] = mix_sample;
    }

    UpdateAudioStream(stream, pcm_buffer, AUDIO_BUFFER_SIZE);
}

bool call_vector(uint16_t vector_address)
{
    push16(VECTOR_EXIT);
    pc = vector_address;
    for (size_t i = 0; pc != VECTOR_EXIT + 1 && i < MAX_VECTOR_STEPS; ++i) {
        step6502();
    }
    if (pc != VECTOR_EXIT + 1) {
        nob_log(ERROR, "Vector at $%04X took more than %d iterations to execute so we cancelled it", vector_address, MAX_VECTOR_STEPS);
        return false;
    }
    return true;
}

bool reload_rom(String_Builder *rom, const char *rom_path)
{
    memset(MEMORY, 0, sizeof(MEMORY));

    if (rom_path == NULL) {
        load_rom_at(no_rom, ARRAY_LEN(no_rom), ENTRY_POINT);
    } else {
        rom->count = 0;
        if (!read_entire_file(rom_path, rom)) return false;
        load_rom_at((uint8_t*)rom->items, rom->count, ENTRY_POINT);
    }

    reset6502();
    MEMORY[FPS_CONFIG] = DEFAULT_EMU_FPS;
    reset_audio_channels();
    soundchip_clock = 0;

    return true;
}

int main(int argc, char **argv)
{
    const char *program_name = shift(argv, argc);
    UNUSED(program_name);

    char *rom_path = NULL;
    String_Builder rom = {0};

    if (argc > 0) {
        rom_path = strdup(shift(argv, argc));
    }

    reload_rom(&rom, rom_path);
    call_vector(ENTRY_POINT);

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(800, 600, "r8");
    if (!IsWindowReady()) {
        TraceLog(LOG_ERROR, "Could not initialize the Window. See error messages above.");
        return 1;
    }
    SetTargetFPS(UI_FPS);
    SetExitKey(KEY_NULL);

    reset_audio_channels();
    generate_wave_tables();

    InitAudioDevice();
    SetAudioStreamBufferSizeDefault(AUDIO_BUFFER_SIZE);
    AudioStream stream = LoadAudioStream(SAMPLERATE, 16, 1);
    SetAudioStreamPan(stream, 0.0f); // center
    PlayAudioStream(stream);

    float soundchip_clock_accum = 0.0f;

    Texture canvas = LoadTextureFromImage((Image) {
        .data    = MEMORY + CANVAS,
        .width   = CANVAS_WIDTH,
        .height  = CANVAS_HEIGHT,
        .mipmaps = 1,
        .format  = PIXELFORMAT_UNCOMPRESSED_GRAYSCALE,
    });
    SetTextureWrap(canvas, TEXTURE_WRAP_CLAMP);

    float global_timer = 0;
    bool pause = false;
    Rectangle ui_box = {0};
    bool pause_button_down = false;
    bool reset_button_down = false;
    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_R) && IsKeyDown(KEY_LEFT_CONTROL)) {
            reload_rom(&rom, rom_path);
            call_vector(ENTRY_POINT);
        }

        if (IsFileDropped()) {
            FilePathList files = LoadDroppedFiles();
            assert(files.count > 0);

            free(rom_path);
            rom_path = strdup(files.paths[files.count-1]);
            reload_rom(&rom, rom_path);
            call_vector(ENTRY_POINT);

            UnloadDroppedFiles(files);
        }

        float w  = GetRenderWidth();
        float h  = GetRenderHeight();

        Camera2D camera = {0};
        camera.offset = (Vector2){w*0.5, h*0.5};
        camera.target = (Vector2){
            ui_box.x + ui_box.width*0.5,
            ui_box.y + ui_box.height*0.5,
        };
        if (w/h < ui_box.width/ui_box.height) {
            camera.zoom = w/ui_box.width;
        } else {
            camera.zoom = h/ui_box.height;
        }

        if (!pause) {
            int emu_fps = MEMORY[FPS_CONFIG] & 31;
            if (emu_fps == 0) { emu_fps = DEFAULT_EMU_FPS; }
            float emu_delta_time = 1.f / (float) emu_fps;
            float a = fmodf(global_timer, emu_delta_time);
            global_timer += GetFrameTime();
            float b = fmodf(global_timer, emu_delta_time);

            update_audio_channels();
            soundchip_clock_accum += GetFrameTime();
            while (soundchip_clock_accum >= SOUNDCHIP_CLOCK_RATE) {
                soundchip_clock++;
                soundchip_clock_accum -= SOUNDCHIP_CLOCK_RATE;
            }
            MEMORY[SOUNDCLOCK]     = soundchip_clock & 0xFF;
            MEMORY[SOUNDCLOCK + 1] = soundchip_clock >> 8;

            if (b < a) {
                for (int c = 0; c < 128; ++c) {
                    MEMORY[KEYBOARD + c] = IsKeyDown(c);
                }
                MEMORY[KEYBOARD + '\b'] = IsKeyDown(KEY_BACKSPACE);
                MEMORY[KEYBOARD + '\n'] = IsKeyDown(KEY_ENTER);
                MEMORY[KEYBOARD + 0x1B] = IsKeyDown(KEY_ESCAPE);

                Vector2 mouse_pos = GetScreenToWorld2D(GetMousePositionDPI(), camera);
                int mouse_x = (int) mouse_pos.x;
                int mouse_y = (int) mouse_pos.y;
                int mouse_btn_state = (
                    IsMouseButtonDown(MOUSE_BUTTON_LEFT)   << 0
                  | IsMouseButtonDown(MOUSE_BUTTON_RIGHT)  << 1
                  | IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) << 2
                );
                if (mouse_x >= 0 && mouse_x < CANVAS_WIDTH && mouse_y >= 0 && mouse_y < CANVAS_HEIGHT) {
                    MEMORY[MOUSE_BTN] = mouse_btn_state;
                    MEMORY[MOUSE_X] = mouse_x;
                    MEMORY[MOUSE_Y] = mouse_y;
                }
                if (mouse_btn_state == 0) {
                    MEMORY[MOUSE_BTN] = mouse_btn_state;
                }

                call_vector(read16(UPDATE_VECTOR));
            }
            if (IsAudioStreamProcessed(stream)) {
                play_audio_channels(stream);
            }
        }

        UpdateTexture(canvas, MEMORY + CANVAS);

        BeginDrawing(); {
            ClearBackground(GetColor(BACKGROUND_COLOR));
            BeginMode2D(camera); {
                memset(&ui_box, 0, sizeof(ui_box));

                DrawTextureEx(canvas, Vector2Zero(), 0, 1, WHITE);
                Rectangle canvas_box = {0, 0, canvas.width, canvas.height};
                box_merge(&ui_box, canvas_box);

                float margin_between_canvas_and_buttons = 0.03*canvas.width;
                Rectangle button_box = {0};
                button_box.x      = canvas_box.x + canvas.width + margin_between_canvas_and_buttons;
                button_box.y      = canvas_box.y + margin_between_canvas_and_buttons;
                button_box.width  = canvas_box.width*0.07;
                button_box.height = button_box.width;

                // Toggle Pause Button
                if (button(&pause_button_down, button_box, pause ? play_icon : pause_icon, camera)) {
                    pause = !pause;
                }
                box_merge(&ui_box, button_box);

                // Reset Button
                button_box.y += button_box.height + margin_between_canvas_and_buttons;
                if (button(&reset_button_down, button_box, reset_icon, camera)) {
                    reload_rom(&rom, rom_path);
                    call_vector(ENTRY_POINT);
                }
                box_merge(&ui_box, button_box);

                ui_box = box_pad(ui_box, 2);
            } EndMode2D();
        } EndDrawing();
    }

    UnloadAudioStream(stream);
    CloseAudioDevice();
    CloseWindow();

    return 0;
}

#include "ui.c"
