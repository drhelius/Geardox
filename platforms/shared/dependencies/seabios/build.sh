#!/bin/bash

# Builds SeaBIOS and SeaVGABIOS from the pinned commit plus geardox.patch, and writes seabios.cpp with the images
# Needs an i386 capable GCC and binutils: i686-elf-gcc on macOS, the system GCC on x86 Linux (CROSS_PREFIX="")

set -e

SEABIOS_REPO=${SEABIOS_REPO:-https://git.seabios.org/seabios.git}
SEABIOS_COMMIT=28aa46349b97ef0ebf742ae4fb9f9fad03578670
CROSS_PREFIX=${CROSS_PREFIX-i686-elf-}
PYTHON=${PYTHON:-python3}

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
WORK_DIR=$(mktemp -d)
trap 'rm -rf "$WORK_DIR"' EXIT

git clone -q "$SEABIOS_REPO" "$WORK_DIR/seabios"
cd "$WORK_DIR/seabios"
git checkout -q "$SEABIOS_COMMIT"

# Without .git SeaBIOS takes its version from .version, so the patched tree keeps the commit's version on every build
git describe --tags --long > .version
rm -rf .git
patch -s -p1 < "$SCRIPT_DIR/geardox.patch"

build_rom()
{
    local options=(CROSS_PREFIX="$CROSS_PREFIX" CPP="${CROSS_PREFIX}cpp" PYTHON="$PYTHON")
    make "${options[@]}" distclean > /dev/null
    cp "$SCRIPT_DIR/$1" .config
    make "${options[@]}" olddefconfig > /dev/null
    make "${options[@]}" EXTRAVERSION=-geardox -j4
    cp "out/$2" "$WORK_DIR/${3:-$2}"
    VERSION=$(sed -n 's/^#define BUILD_VERSION "\(.*\)"$/\1/p' out/autoversion.h)
}

build_rom bios.config bios.bin
build_rom bios_pci.config bios.bin bios_pci.bin
build_rom vgabios.config vgabios.bin
build_rom vgabios_cirrus.config vgabios.bin vgabios_cirrus.bin
build_rom vgabios_ega.config vgabios.bin vgabios_ega.bin
build_rom vgabios_cga.config vgabios.bin vgabios_cga.bin

"$PYTHON" - "$SCRIPT_DIR/seabios.cpp" "$VERSION" "k_seabios_bios=$WORK_DIR/bios.bin" \
    "k_seabios_bios_pci=$WORK_DIR/bios_pci.bin" "k_seabios_vgabios=$WORK_DIR/vgabios.bin" \
    "k_seabios_vgabios_cirrus=$WORK_DIR/vgabios_cirrus.bin" \
    "k_seabios_vgabios_ega=$WORK_DIR/vgabios_ega.bin" \
    "k_seabios_vgabios_cga=$WORK_DIR/vgabios_cga.bin" << 'PYTHON_EOF'
import sys

header = """/*
 * SeaBIOS and SeaVGABIOS images built for Geardox by build.sh, do not edit
 * SeaBIOS: https://www.seabios.org, LGPLv3, see COPYING.LESSER
 */

#include "seabios.h"
"""

def array(name, data):
    lines = ["const uint8_t %s[] =" % name, "{"]
    for i in range(0, len(data), 16):
        chunk = ", ".join("0x%02X" % b for b in data[i:i + 16])
        lines.append("    " + chunk + ("," if i + 16 < len(data) else ""))
    lines.append("};")
    lines.append("")
    lines.append("const uint32_t %s_size = %d;" % (name, len(data)))
    return "\n".join(lines)

with open(sys.argv[1], "w") as out:
    out.write(header)
    out.write('\nconst char* const k_seabios_version = "%s";\n' % sys.argv[2])
    for image in sys.argv[3:]:
        name, path = image.split("=", 1)
        out.write("\n" + array(name, open(path, "rb").read()) + "\n")
PYTHON_EOF

echo "Built SeaBIOS $VERSION: bios.bin $(wc -c < "$WORK_DIR/bios.bin") bytes, bios_pci.bin $(wc -c < "$WORK_DIR/bios_pci.bin") bytes," \
    "vgabios.bin $(wc -c < "$WORK_DIR/vgabios.bin") bytes," \
    "vgabios_cirrus.bin $(wc -c < "$WORK_DIR/vgabios_cirrus.bin") bytes, vgabios_ega.bin $(wc -c < "$WORK_DIR/vgabios_ega.bin") bytes," \
    "vgabios_cga.bin $(wc -c < "$WORK_DIR/vgabios_cga.bin") bytes"
