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

#define GUI_DEBUG_IDE_IMPORT
#include "gui_debug_ide.h"

#include "imgui.h"
#include "geardox.h"
#include "drive/ide_controller.h"
#include "drive/ide_cdrom.h"
#include "../config.h"
#include "../emu.h"
#include "../gui.h"
#include "../gui_colors.h"
#include "gui_debug_constants.h"

static const char* const k_status_bits[8] = { "ERR", NULL, NULL, "DRQ", "DSC", NULL, "DRDY", "BSY" };
static const char* const k_error_bits[8] = { NULL, NULL, "ABRT", NULL, "IDNF", NULL, "UNC", NULL };
static const char* const k_control_bits[8] = { NULL, "nIEN", "SRST", NULL, NULL, NULL, NULL, NULL };
static const char* const k_atapi_error_bits[8] = { "ILI", "EOM", "ABRT", "MCR", NULL, NULL, NULL, NULL };
static const char* const k_atapi_reason_bits[8] = { "COD", "IO", NULL, NULL, NULL, NULL, NULL, NULL };

static void setup_ide_columns(void);
static void draw_ide_channel(int channel);
static void draw_ide_label(const char* label);
static void draw_ide_register(const char* label, u8 value, bool present);
static void draw_ide_port(const char* port);
static void draw_ide_detail(void);
static void draw_ide_bits(u8 value, const char* const* names, bool present);
static void draw_ide_disk_row(int id);
static void draw_ide_cdrom_registers(IdeCdRom* cdrom, u16 base, u8 control, bool present, bool attached);

void gui_debug_window_ide(void)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::SetNextWindowPos(ImVec2(289, 243), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(295, 620), ImGuiCond_FirstUseEver);
    ImGui::Begin("IDE", &config_debug.show_ide);

    ImGui::PushFont(gui_default_font);

    draw_ide_channel(0);
    ImGui::NewLine();
    draw_ide_channel(1);

    ImGui::PopFont();

    ImGui::End();
    ImGui::PopStyleVar();
}

void gui_debug_window_ide_disks(void)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::SetNextWindowPos(ImVec2(130, 110), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(520, 130), ImGuiCond_FirstUseEver);
    ImGui::Begin("IDE Disks", &config_debug.show_ide_disks);

    ImGui::PushFont(gui_default_font);

    ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit |
        ImGuiTableFlags_NoHostExtendX;

    if (ImGui::BeginTable("##ide_disks", 8, flags))
    {
        float character = ImGui::CalcTextSize("0").x;

        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, character * 2);
        ImGui::TableSetupColumn("IMAGE", ImGuiTableColumnFlags_WidthFixed, character * 16);
        ImGui::TableSetupColumn("BLOCKS", ImGuiTableColumnFlags_WidthFixed, character * 9);
        ImGui::TableSetupColumn("WP", ImGuiTableColumnFlags_WidthFixed, character * 3);
        ImGui::TableSetupColumn("GEOMETRY", ImGuiTableColumnFlags_WidthFixed, character * 11);
        ImGui::TableSetupColumn("BIOS", ImGuiTableColumnFlags_WidthFixed, character * 4);
        ImGui::TableSetupColumn("MULTI", ImGuiTableColumnFlags_WidthFixed, character * 5);
        ImGui::TableSetupColumn("HEAD", ImGuiTableColumnFlags_WidthFixed, character * 5);
        ImGui::TableHeadersRow();

        for (int id = 0; id < IDE_DISKS; id++)
            draw_ide_disk_row(id);

        ImGui::EndTable();
    }

    ImGui::PopFont();

    ImGui::End();
    ImGui::PopStyleVar();
}

static void setup_ide_columns(void)
{
    float character = ImGui::CalcTextSize("0").x;

    ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, character * 7);
    ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, character * 24);
    ImGui::TableSetupColumn(NULL, ImGuiTableColumnFlags_WidthFixed, character * 6);
}

