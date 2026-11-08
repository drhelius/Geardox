/*
 * Geardox - MS-DOS PC/AT Emulator
 * Copyright (C) 2026  Ignacio Sanchez

 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/
 *
 */

#include <SDL3/SDL_main.h>
#include <stdlib.h>
#if !defined(_WIN32)
#include <signal.h>
#endif
#include "geardox.h"
#include "application.h"
#include "application_headless.h"
#include "config.h"
#include "console_utils.h"

extern bool g_mcp_stdio_mode;
extern bool g_mcp_router_enabled;

struct Machine_Override
{
    bool set;
    int value;
    int saved;
};

static Machine_Override model_override = { false, GDOX_MACHINE_AUTO, GDOX_MACHINE_AUTO };
static Machine_Override cpu_override = { false, GDOX_MACHINE_AUTO, GDOX_MACHINE_AUTO };
static Machine_Override ram_override = { false, GDOX_MACHINE_AUTO, GDOX_MACHINE_AUTO };
static Machine_Override fpu_override = { false, GDOX_MACHINE_AUTO, GDOX_MACHINE_AUTO };
static Machine_Override sound_override = { false, GDOX_SOUND_CARD_SB16, GDOX_SOUND_CARD_SB16 };
static Machine_Override mouse_override = { false, GDOX_MOUSE_PS2, GDOX_MOUSE_PS2 };
static Machine_Override joystick_override = { false, GDOX_JOYSTICK_GAMEPAD, GDOX_JOYSTICK_GAMEPAD };

static bool option_has_value(const char* option);
static const char* option_value(int argc, char* argv[], int* i);
static bool parse_model(const char* text, int* model);
static bool parse_fpu(const char* text, int* fpu);
static bool parse_sound(const char* text, int* card);
static bool parse_mouse(const char* text, int* mouse);
static bool parse_joystick(const char* text, int* joystick);
static bool parse_number(const char* text, const int* values, int count, int* value);
static void apply_override(Machine_Override& override, int* setting);
static void restore_override(const Machine_Override& override, int* setting);
static void apply_machine_overrides(void);
static void restore_machine_settings(void);
static void print_wrapped(const char* label, const std::string& text);
static void print_usage(const char* program);

