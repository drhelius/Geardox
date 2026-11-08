#!/bin/bash

# Builds the files of the FreeDOS disk and its boot sectors from the unmodified packages, and writes freedos.cpp
# Needs Python and NASM

set -e

PYTHON=${PYTHON:-python3}
NASM=${NASM:-nasm}

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
WORK_DIR=$(mktemp -d)
trap 'rm -rf "$WORK_DIR"' EXIT

"$PYTHON" - "$SCRIPT_DIR" "$WORK_DIR" "$NASM" << 'PYTHON_EOF'
import hashlib
import io
import os
import subprocess
import sys
import zipfile

script_dir, work_dir, nasm = sys.argv[1:4]
packages = os.path.join(script_dir, "packages")

KERNEL = "kernel"
FDISK = "fdisk"
BIN = "FREEDOS/BIN/"
FIXED_DATE = (2025, 4, 5, 12, 0, 0)
OEM_NAME = b"FRDOS5.1"
VERSION = "FreeDOS 1.4, kernel 2043, FreeCOM 0.86a"

FILES = [
    (KERNEL, "KERNL386.SYS", "KERNEL.SYS"),
    ("freecom", "COMMAND.COM", BIN + "COMMAND.COM"),
    (KERNEL, "SYS.COM", BIN + "SYS.COM"),
    (KERNEL, "COUNTRY.SYS", BIN + "COUNTRY.SYS"),
    ("attrib", "ATTRIB.COM", BIN + "ATTRIB.COM"),
    ("chkdsk", "CHKDSK.EXE", BIN + "CHKDSK.EXE"),
    ("choice", "CHOICE.EXE", BIN + "CHOICE.EXE"),
    ("ctmouse", "CTMOUSE.EXE", BIN + "CTMOUSE.EXE"),
    ("deltree", "DELTREE.COM", BIN + "DELTREE.COM"),
    ("diskcopy", "DISKCOPY.EXE", BIN + "DISKCOPY.EXE"),
    ("edit", "EDIT.EXE", BIN + "EDIT.EXE"),
    ("edit", "EDIT.HLP", BIN + "EDIT.HLP"),
    (FDISK, "FDISK.EXE", BIN + "FDISK.EXE"),
    (FDISK, "FDISK.INI", BIN + "FDISK.INI"),
    ("find", "FIND.COM", BIN + "FIND.COM"),
    ("format", "FORMAT.EXE", BIN + "FORMAT.EXE"),
    ("himemx", "HIMEMX.EXE", BIN + "HIMEMX.EXE"),
    ("jemm", "JEMMEX.EXE", BIN + "JEMMEX.EXE"),
    ("jemm", "JEMM386.EXE", BIN + "JEMM386.EXE"),
    ("keyb", "KEYB.EXE", BIN + "KEYB.EXE"),
    ("keyb_lay", "KEYBOARD.SYS", BIN + "KEYBOARD.SYS"),
    ("label", "LABEL.EXE", BIN + "LABEL.EXE"),
    ("mem", "MEM.EXE", BIN + "MEM.EXE"),
    ("mode", "MODE.COM", BIN + "MODE.COM"),
    ("more", "MORE.EXE", BIN + "MORE.EXE"),
    ("move", "MOVE.EXE", BIN + "MOVE.EXE"),
    ("shsucdx", "SHSUCDX.COM", BIN + "SHSUCDX.COM"),
    ("sort", "SORT.EXE", BIN + "SORT.EXE"),
    ("tree", "TREE.COM", BIN + "TREE.COM"),
    ("udvd2", "UDVD2.SYS", BIN + "UDVD2.SYS"),
    ("xcopy", "XCOPY.EXE", BIN + "XCOPY.EXE"),
]

def check_packages():
    for line in open(os.path.join(script_dir, "packages.sha256")):
        expected, name = line.split()
        name = name.lstrip("*")
        digest = hashlib.sha256(open(os.path.join(packages, name), "rb").read()).hexdigest()

        if digest != expected:
            sys.exit("%s doesn't match packages.sha256" % name)

def read_member(package, name):
    with zipfile.ZipFile(os.path.join(packages, package + ".zip")) as archive:
        for member in archive.namelist():
            if member.upper() == "BIN/" + name:
                return archive.read(member)

    sys.exit("%s has no %s" % (package, name))