static void draw_ide_channel(int channel)
{
    IdeController* ide = emu_get_core()->GetIDE();
    IdeController::IdeController_State* state = ide->GetState();
    int selected = (channel * IDE_CHANNEL_DISKS) + state->selected[channel];
    IdeDisk* disk = ide->GetDisk(selected);
    IdeDisk::IdeDisk_State* registers = disk->GetState();
    bool present = ide->Decodes(channel == 0 ? 0x01F0 : 0x0170);
    bool is_cdrom = selected == ide->GetCdRomSlot();
    bool attached = is_cdrom ? ide->GetCdRom()->IsAttached() : disk->IsAttached();
    u8 status = is_cdrom ? ide->GetCdRom()->GetStatus() : registers->status;
    float space = ImGui::CalcTextSize(" ").x;
    ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX;
    u16 base = channel == 0 ? 0x01F0 : 0x0170;
    char port[8];

    ImGui::TextColored(cyan, "%s", channel == 0 ? "PRIMARY CHANNEL" : "SECONDARY CHANNEL"); ImGui::Separator();

    ImGui::PushID(channel);

    if (ImGui::BeginTable("##ide_registers", 3, flags))
    {
        setup_ide_columns();

        draw_ide_label("DISK");

        if (attached)
            ImGui::TextColored(orange, "ID %d  %s%s", selected, k_debug_ide_slot_names[selected], is_cdrom ? " (CD-ROM)" : "");
        else
            ImGui::TextColored(gray, "--");

        if (is_cdrom)
            draw_ide_cdrom_registers(ide->GetCdRom(), base, state->control[channel], present, attached);
        else
        {
            draw_ide_register("STATUS", registers->status, attached);
            snprintf(port, sizeof(port), "%04X", base + 7);
            draw_ide_port(port);
            draw_ide_detail();
            draw_ide_bits(registers->status, k_status_bits, attached);

            draw_ide_register("ERROR", registers->error, attached);
            snprintf(port, sizeof(port), "%04X", base + 1);
            draw_ide_port(port);
            draw_ide_detail();
            draw_ide_bits(registers->error, k_error_bits, attached);

            draw_ide_register("CONTROL", state->control[channel], present);
            snprintf(port, sizeof(port), "%04X", base + 0x206);
            draw_ide_port(port);
            draw_ide_detail();
            draw_ide_bits(state->control[channel], k_control_bits, present);

            draw_ide_register("COUNT", registers->sector_count, attached);
            snprintf(port, sizeof(port), "%04X", base + 2);
            draw_ide_port(port);

            draw_ide_register("SECTOR", registers->sector_number, attached);
            snprintf(port, sizeof(port), "%04X", base + 3);
            draw_ide_port(port);

            draw_ide_label("CYL");

            if (attached)
                ImGui::TextColored(white, "$%02X%02X", registers->cylinder_high, registers->cylinder_low);
            else
                ImGui::TextColored(gray, "--");

            snprintf(port, sizeof(port), "%04X", base + 4);
            draw_ide_port(port);

            draw_ide_register("HEAD", registers->device_head, attached);
            snprintf(port, sizeof(port), "%04X", base + 6);
            draw_ide_port(port);

            draw_ide_label("COMMAND");

            if (attached)
                ImGui::TextColored(white, "$%02X %s", registers->command, gui_debug_ide_command_name(registers->command));
            else
                ImGui::TextColored(gray, "--");

            draw_ide_label("PHASE");

            if (attached)
                ImGui::TextColored(blue, "%s", k_debug_ide_phase_names[registers->phase % IDE_PHASE_COUNT]);
            else
                ImGui::TextColored(gray, "--");

            draw_ide_label("DATA");

            if (attached)
                ImGui::TextColored(white, "%u / %u BYTES", registers->position, registers->length);
            else
                ImGui::TextColored(gray, "--");

            draw_ide_label("LEFT");

            if (attached)
                ImGui::TextColored(white, "%u SECTORS", registers->remaining);
            else
                ImGui::TextColored(gray, "--");
        }

        ImGui::EndTable();
    }

    ImGui::PopID();

    ImGui::TextColored(attached && (status & k_ide_status_drq) != 0 ? green : gray, "DRQ");
    ImGui::SameLine(0.0f, space);
    ImGui::TextColored(ide->IsInterruptOut(channel) ? yellow : gray, "IRQ%d", k_ide_irq[channel]);
}

