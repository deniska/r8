#define NOB_IMPLEMENTATION
#include "thirdparty/nob.h"
#define FLAG_IMPLEMENTATION
#include "thirdparty/flag.h"
#define OLIVEC_IMPLEMENTATION
#include "thirdparty/olive.c"

#define BUILD_FOLDER "build/"
#define SRC_FOLDER "src/"
#define EXAMPLES_FOLDER "examples/"
#define THIRDPARTY_FOLDER "thirdparty/"
#define TARGET_NAME "linux_amd64"
#define RAYLIB_SRC_FOLDER THIRDPARTY_FOLDER "raylib-6.0/src/"
#define GLFW_SRC_FOLDER RAYLIB_SRC_FOLDER "external/glfw/"
#define WAYLAND_PROTOCOLS_FOLDER GLFW_SRC_FOLDER "deps/wayland/"

#include "./src/layout.h"

bool build_raylib(void);
bool generate_no_rom_asm(const char *output_asm_path);
bool rom_to_c(const char *input_path, const char *output_path);
bool build_rom_with_vasm(Cmd *cmd, const char *input_path, const char *output_path);

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);

    char *run_example = NULL;
    bool help = false;
    flag_str_var(&run_example, "run", NULL, "Run the specified example after the build");
    flag_bool_var(&help, "help", false, "Print this help message");

    if (!flag_parse(argc, argv)) {
        fprintf(stderr, "Usage: %s [OPTIONS]\n", flag_program_name());
        flag_print_options(stderr);
        flag_print_error(stderr);
        return 1;
    }

    if (help) {
        fprintf(stderr, "Usage: %s [OPTIONS]\n", flag_program_name());
        flag_print_options(stderr);
        return 0;
    }

    if (!mkdir_if_not_exists(BUILD_FOLDER)) return 1;
    if (!mkdir_if_not_exists(BUILD_FOLDER EXAMPLES_FOLDER)) return 1;

    if (!build_raylib()) return 1;

    Cmd cmd = {0};
    cmd_append(&cmd, "cc");
    cmd_append(&cmd, "-Wall");
    cmd_append(&cmd, "-Wextra");
    cmd_append(&cmd, "-ggdb");
    cmd_append(&cmd, "-c");
    cmd_append(&cmd, SRC_FOLDER"fake6502.c");
    cmd_append(&cmd, "-o", BUILD_FOLDER"fake6502.o");
    if (!cmd_run(&cmd)) return 1;

    if (!generate_no_rom_asm(BUILD_FOLDER"no_rom.asm")) return 1;
    if (!build_rom_with_vasm(&cmd, BUILD_FOLDER"no_rom.asm", BUILD_FOLDER"no_rom.rom")) return 1;
    if (!rom_to_c(BUILD_FOLDER"no_rom.rom", BUILD_FOLDER"no_rom.c")) return 1;

    if (!build_rom_with_vasm(&cmd, EXAMPLES_FOLDER"checker.asm", BUILD_FOLDER EXAMPLES_FOLDER"checker.rom")) return 1;
    if (!build_rom_with_vasm(&cmd, EXAMPLES_FOLDER"box.asm", BUILD_FOLDER EXAMPLES_FOLDER"box.rom")) return 1;
    if (!build_rom_with_vasm(&cmd, EXAMPLES_FOLDER"mouse.asm", BUILD_FOLDER EXAMPLES_FOLDER"mouse.rom")) return 1;

    cmd_append(&cmd, "cc");
    cmd_append(&cmd, "-I"BUILD_FOLDER);
    cmd_append(&cmd, "-I"RAYLIB_SRC_FOLDER);
    cmd_append(&cmd, "-I"THIRDPARTY_FOLDER);
    cmd_append(&cmd, "-Wall");
    cmd_append(&cmd, "-Wextra");
    cmd_append(&cmd, "-ggdb");
    cmd_append(&cmd, "-o", BUILD_FOLDER"r8");
    cmd_append(&cmd, SRC_FOLDER"r8.c");
    cmd_append(&cmd, BUILD_FOLDER"fake6502.o");
    cmd_append(&cmd, "-L"BUILD_FOLDER"raylib_"TARGET_NAME"/");
    cmd_append(&cmd, "-l:libraylib.a");
    cmd_append(&cmd, "-ldl");
    cmd_append(&cmd, "-lX11");
    cmd_append(&cmd, "-lm");
    if (!cmd_run(&cmd)) return 1;

    if (run_example) {
        cmd_append(&cmd, BUILD_FOLDER"r8", temp_sprintf(BUILD_FOLDER EXAMPLES_FOLDER "%s.rom", run_example));
        if (!cmd_run(&cmd)) return 1;
    }

    return 0;
}