def read_license(package, name):
    with zipfile.ZipFile(os.path.join(packages, package + ".zip")) as archive:
        for member in archive.namelist():
            if member.upper() == name:
                return archive.read(member)

    sys.exit("%s has no %s" % (package, name))

def read_sources(package):
    with zipfile.ZipFile(os.path.join(packages, package + ".zip")) as archive:
        for member in archive.namelist():
            if member.upper().endswith("SOURCES.ZIP"):
                return zipfile.ZipFile(io.BytesIO(archive.read(member)))

    sys.exit("%s has no sources" % package)

def assemble(source, output, defines=()):
    command = [nasm, "-f", "bin"] + list(defines)
    command += ["-I", os.path.dirname(source) + "/", source, "-o", output]
    subprocess.check_call(command)
    return open(output, "rb").read()

def boot_sector():
    read_sources(KERNEL).extractall(os.path.join(work_dir, "kernel"))
    source = os.path.join(work_dir, "kernel", "boot", "boot.asm")
    sector = bytearray(assemble(source, os.path.join(work_dir, "boot.bin"), ["-dISFAT16"]))
    sector[3:11] = OEM_NAME
    return bytes(sector)

def master_boot_record():
    read_sources(FDISK).extract("BOOTNORM.ASM", os.path.join(work_dir, "fdisk"))
    return assemble(os.path.join(work_dir, "fdisk", "BOOTNORM.ASM"), os.path.join(work_dir, "mbr.bin"))

def add(archive, name, data):
    entry = zipfile.ZipInfo(name, FIXED_DATE)
    entry.compress_type = zipfile.ZIP_DEFLATED
    entry.create_system = 0
    archive.writestr(entry, data, compresslevel=9)

def text_file(name):
    data = open(os.path.join(script_dir, name), "rb").read().replace(b"\r\n", b"\n")
    return data.replace(b"\n", b"\r\n")

def disk_files():
    files = [(target, read_member(package, name)) for package, name, target in FILES]
    files.append(("FDCONFIG.SYS", text_file("FDCONFIG.SYS")))
    files.append(("FDAUTO.BAT", text_file("FDAUTO.BAT")))
    files.append(("FREEDOS/COPYING.TXT", text_file("COPYING")))
    files.append(("FREEDOS/ARTISTIC.TXT", read_license("jemm", "DOC/JEMM/ARTISTIC.TXT")))
    files.append(("TEMP/", b""))
    files.sort(key=lambda item: item[0])
    buffer = io.BytesIO()

    with zipfile.ZipFile(buffer, "w") as archive:
        for name, data in files:
            add(archive, name, data)

    return buffer.getvalue()

def array(name, data):
    lines = ["const uint8_t %s[] =" % name, "{"]

    for i in range(0, len(data), 16):
        chunk = ", ".join("0x%02X" % b for b in data[i:i + 16])
        lines.append("    " + chunk + ("," if i + 16 < len(data) else ""))

    lines.append("};")
    lines.append("")
    lines.append("const uint32_t %s_size = %d;" % (name, len(data)))
    return "\n".join(lines)

check_packages()
files = disk_files()
boot = boot_sector()
mbr = master_boot_record()
no_system = assemble(os.path.join(script_dir, "nosys.asm"), os.path.join(work_dir, "nosys.bin"))

header = """/*
 * The files of the FreeDOS disk and its boot sectors, built for Geardox by build.sh, do not edit
 * FreeDOS: https://www.freedos.org, see COPYING and README.md
 */

#include "freedos.h"
"""

with open(os.path.join(script_dir, "freedos.cpp"), "w") as out:
    out.write(header)
    out.write('\nconst char* const k_freedos_version = "%s";\n' % VERSION)
    out.write("\n" + array("k_freedos_files", files) + "\n")
    out.write("\n" + array("k_freedos_boot", boot) + "\n")
    out.write("\n" + array("k_freedos_mbr", mbr) + "\n")
    out.write("\n" + array("k_freedos_no_system", no_system) + "\n")

print("Built %s: %d bytes of files, %d of no system code" % (VERSION, len(files), len(no_system)))
PYTHON_EOF
