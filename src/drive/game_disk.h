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

#ifndef GAME_DISK_H
#define GAME_DISK_H

#include <string>
#include <vector>
#include "../common/common.h"
#include "../media/game_database.h"
#include "../media/game_descriptor.h"
#include "dos_import.h"
#include "fat_image.h"
#include "folder_disk.h"
#include "gus_import.h"

struct GameDisk_Found
{
    std::string path;
    std::string inner;
    bool matched;
    GameDatabase_Match match;
};

enum GameDisk_Message_Kind
{
    GAME_DISK_MESSAGE_RECOVERED = 0,
    GAME_DISK_MESSAGE_READ_ONLY,
    GAME_DISK_MESSAGE_RECOGNISED,
    GAME_DISK_MESSAGE_LEFT_OUT,
    GAME_DISK_MESSAGE_REFUSED,
    GAME_DISK_MESSAGE_STARTUP,
    GAME_DISK_MESSAGE_VOODOO,
    GAME_DISK_MESSAGE_VOODOO_DRIVER
};

struct GameDisk_Message
{
    GameDisk_Message_Kind kind;
    std::string title;
    std::string detail;
};

class GameDisk
{
public:
    GameDisk();
    ~GameDisk();
    static bool Find(const char* path, std::vector<GameDisk_Found>* found);
    static bool IsGameContent(const char* path);
    static bool IsDosProgram(const char* path);
    static void SetDatabaseEnabled(bool enabled);
    static bool IsDatabaseEnabled();
    static std::string GetFoundName(const GameDisk_Found& found);
    static std::string GetDosFolderName(const std::string& name);
    bool Open(const GameDisk_Found& found, const char* save_directory, const char* temp_directory, bool settings,
        bool write_to_folder, const char* msdos_path, const char* ultrasnd_path, const char* glide_path = NULL);
    void Close();
    bool IsOpen() const;
    bool IsImage() const;
    bool SaveOverrides(const GameDescriptor& overrides);
    bool ClearOverrides();
    const GameDescriptor& GetDescriptor() const;
    const GameDescriptor& GetBase() const;
    const GameDescriptor& GetOverrides() const;
    void GetGameSettings(const GDOX_Machine_Settings& user, GDOX_Machine_Settings* game) const;
    static void GetGameSettings(const GameDescriptor& descriptor, const GDOX_Machine_Settings& user,
        GDOX_Machine_Settings* game, int system);
    int GetSystem() const;
    void SetSystem(int system);
    void SetKeyboardLayout(const char* layout);
    const char* GetKeyboardLayout() const;
    bool HasMSDOS() const;
    const char* GetSystemName() const;
    bool Prepare(const GDOX_Machine_Config& machine);
    FolderDisk* TakeDisk();
    FolderDisk* TakeHardDisk(int id);
    std::string GetConfigSys(const GDOX_Machine_Config& machine) const;
    std::string GetBatch(const GDOX_Machine_Config& machine) const;
    const char* GetName() const;
    const char* GetDescriptorPath() const;
    const char* GetSourcePath() const;
    const char* GetRootPath() const;
    std::string GetSourceSetting() const;
    const char* GetFolder() const;
    std::string GetSavesPath() const;
    std::string GetWorkingPath() const;
    const char* GetSettingsPath() const;
    std::string GetKeysPath() const;
    bool IsWritingToSaves() const;
    bool IsFolderReadOnly() const;
    bool HasStartupEdits() const;
    const FolderDisk_Result& GetRecovery() const;
    u32 GetLeftOut() const;
    const std::vector<std::string>& GetFloppies(int drive) const;
    const std::vector<std::string>& GetCdRoms() const;
    const char* GetCdRomLabel() const;
    const char* GetHardDisk(int id) const;
    const GDOX_Machine_Config& GetMachine() const;
    void GetMessages(std::vector<GameDisk_Message>* messages) const;
    const char* GetError() const;
    bool IsFromDatabase() const;
    bool HasReleaseSettings() const;
    bool HasWithheldSettings() const;
    void SetProgram(int program);
    int GetProgram() const;
    bool IsVoodooChosen() const;
    bool HasVoodooProgram() const;
    bool VoodooProgramNeedsDriver() const;
    bool UsesVoodooExe() const;
    bool NeedsRestart(const GDOX_Machine_Config& machine) const;

private:
    static bool FindGames(const std::string& source, bool prompt, std::vector<GameDisk_Found>* found);
    bool OpenFromDatabase(const GameDisk_Found& found);
    bool Fail(const char* format, ...);
    bool HasCDExtension() const;
    bool LoadSystem();
    bool LoadUltraSound();
    bool LoadGlide();
    bool IsVoodooOffered() const;
    bool UseVoodooExe(FatImage* image, const GDOX_Machine_Config& machine);
    std::string GetDosFolder() const;
    std::string GetStartupFile(const char* name, const std::string& base);
    bool BuildTree(const GDOX_Machine_Config& machine, FatImage* image, std::vector<FolderDisk_Layer>* layers);
    bool ResolveMedia(const std::string& value, std::string* path);
    void Discard();

private:
    bool m_open;
    GameDescriptor m_base;
    GameDescriptor m_overrides;
    GameDescriptor m_descriptor;
    std::string m_descriptor_path;
    std::string m_base_directory;
    std::string m_save_directory;
    std::string m_temp_directory;
    std::string m_folder;
    std::string m_overrides_path;
    std::string m_source;
    bool m_source_zip;
    bool m_source_image;
    std::string m_zip_prefix;
    std::string m_extracted;
    std::string m_root;
    std::vector<FatImage_Rule> m_layout;
    std::vector<GameDatabase_Patch> m_patches;
    bool m_database;
    bool m_release_settings;
    bool m_withheld_settings;
    std::string m_msdos_path;
    std::string m_ultrasnd_path;
    std::string m_keyboard;
    DosImport_System m_msdos;
    bool m_msdos_loaded;
    GusImport_Software m_ultrasnd;
    bool m_ultrasnd_loaded;
    std::string m_glide_path;
    std::string m_glide;
    std::string m_glide_name;
    bool m_glide_loaded;
    bool m_voodoo_release;
    int m_program;
    int m_built_program;
    bool m_voodoo_program;
    bool m_voodoo_driver;
    bool m_voodoo_exe;
    GameDisk_Message m_voodoo_note;
    bool m_write_saves;
    bool m_folder_read_only;
    bool m_startup_edits;
    bool m_has_machine;
    GDOX_Machine_Config m_machine;
    std::string m_config_base;
    std::string m_autoexec_base;
    std::vector<std::string> m_merged;
    FolderDisk* m_disk;
    FolderDisk* m_hard_disk_sources[GDOX_MAX_HARD_DISKS];
    FolderDisk_Result m_recovery;
    u32 m_left_out;
    std::vector<std::string> m_floppies[2];
    std::vector<std::string> m_cdroms;
    std::string m_hard_disks[GDOX_MAX_HARD_DISKS];
    std::string m_error;
};

#endif /* GAME_DISK_H */