int main(int argc, char* argv[])
{
#if !defined(_WIN32)
    signal(SIGPIPE, SIG_IGN);
#endif

    attach_parent_console(argc, argv);

    ApplicationParams app_params;
    bool show_usage = false;
    int ret = 0;
    bool mcp_stdio_set = false;
    bool mcp_http_set = false;
    bool headless = false;
    bool portable = false;

    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-' || strcmp(argv[i], "/?") == 0)
        {
            if ((strcmp(argv[i], "-h") == 0) || (strcmp(argv[i], "-?") == 0) ||
                (strcmp(argv[i], "--help") == 0) || (strcmp(argv[i], "/?") == 0))
            {
                show_usage = true;
                ret = 0;
            }
            else if ((strcmp(argv[i], "-v") == 0) || (strcmp(argv[i], "--version") == 0))
            {
                printf("%s\n", GDOX_TITLE_ASCII);
                printf("Build: %s\n", GDOX_VERSION);
                printf("Author: Ignacio Sánchez (drhelius)\n");
                return 0;
            }
            else if ((strcmp(argv[i], "-f") == 0) || (strcmp(argv[i], "--fullscreen") == 0))
            {
                app_params.force_fullscreen = true;
            }
            else if ((strcmp(argv[i], "-w") == 0) || (strcmp(argv[i], "--windowed") == 0))
            {
                app_params.force_windowed = true;
            }
            else if (strcmp(argv[i], "--mcp-stdio") == 0)
            {
                g_mcp_stdio_mode = true;  // Disable logging immediately
                mcp_stdio_set = true;
                app_params.mcp_mode = 0;
            }
            else if (strcmp(argv[i], "--mcp-http") == 0)
            {
                mcp_http_set = true;
                app_params.mcp_mode = 1;
            }
            else if ((strcmp(argv[i], "--mcp-router") == 0) || (strcmp(argv[i], "--mcp-enable-router") == 0))
            {
                g_mcp_router_enabled = true;
            }
            else if (strcmp(argv[i], "--headless") == 0)
            {
                headless = true;
            }
            else if (strcmp(argv[i], "--portable") == 0)
            {
                portable = true;
            }
            else if (strcmp(argv[i], "--cd") == 0)
            {
                if (!(app_params.cd_file = option_value(argc, argv, &i)))
                    return -1;
            }
            else if ((strcmp(argv[i], "--fd1") == 0) || (strcmp(argv[i], "--fd2") == 0))
            {
                int drive = argv[i][4] - '1';

                if (!(app_params.floppy_files[drive] = option_value(argc, argv, &i)))
                    return -1;
            }
            else if (strcmp(argv[i], "--load-state") == 0)
            {
                const char* value = option_value(argc, argv, &i);

                if (!value)
                    return -1;

                if ((strlen(value) == 1) && (value[0] >= '1') && (value[0] <= '5'))
                    app_params.state_slot = value[0] - '0';
                else
                    app_params.state_file = value;
            }
            else if ((strcmp(argv[i], "--model") == 0) || (strcmp(argv[i], "--cpu-mhz") == 0) ||
                (strcmp(argv[i], "--ram") == 0) || (strcmp(argv[i], "--fpu") == 0) ||
                (strcmp(argv[i], "--sound") == 0) || (strcmp(argv[i], "--mouse") == 0) ||
                (strcmp(argv[i], "--joystick") == 0))
            {
                const char* option = argv[i];
                const char* value = option_value(argc, argv, &i);
                Machine_Override* override = &ram_override;
                bool valid = false;

                if (!value)
                    return -1;

                if (strcmp(option, "--model") == 0)
                {
                    override = &model_override;
                    valid = parse_model(value, &override->value);
                }
                else if (strcmp(option, "--cpu-mhz") == 0)
                {
                    override = &cpu_override;
                    valid = parse_number(value, k_machine_cpu_speeds_mhz, GDOX_MACHINE_CPU_SPEED_COUNT, &override->value);
                }
                else if (strcmp(option, "--sound") == 0)
                {
                    override = &sound_override;
                    valid = parse_sound(value, &override->value);
                }
                else if (strcmp(option, "--mouse") == 0)
                {
                    override = &mouse_override;
                    valid = parse_mouse(value, &override->value);
                }
                else if (strcmp(option, "--joystick") == 0)
                {
                    override = &joystick_override;
                    valid = parse_joystick(value, &override->value);
                }
                else if (strcmp(option, "--fpu") == 0)
                {
                    override = &fpu_override;
                    valid = parse_fpu(value, &override->value);
                }
                else
                    valid = parse_number(value, k_machine_ram_sizes_mb, GDOX_MACHINE_RAM_SIZE_COUNT, &override->value);

                override->set = valid;

                if (!valid)
                {
                    fprintf(stderr, "Invalid value for %s: %s\n", option, value);
                    return -1;
                }
            }
            else if (strcmp(argv[i], "--mcp-http-port") == 0)
            {
                if (i + 1 >= argc || argv[i + 1][0] == '-')
                {
                    fprintf(stderr, "Missing value for --mcp-http-port\n");
                    return -1;
                }

                char* end = NULL;
                long port = strtol(argv[++i], &end, 10);

                if (!end || *end != '\0' || port <= 0 || port > 65535)
                {
                    fprintf(stderr, "Invalid port number: %s\n", argv[i]);
                    return -1;
                }

                app_params.mcp_tcp_port = (int)port;
                app_params.mcp_tcp_port_set = true;
            }
            else if (strcmp(argv[i], "--mcp-http-address") == 0)
            {
                if (i + 1 >= argc || argv[i + 1][0] == '-')
                {
                    fprintf(stderr, "Missing value for --mcp-http-address\n");
                    return -1;
                }

                app_params.mcp_http_address = argv[++i];
                app_params.mcp_http_address_set = true;
            }
            else
            {
                printf("Unknown option: %s\n", argv[i]);
                show_usage = true;
                ret = -1;
            }
        }
    }

    int non_option_count = 0;

    for (int i = 1; i < argc; i++)
    {
        if (option_has_value(argv[i]))
        {
            if (i + 1 < argc)
                i++;

            continue;
        }

        if (argv[i][0] != '-' && strcmp(argv[i], "/?") != 0)
        {
            if (non_option_count == 0)
                app_params.rom_file = argv[i];
            else if (non_option_count == 1)
                app_params.symbol_file = argv[i];

            non_option_count++;

            if (non_option_count > 2)
            {
                show_usage = true;
                ret = -1;
                break;
            }
        }
    }

    if (mcp_stdio_set && mcp_http_set)
    {
        printf("Error: Cannot use both --mcp-stdio and --mcp-http at the same time\n");
        return -1;
    }

    if (show_usage)
    {
        print_usage(argv[0]);
        return ret;
    }

    if (app_params.force_fullscreen && app_params.force_windowed)
        app_params.force_fullscreen = false;

    if (!app_params.rom_file && !app_params.cd_file && app_params.floppy_files[0])
    {
        app_params.rom_file = app_params.floppy_files[0];
        app_params.floppy_files[0] = NULL;
    }

    config_init(portable);
    config_read();
    apply_machine_overrides();

    if (app_params.mcp_tcp_port_set)
        config_emulator.mcp_tcp_port = app_params.mcp_tcp_port;
    else
        app_params.mcp_tcp_port = config_emulator.mcp_tcp_port;

    if (app_params.mcp_http_address_set)
        config_emulator.mcp_http_address = app_params.mcp_http_address;
    else
        app_params.mcp_http_address = config_emulator.mcp_http_address;

    if (headless)
    {
        ret = application_headless_init(app_params);

        if (ret == 0)
        {
            application_headless_mainloop();
            application_headless_destroy();
        }

        restore_machine_settings();
        config_write();
        config_destroy();

        return ret;
    }

    const char* forward_file = app_params.rom_file ? app_params.rom_file :
        app_params.cd_file;

    if (!application_check_single_instance(forward_file, app_params.symbol_file))
    {
        config_destroy();
        return 0;
    }

    ret = application_init(app_params);

    if (ret == 0)
        application_mainloop();

    application_destroy();

    restore_machine_settings();
    config_write();
    config_destroy();

    return ret;
}

