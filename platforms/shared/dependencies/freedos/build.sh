#!/bin/bash

# Builds the files of the FreeDOS disk and its boot sectors from the unmodified packages, and writes freedos.cpp
# Needs Python, NASM and UPX

set -e

PYTHON=${PYTHON:-python3}
NASM=${NASM:-nasm}
UPX=${UPX:-upx}

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
WORK_DIR=$(mktemp -d)
trap 'rm -rf "$WORK_DIR"' EXIT

"$PYTHON" - "$SCRIPT_DIR" "$WORK_DIR" "$NASM" "$UPX" << 'PYTHON_EOF'
import hashlib
import io
import os
import struct
import subprocess
import sys
import zipfile

script_dir, work_dir, nasm, upx = sys.argv[1:5]
packages = os.path.join(script_dir, "packages")

KERNEL = "kernel"
FDISK = "fdisk"
BIN = "FREEDOS/BIN/"
FIXED_DATE = (2025, 4, 5, 12, 0, 0)
OEM_NAME = b"FRDOS5.1"
VERSION = "FreeDOS 1.4, kernel 2043, FreeCOM 0.86a"

FILES = [
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
    ("lbacache", "LBACACHE.COM", BIN + "LBACACHE.COM"),
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

def upx_exe(data, ss, sp):
    size = len(data) + 32
    header = struct.pack("<2s13H", b"MZ", size % 512, (size + 511) // 512, 0, 2, 0x1000, 0xFFFF, ss, sp, 0, 0, 0,
                         0x1C, 0)
    return header.ljust(32, b"\0") + data

def run_upx(data, options):
    source = os.path.join(work_dir, "upx_in.exe")
    output = os.path.join(work_dir, "upx_out.exe")
    open(source, "wb").write(data)

    if os.path.exists(output):
        os.remove(output)

    subprocess.check_call([upx, "-q"] + options + [source, "-o", output], stdout=subprocess.DEVNULL)
    return open(output, "rb").read()

def kernel_without_sign_on():
    data = read_member(KERNEL, "KERNL386.SYS")
    config, packed, trailer = data[:32], data[32:-34], bytearray(data[-34:])
    ss, sp = struct.unpack_from("<H", trailer, 23)[0], struct.unpack_from("<H", trailer, 28)[0]
    image = bytearray(run_upx(upx_exe(packed, ss, sp), ["-d"]))

    sign_on = image.find(b"\r%SKernel compatibility %d.%d")

    if sign_on < 0 or image.count(b"\r%SKernel compatibility %d.%d") != 1:
        sys.exit("KERNL386.SYS has no sign-on to remove")

    image[sign_on] = 0
    kernel_ip, kernel_cs = struct.unpack_from("<2H", image, 20)
    exe = run_upx(bytes(image), ["--8086", "--best", "--no-reloc"])
    magic, extra, pages, relocs, header_size = struct.unpack_from("<2s4H", exe)
    ss, sp, checksum, ip, cs = struct.unpack_from("<5H", exe, 14)

    if relocs or ip or cs:
        sys.exit("UPX made a kernel the CONFIG block can't start")

    size = pages * 512 - (512 - extra if extra else 0)
    packed = exe[header_size * 16:size]
    relative_jump = b"\x81\xC5" + struct.pack("<H", kernel_cs) + b"\x55\xB8" + struct.pack("<H", kernel_ip) + b"\x50\xCB"

    if packed.count(relative_jump) != 1:
        sys.exit("UPX made a kernel with no jump to %04X:%04X" % (kernel_cs, kernel_ip))

    absolute_jump = b"\xEA" + struct.pack("<2H", kernel_ip, kernel_cs) + b"\x90" * 5
    packed = packed.replace(relative_jump, absolute_jump)
    config = config[:0x1E] + struct.pack("<H", len(packed))
    struct.pack_into("<H", trailer, 1, len(packed) + 0x20)
    struct.pack_into("<H", trailer, 23, ss)
    struct.pack_into("<H", trailer, 28, sp)
    return config + packed + bytes(trailer)

def disk_files():
    files = [(target, read_member(package, name)) for package, name, target in FILES]
    files.append(("KERNEL.SYS", kernel_without_sign_on()))
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

print("Built %s: %d bytes of files" % (VERSION, len(files)))
PYTHON_EOF
