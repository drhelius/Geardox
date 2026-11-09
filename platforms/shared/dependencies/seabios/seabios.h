/*
 * SeaBIOS and SeaVGABIOS images built for Geardox by build.sh
 * SeaBIOS: https://www.seabios.org, LGPLv3, see COPYING.LESSER
 */

#ifndef SEABIOS_H
#define SEABIOS_H

#include <stdint.h>

extern const char* const k_seabios_version;
extern const uint8_t k_seabios_bios[];
extern const uint32_t k_seabios_bios_size;
extern const uint8_t k_seabios_vgabios[];
extern const uint32_t k_seabios_vgabios_size;
extern const uint8_t k_seabios_vgabios_cirrus[];
extern const uint32_t k_seabios_vgabios_cirrus_size;

#endif /* SEABIOS_H */