static bool option_has_value(const char* option)
{
    static const char* const k_options[] = { "--mcp-http-port", "--mcp-http-address", "--cd", "--fd1", "--fd2",
        "--load-state", "--model", "--cpu-mhz", "--ram", "--fpu", "--sound", "--mouse", "--joystick" };

    for (size_t i = 0; i < sizeof(k_options) / sizeof(k_options[0]); i++)
    {
        if (strcmp(option, k_options[i]) == 0)
            return true;
    }

    return false;
}

static const char* option_value(int argc, char* argv[], int* i)
{
    if ((*i + 1 >= argc) || (argv[*i + 1][0] == '-'))
    {
        fprintf(stderr, "Missing value for %s\n", argv[*i]);
        return NULL;
    }

    return argv[++(*i)];
}

static bool parse_model(const char* text, int* model)
{
    if (strcmp(text, "auto") == 0)
    {
        *model = GDOX_MACHINE_AUTO;
        return true;
    }

    for (int i = 0; i < GDOX_MACHINE_RELEASE_ORDER_COUNT; i++)
    {
        GDOX_Machine_Model candidate = k_machine_release_order[i];

        if (k_machine_profiles[candidate].emulated && (strcmp(k_machine_profiles[candidate].key, text) == 0))
        {
            *model = candidate;
            return true;
        }
    }

    return false;
}

static bool parse_mouse(const char* text, int* mouse)
{
    for (int i = 0; i < GDOX_MOUSE_TYPE_COUNT; i++)
    {
        if (strcmp(text, k_mice[i].id) == 0)
        {
            *mouse = i;
            return true;
        }
    }

    return false;
}

static bool parse_joystick(const char* text, int* joystick)
{
    for (int i = 0; i < GDOX_JOYSTICK_TYPE_COUNT; i++)
    {
        if (strcmp(text, k_joysticks[i].id) == 0)
        {
            *joystick = i;
            return true;
        }
    }

    return false;
}

static bool parse_sound(const char* text, int* card)
{
    for (int i = 0; i < GDOX_SOUND_CARD_COUNT; i++)
    {
        if (strcmp(text, k_sound_cards[i].id) == 0)
        {
            *card = i;
            return true;
        }
    }

    return false;
}

static bool parse_number(const char* text, const int* values, int count, int* value)
{
    if (strcmp(text, "auto") == 0)
    {
        *value = GDOX_MACHINE_AUTO;
        return true;
    }

    char* end = NULL;
    long number = strtol(text, &end, 10);

    if (!end || (*end != '\0') || (end == text))
        return false;

    for (int i = 0; i < count; i++)
    {
        if (values[i] == number)
        {
            *value = values[i];
            return true;
        }
    }

    return false;
}

static bool parse_fpu(const char* text, int* fpu)
{
    if (strcmp(text, "auto") == 0)
        *fpu = GDOX_MACHINE_AUTO;
    else if (strcmp(text, "none") == 0)
        *fpu = 0;
    else if (strcmp(text, "80387") == 0)
        *fpu = 1;
    else
        return false;

    return true;
}

static void apply_override(Machine_Override& override, int* setting)
{
    if (!override.set)
        return;

    override.saved = *setting;
    *setting = override.value;
}

static void restore_override(const Machine_Override& override, int* setting)
{
    if (override.set && (*setting == override.value))
        *setting = override.saved;
}

static void apply_machine_overrides(void)
{
    apply_override(model_override, &config_machine.model);
    apply_override(cpu_override, &config_machine.cpu_mhz);
    apply_override(ram_override, &config_machine.ram_mb);
    apply_override(fpu_override, &config_machine.fpu);
    apply_override(sound_override, &config_machine.sound_card);
    apply_override(mouse_override, &config_machine.mouse);
    apply_override(joystick_override, &config_input.joystick_type);
}