static const char *raylib_modules[] = {
    "rcore",
    "raudio",
    "rglfw",
    "rmodels",
    "rshapes",
    "rtext",
    "rtextures",
};

// Taken from the generate_wayland_protocol() calls under GLFW_BUILD_WAYLAND
// in external/glfw/CMakeLists.txt (upstream GLFW, not raylib's own Makefile).
static const char *wayland_protocols[] = {
    "wayland",
    "xdg-shell",
    "xdg-decoration-unstable-v1",
    "viewporter",
    "relative-pointer-unstable-v1",
    "pointer-constraints-unstable-v1",
    "fractional-scale-v1",
    "xdg-activation-v1",
    "idle-inhibit-unstable-v1",
};

bool build_wayland_protocols(const char *output_dir)
{
    bool result = true;
    Cmd cmd = {0};
    Procs procs = {0};

    if (!mkdir_if_not_exists(output_dir)) return_defer(false);

    for (size_t i = 0; i < ARRAY_LEN(wayland_protocols); ++i) {
        const char *xml_path = temp_sprintf(WAYLAND_PROTOCOLS_FOLDER"%s.xml", wayland_protocols[i]);
        const char *header_path = temp_sprintf("%s/%s-client-protocol.h", output_dir, wayland_protocols[i]);
        const char *code_path = temp_sprintf("%s/%s-client-protocol-code.h", output_dir, wayland_protocols[i]);

        if (needs_rebuild1(header_path, xml_path)) {
            cmd_append(&cmd, "wayland-scanner", "client-header", xml_path, header_path);
            if (!cmd_run(&cmd, .async = &procs)) return_defer(false);
        }
        if (needs_rebuild1(code_path, xml_path)) {
            cmd_append(&cmd, "wayland-scanner", "private-code", xml_path, code_path);
            if (!cmd_run(&cmd, .async = &procs)) return_defer(false);
        }
    }

    if (!procs_flush(&procs)) return_defer(false);

defer:
    cmd_free(cmd);
    return result;
}

bool build_raylib(void)
{
    bool result = true;
    Cmd cmd = {0};
    File_Paths object_files = {0};

    Procs procs = {0};

    const char *build_path = BUILD_FOLDER "raylib_" TARGET_NAME;
    const char *wayland_protocols_path = temp_sprintf("%s/wayland-protocols", build_path);

    if (!mkdir_if_not_exists(build_path)) {
        return_defer(false);
    }

    if (!build_wayland_protocols(wayland_protocols_path)) {
        return_defer(false);
    }

    for (size_t i = 0; i < ARRAY_LEN(raylib_modules); ++i) {
        const char *input_path = temp_sprintf(RAYLIB_SRC_FOLDER"%s.c", raylib_modules[i]);
        const char *output_path = temp_sprintf("%s/%s.o", build_path, raylib_modules[i]);
        output_path = temp_sprintf("%s/%s.o", build_path, raylib_modules[i]);

        da_append(&object_files, output_path);

        if (needs_rebuild(output_path, &input_path, 1)) {
            cmd_append(&cmd, "cc",
                "-ggdb", "-DPLATFORM_DESKTOP", "-D_GLFW_X11", "-D_GLFW_WAYLAND", "-fPIC", "-DSUPPORT_FILEFORMAT_FLAC=1",
                "-I"RAYLIB_SRC_FOLDER"external/glfw/include",
                "-I", wayland_protocols_path,
                "-c", input_path,
                "-o", output_path);
            if (!cmd_run(&cmd, .async = &procs)) return_defer(false);
        }
    }

    if (!procs_flush(&procs)) return_defer(false);

    const char *libraylib_path = temp_sprintf("%s/libraylib.a", build_path);
    delete_file(libraylib_path);
    cmd_append(&cmd, "ar", "-crs", libraylib_path);
    for (size_t i = 0; i < ARRAY_LEN(raylib_modules); ++i) {
        const char *input_path = temp_sprintf("%s/%s.o", build_path, raylib_modules[i]);
        cmd_append(&cmd, input_path);
    }
    if (!cmd_run(&cmd)) return_defer(false);

defer:
    cmd_free(cmd);
    da_free(object_files);
    return result;
}