static void draw_ide_cdrom_registers(IdeCdRom* cdrom, u16 base, u8 control, bool present, bool attached)
{
    IdeCdRom::IdeCdRom_State* registers = cdrom->GetState();
    float space = ImGui::CalcTextSize(" ").x;
    char port[8];

    draw_ide_register("STATUS", registers->status, attached);
    snprintf(port, sizeof(port), "%04X", base + 7);
    draw_ide_port(port);
    draw_ide_detail();
    draw_ide_bits(registers->status, k_status_bits, attached);

    draw_ide_register("ERROR", registers->error, attached);
    snprintf(port, sizeof(port), "%04X", base + 1);
    draw_ide_port(port);
    draw_ide_detail();
    draw_ide_bits(registers->error, k_atapi_error_bits, attached);
    ImGui::SameLine(0.0f, space);
    ImGui::TextColored(registers->error >> 4 != 0 ? orange : gray, "KEY %X", registers->error >> 4);

    draw_ide_register("CONTROL", control, present);
    snprintf(port, sizeof(port), "%04X", base + 0x206);
    draw_ide_port(port);
    draw_ide_detail();
    draw_ide_bits(control, k_control_bits, present);

    draw_ide_register("REASON", registers->reason, attached);
    snprintf(port, sizeof(port), "%04X", base + 2);
    draw_ide_port(port);
    draw_ide_detail();
    draw_ide_bits(registers->reason, k_atapi_reason_bits, attached);

    draw_ide_register("SECTOR", registers->sector_number, attached);
    snprintf(port, sizeof(port), "%04X", base + 3);
    draw_ide_port(port);

    draw_ide_label("BYTES");
    ImGui::TextColored(white, "$%02X%02X", registers->byte_count_high, registers->byte_count_low);
    snprintf(port, sizeof(port), "%04X", base + 4);
    draw_ide_port(port);

    draw_ide_register("HEAD", registers->device_head, attached);
    snprintf(port, sizeof(port), "%04X", base + 6);
    draw_ide_port(port);

    draw_ide_label("COMMAND");
    ImGui::TextColored(white, "$%02X %s", registers->command, GetCdRomAtaName(registers->command));

    draw_ide_label("PACKET");
    ImGui::TextColored(white, "$%02X %s", registers->packet[0], GetCdRomPacketName(registers->packet[0]));

    draw_ide_label("PHASE");
    ImGui::TextColored(blue, "%s", k_debug_cdrom_phase_names[registers->phase % IDE_CDROM_PHASE_COUNT]);

    draw_ide_label("DATA");
    ImGui::TextColored(white, "%u / %u BYTES", registers->position, registers->length);

    draw_ide_label("LEFT");
    ImGui::TextColored(white, "%u SECTORS", registers->remaining);
}

static void draw_ide_label(const char* label)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextColored(violet, "%s", label);
    ImGui::TableNextColumn();
}

static void draw_ide_register(const char* label, u8 value, bool present)
{
    draw_ide_label(label);

    if (present)
        ImGui::TextColored(white, "$%02X", value);
    else
        ImGui::TextColored(gray, "--");
}

static void draw_ide_port(const char* port)
{
    ImGui::TableNextColumn();
    ImGui::TextColored(gray, "(%s)", port);
}

static void draw_ide_detail(void)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TableNextColumn();
}

static void draw_ide_bits(u8 value, const char* const* names, bool present)
{
    float space = ImGui::CalcTextSize(" ").x;
    bool first = true;

    for (int bit = 7; bit >= 0; bit--)
    {
        if (!IsValidPointer(names[bit]))
            continue;

        if (!first)
            ImGui::SameLine(0.0f, space);

        ImGui::TextColored(present && IS_SET_BIT(value, bit) ? green : gray, "%s", names[bit]);
        first = false;
    }
}

static void draw_ide_disk_row(int id)
{
    IdeDisk* disk = emu_get_core()->GetIDE()->GetDisk(id);
    IdeDisk::IdeDisk_State* state = disk->GetState();
    HardDiskImage* image = disk->GetImage();
    bool attached = disk->IsAttached();

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextColored(orange, "%d", id);

    ImGui::TableNextColumn();

    if (attached)
    {
        ImGui::TextColored(white, "%.16s", image->GetFileName());

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", image->GetPath());
    }
    else
        ImGui::TextColored(gray, "--");

    ImGui::TableNextColumn();

    if (attached)
        ImGui::TextColored(white, "%u", image->GetBlocks());
    else
        ImGui::TextColored(gray, "--");

    ImGui::TableNextColumn();

    if (attached)
        ImGui::TextColored(image->IsWriteProtected() ? orange : gray, "%s", image->IsWriteProtected() ? "YES" : "NO");
    else
        ImGui::TextColored(gray, "--");

    ImGui::TableNextColumn();

    if (attached)
        ImGui::TextColored(white, "%u/%u/%u", state->cylinders, state->heads, state->sectors);
    else
        ImGui::TextColored(gray, "--");

    ImGui::TableNextColumn();

    if (attached)
        ImGui::TextColored(brown, "%s", disk->GetTranslation() != 0 ? "LBA" : "NONE");
    else
        ImGui::TextColored(gray, "--");

    ImGui::TableNextColumn();

    if (attached)
        ImGui::TextColored(state->multiple != 0 ? white : gray, "%u", state->multiple);
    else
        ImGui::TextColored(gray, "--");

    ImGui::TableNextColumn();

    if (attached)
        ImGui::TextColored(white, "%u", state->head_cylinder);
    else
        ImGui::TextColored(gray, "--");
}