static void restore_machine_settings(void)
{
    restore_override(model_override, &config_machine.model);
    restore_override(cpu_override, &config_machine.cpu_mhz);
    restore_override(ram_override, &config_machine.ram_mb);
    restore_override(fpu_override, &config_machine.fpu);
    restore_override(sound_override, &config_machine.sound_card);
    restore_override(mouse_override, &config_machine.mouse);
    restore_override(joystick_override, &config_input.joystick_type);
}

static void print_wrapped(const char* label, const std::string& text)
{
    const size_t k_column = 28;
    const size_t k_width = 79;
    std::string line(label);
    size_t start = 0;

    line.resize(k_column, ' ');

    while (start < text.size())
    {
        size_t end = text.find(' ', start);
        std::string word = text.substr(start, end == std::string::npos ? std::string::npos : end - start);

        if ((line.size() > k_column) && (line.size() + 1 + word.size() > k_width))
        {
            printf("%s\n", line.c_str());
            line.assign(k_column, ' ');
        }

        if (line.size() > k_column)
            line += ' ';

        line += word;
        start = end == std::string::npos ? text.size() : end + 1;
    }

    printf("%s\n", line.c_str());
}

static void print_usage(const char* program)
{
    const char* name = strrchr(program, '/');
    const char* name_windows = strrchr(program, '\\');
    std::string models("auto");
    std::string speeds("auto");
    std::string sizes("auto");
    char number[16];

    if (name_windows > name)
        name = name_windows;

    for (int i = 0; i < GDOX_MACHINE_RELEASE_ORDER_COUNT; i++)
    {
        if (k_machine_profiles[k_machine_release_order[i]].emulated)
            models += std::string(", ") + k_machine_profiles[k_machine_release_order[i]].key;
    }

    for (int i = 0; i < GDOX_MACHINE_CPU_SPEED_COUNT; i++)
    {
        snprintf(number, sizeof(number), ", %d", k_machine_cpu_speeds_mhz[i]);
        speeds += number;
    }

    for (int i = 0; i < GDOX_MACHINE_RAM_SIZE_COUNT; i++)
    {
        snprintf(number, sizeof(number), ", %d", k_machine_ram_sizes_mb[i]);
        sizes += number;
    }

    printf("Usage: %s [options] [media_file] [symbol_file]\n", name ? name + 1 : program);
    printf("\nArguments:\n");
    print_wrapped("  media_file", "CD-ROM (.cue .chd .iso), floppy (.img .ima .vfd .flp .dsk .d77 .d88 .hdm .xdf .mfm), .m3u playlist, "
        ".zip, or hard disk (.hdd .h0-.h3, .img and .hdm over 8 MB, fixed .vhd, hard disk .chd)");
    print_wrapped("  symbol_file", "Symbol file for debugging");
    printf("\nMedia:\n");
    print_wrapped("      --cd FILE", "CD-ROM image or .m3u playlist");
    print_wrapped("      --fd1 FILE", "Floppy image in drive 1");
    print_wrapped("      --fd2 FILE", "Floppy image in drive 2");
    print_wrapped("      --load-state SLOT", "Load save state slot 1-5, or a state file, once the media is loaded");
    printf("\nMachine (for this run only, the saved settings stay):\n");
    print_wrapped("      --model MODEL", models);
    print_wrapped("      --cpu-mhz MHZ", speeds);
    print_wrapped("      --ram MB", sizes);
    print_wrapped("      --fpu FPU", "auto, none, 80387");
    print_wrapped("      --sound CARD", "none, adlib, sb15, sb20, sbpro, sbpro2, sb16");
    print_wrapped("      --mouse MOUSE", "none, ps2, serial");
    print_wrapped("      --joystick DEVICE", "none, joystick, gamepad, flightstick");
    printf("\nOptions:\n");
    print_wrapped("  -f, --fullscreen", "Start in fullscreen mode");
    print_wrapped("  -w, --windowed", "Start in windowed mode with menu visible");
    print_wrapped("      --mcp-stdio", "Auto-start MCP server with stdio transport");
    print_wrapped("      --mcp-http", "Auto-start MCP server with HTTP transport");
    print_wrapped("      --mcp-router", "Enable compact MCP tool routing");
    print_wrapped("      --mcp-http-address A", "HTTP bind address (default: 127.0.0.1)");
    print_wrapped("      --mcp-http-port N", "HTTP port for MCP server (default: 7777)");
    print_wrapped("      --headless", "Run without GUI (requires MCP)");
    print_wrapped("      --portable", "Store configuration and user data beside the application");
    print_wrapped("  -v, --version", "Display version information");
    print_wrapped("  -h, --help", "Display this help message");
}
