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

INLINE void GeardoxCore::Pause(bool paused)
{
    m_paused = paused;
}

INLINE bool GeardoxCore::IsPaused()
{
    return m_paused;
}

INLINE bool GeardoxCore::IsPoweredOn()
{
    return m_powered;
}

INLINE const GDOX_Machine_Settings& GeardoxCore::GetMachineSettings()
{
    return m_machine_settings;
}

INLINE const GDOX_Machine_Config& GeardoxCore::GetMachineConfig()
{
    return m_machine_config;
}

INLINE const GDOX_Machine_Config& GeardoxCore::GetPendingMachineConfig()
{
    return m_pending_machine_config;
}

INLINE const GDOX_Machine_Report& GeardoxCore::GetMachineReport()
{
    return m_machine_report;
}

INLINE const GDOX_Machine_Report& GeardoxCore::GetPendingMachineReport()
{
    return m_pending_machine_report;
}

INLINE bool GeardoxCore::HasFirmware(GDOX_Machine_Model model)
{
    return m_firmware_sources[model].type != GDOX_FIRMWARE_SOURCE_NONE;
}

INLINE const GDOX_Firmware_Source& GeardoxCore::GetFirmwareSource(GDOX_Machine_Model model)
{
    return m_firmware_sources[model];
}

INLINE Firmware* GeardoxCore::GetFirmware()
{
    return m_firmware;
}

INLINE Media* GeardoxCore::GetMedia()
{
    return m_media;
}

INLINE Audio* GeardoxCore::GetAudio()
{
    return m_audio;
}

INLINE Input* GeardoxCore::GetInput()
{
    return m_input;
}

INLINE Memory* GeardoxCore::GetMemory()
{
    return m_memory;
}

INLINE I386* GeardoxCore::GetI386()
{
    return m_i386;
}

INLINE X87* GeardoxCore::GetX87()
{
    return m_x87;
}

INLINE IO* GeardoxCore::GetIO()
{
    return m_io;
}

INLINE PIC* GeardoxCore::GetPIC()
{
    return m_pic;
}

INLINE PIT* GeardoxCore::GetPIT()
{
    return m_pit;
}

INLINE Scheduler* GeardoxCore::GetScheduler()
{
    return m_scheduler;
}

INLINE Video* GeardoxCore::GetVideo()
{
    return m_video;
}

INLINE CdRomMedia* GeardoxCore::GetCDROMMedia()
{
    return m_cdrom_media;
}

INLINE CdRomAudio* GeardoxCore::GetCDROMAudio()
{
    return m_cdrom_audio;
}

INLINE FDC* GeardoxCore::GetFDC()
{
    return m_fdc;
}

INLINE TraceLogger* GeardoxCore::GetTraceLogger()
{
    return m_trace_logger;
}

INLINE Profiler* GeardoxCore::GetProfiler()
{
    return m_profiler;
}