#define WIDTH 64
#define HEIGHT 64
bool generate_no_rom_asm(const char *output_asm_path)
{
    static uint32_t pixels[WIDTH*HEIGHT];
    Olivec_Canvas oc = olivec_canvas(pixels, WIDTH, HEIGHT, WIDTH);
    olivec_fill(oc, 0xFF000000);
    static const char *sign[] = {
        "drop",
        "rom",
        "here",
    };
    size_t sign_width = 0;
    for (size_t i = 0; i < ARRAY_LEN(sign); ++i) {
        size_t width = strlen(sign[i]);
        if (width > sign_width) sign_width = width;
    }
    sign_width *= OLIVEC_DEFAULT_FONT_WIDTH;
    size_t sign_height = (OLIVEC_DEFAULT_FONT_HEIGHT + 1)*ARRAY_LEN(sign);
    size_t sign_x = (WIDTH - sign_width)/2;
    size_t sign_y = (HEIGHT - sign_height)/2;
    for (size_t i = 0; i < ARRAY_LEN(sign); ++i) {
        olivec_text(oc, sign[i], sign_x, sign_y + (OLIVEC_DEFAULT_FONT_HEIGHT + 1)*i, olivec_default_font, 1, 0xFFFFFFFF);
    }

    String_Builder rom_asm = {0};

    sb_appendf(&rom_asm, "    org $%04X\n", ENTRY_POINT);
    sb_appendf(&rom_asm, "init:\n");
    sb_appendf(&rom_asm, "    lda #<update\n");
    sb_appendf(&rom_asm, "    sta $%04X\n", UPDATE_VECTOR);
    sb_appendf(&rom_asm, "    lda #>update\n");
    sb_appendf(&rom_asm, "    sta $%04X\n", UPDATE_VECTOR + 1);
    sb_appendf(&rom_asm, "\n");
    sb_appendf(&rom_asm, "    lda #$FF\n");
    for (size_t y = 0; y < oc.height; ++y) {
        for (size_t x = 0; x < oc.width; ++x) {
            if (OLIVEC_PIXEL(oc, x, y)&0xFF) {
                sb_appendf(&rom_asm, "    sta $%04X\n", CANVAS + y*CANVAS_WIDTH + x);
            }
        }
    }
    sb_appendf(&rom_asm, "update:\n");
    sb_appendf(&rom_asm, "    rts\n");

    if (!write_entire_file(output_asm_path, rom_asm.items, rom_asm.count)) return false;
    nob_log(INFO, "generated %s", output_asm_path);

    return true;
}

bool rom_to_c(const char *input_path, const char *output_path)
{
    String_Builder rom = {0};
    if (!read_entire_file(input_path, &rom)) return false;

    String_Builder out = {0};
    sb_appendf(&out, "uint8_t no_rom[] = {\n");
    for (size_t i = 0; i < rom.count; ) {
        sb_appendf(&out, "    ");
        for (size_t j = 0; i < rom.count && j < 10; ++j, ++i) {
            if (j > 0) sb_append(&out, ' ');
            sb_appendf(&out, "0x%02X,", (uint8_t)rom.items[i]);
        }
        sb_appendf(&out, "\n");
    }
    sb_appendf(&out, "};\n");
    if (!write_entire_file(output_path, out.items, out.count)) return false;
    nob_log(INFO, "generated %s", output_path);
    return true;
}

bool build_rom_with_vasm(Cmd *cmd, const char *input_path, const char *output_path)
{
    cmd_append(cmd, "./vasm6502_oldstyle/linux/vasm6502_oldstyle");
    cmd_append(cmd, input_path);
    cmd_append(cmd, "-Fbin");
    cmd_append(cmd, "-o", output_path);
    return cmd_run(cmd);
}
