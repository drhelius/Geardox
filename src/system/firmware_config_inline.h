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


#ifndef FIRMWARE_CONFIG_INLINE_H
#define FIRMWARE_CONFIG_INLINE_H

#include "firmware_config.h"

INLINE void FirmwareConfig::SetEnabled(bool enabled)
{
    m_enabled = enabled;
}

INLINE bool FirmwareConfig::IsEnabled() const
{
    return m_enabled;
}

INLINE bool FirmwareConfig::Decodes(u16 port) const
{
    return m_enabled && (port == k_firmware_config_selector_port || port == k_firmware_config_data_port);
}

INLINE FirmwareConfig::FirmwareConfig_State* FirmwareConfig::GetState()
{
    return &m_state;
}

#endif /* FIRMWARE_CONFIG_INLINE_H */
