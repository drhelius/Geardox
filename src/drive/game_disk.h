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

struct GameDisk_Found
{
    std::string path;
    std::string inner;
};

struct GameDisk_Result
{
    bool done;
    bool refused;
    u32 written;
    u32 deleted;
    u32 directories;
    u32 conflicts;
    u32 kept;
    u32 startup;
    std::vector<std::string> notes;
};

struct GameDisk_Manifest_System
{
    u8 system;
    std::vector<std::string> reserved;
    std::string config_sys;
    std::string autoexec;
};

struct GameDisk_Manifest_Entry
{
    std::string dos_path;
    std::string host_path;
    bool directory;
    bool system;
    bool overlay;
    u32 size;
    u32 crc;
    u16 date;
    u16 time;
    u32 first_cluster;
    bool host_exists;
    u64 host_size;
    s64 host_modified;
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
    bool Open(const GameDisk_Found& found, const char* save_directory, const char* overrides_directory, bool write_to_folder,
        const char* msdos_path);
    void Close();
    bool IsOpen() const;
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
    bool Finish();
    bool RetryFinish();
    bool IsSessionOpen() const;
    std::string GetConfigSys(const GDOX_Machine_Config& machine) const;
    std::string GetBatch(const GDOX_Machine_Config& machine) const;
    const char* GetName() const;
    const char* GetDescriptorPath() const;
    const char* GetImagePath() const;
    const char* GetSourcePath() const;
    const char* GetOverlayPath() const;
    const char* GetOverridesPath() const;
    bool IsWritingToOverlay() const;
    bool WasBuilt() const;
    bool WasRecovered() const;
    const char* GetRefusedPath() const;
    bool IsFolderReadOnly() const;
    u32 GetLeftOut() const;
    const std::vector<std::string>& GetFloppies(int drive) const;
    const std::vector<std::string>& GetCdRoms() const;
    const char* GetHardDisk(int id) const;
    const GameDisk_Result& GetLastWriteBack() const;
    const char* GetError() const;
    bool IsFromDatabase() const;
    bool HasReleaseSettings() const;
    bool HasWithheldSettings() const;

private:
    bool OpenFromDatabase(const GameDisk_Found& found);
    bool BuildFolderCd(const std::string& folder, const std::string& label, std::string* path);
    bool ExtractEntries(mz_zip_archive* zip, const std::string& wanted, const std::string& folder, bool cue,
        std::string* path, bool* found);
    bool Fail(const char* format, ...);
    bool IsSystemPath(const std::string& dos_path) const;
    bool HasCDExtension() const;
    bool LoadSystem();
    bool IsGamePath(const std::string& dos_path) const;
    bool Assemble(FatImage* image, const GDOX_Machine_Config& machine, GameDisk_Manifest_System* system);
    bool ResolveMedia(const std::string& value, std::string* path);
    bool ExtractFromZip(const std::string& inner, std::string* path);
    bool LoadManifest(std::vector<GameDisk_Manifest_Entry>* entries, bool* open, u32* fingerprint, u8* mode,
        GameDisk_Manifest_System* system = NULL) const;
    bool SaveManifest(const std::vector<GameDisk_Manifest_Entry>& entries, bool open, u32 fingerprint);
    bool SetManifestOpen(bool open);
    bool WriteBack(GameDisk_Result* result);
    void NoteStartupFile(FatImage* image, const FatImage_File* file, const GameDisk_Manifest_Entry& entry, GameDisk_Result* result);
    bool TakeLock();
    void ReleaseLock();
    void ListBuilt(FatImage* image, std::vector<GameDisk_Manifest_Entry>* entries);
    std::string GetDestination(const std::string& host_path) const;
    bool WriteConflict(FatImage* image, const FatImage_File& file, const std::string& host_path);
    void LoadDeleted();
    bool SaveDeleted();
    bool IsDeleted(const std::string& host_path) const;
    void SetDeleted(const std::string& host_path, bool deleted);

private:
    bool m_open;
    GameDescriptor m_base;
    GameDescriptor m_overrides;
    GameDescriptor m_descriptor;
    std::string m_descriptor_path;
    std::string m_base_directory;
    std::string m_save_directory;
    std::string m_overrides_path;
    std::string m_source;
    bool m_source_zip;
    std::string m_zip_prefix;
    std::vector<FatImage_Rule> m_layout;
    std::vector<GameDatabase_Patch> m_patches;
    bool m_database;
    bool m_release_settings;
    bool m_withheld_settings;
    std::string m_overlay;
    std::string m_image;
    std::string m_manifest;
    std::string m_lock;
    std::string m_media_directory;
    std::string m_msdos_path;
    std::string m_keyboard;
    DosImport_System m_msdos;
    bool m_msdos_loaded;
    bool m_write_overlay;
    bool m_folder_read_only;
    bool m_built;
    bool m_recovered;
    std::string m_refused;
    bool m_locked;
    bool m_has_machine;
    GDOX_Machine_Config m_machine;
    u32 m_left_out;
    std::vector<std::string> m_floppies[2];
    std::vector<std::string> m_cdroms;
    std::string m_hard_disks[GDOX_MAX_HARD_DISKS];
    std::vector<std::string> m_deleted;
    GameDisk_Manifest_System m_system;
    GameDisk_Result m_last;
    std::string m_error;
};

static const u32 k_game_disk_manifest_magic = 0x4D584447;
static const u32 k_game_disk_manifest_version = 1;
static const u32 k_game_disk_builder_version = 1;

#endif /* GAME_DISK_H */
