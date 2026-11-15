/*
 * The files of the FreeDOS disk and its boot sectors, built for Geardox by build.sh
 * FreeDOS: https://www.freedos.org, see COPYING and README.md
 */

#ifndef FREEDOS_H
#define FREEDOS_H

#include <stdint.h>

extern const char* const k_freedos_version;
extern const uint8_t k_freedos_files[];
extern const uint32_t k_freedos_files_size;
extern const uint8_t k_freedos_boot[];
extern const uint32_t k_freedos_boot_size;
extern const uint8_t k_freedos_mbr[];
extern const uint32_t k_freedos_mbr_size;

#endif /* FREEDOS_H */
