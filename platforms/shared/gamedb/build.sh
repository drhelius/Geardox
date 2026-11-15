#!/bin/bash

set -e

PYTHON=${PYTHON:-python3}
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
OUTPUT="$SCRIPT_DIR/../../../src/media/game_db.h"

"$PYTHON" - "$SCRIPT_DIR" "$OUTPUT" << 'PYTHON_EOF'
import os
import re
import sys

script_dir, output = sys.argv[1:3]
ID = re.compile(r"^[a-z0-9_]+$")
LABEL = re.compile(r"^r[0-9]+$")
PART = re.compile(r"^[A-Z0-9_$~!#%&'(){}@^`\-]{1,8}(\.[A-Z0-9_$~!#%&'(){}@^`\-]{1,3})?$")
CRC = re.compile(r"^[0-9A-F]{8}$")
HEX = re.compile(r"^[0-9A-F]+$")
BYTES = re.compile(r"^([0-9A-F]{2})+$")
RELEASE_SECTIONS = ("sound", "drives")
SINGLE_MIN = 64 * 1024
errors = []


def fail(text):
    errors.append(text)


def read_index(path):
    sections = []
    current = None
    for number, raw in enumerate(open(path), 1):
        line = raw.strip()
        if not line or line.startswith((";", "#")):
            continue
        if line.startswith("[") and line.endswith("]"):
            current = (line[1:-1].strip(), [])
            sections.append(current)
        elif current is not None and "=" in line:
            key, value = line.split("=", 1)
            current[1].append((key.strip(), value.strip()))
        else:
            fail("gamedb.ini:%d: not a section or key" % number)
    return sections


def files_of(where, value, sig):
    files = []
    for item in value.split(","):
        parts = item.strip().rsplit(None, 2)
        if len(parts) != 3 or not parts[1].isdigit() or not CRC.match(parts[2]):
            fail("%s: '%s' isn't path, decimal size and 8 hex digits" % (where, item.strip()))
            continue
        path = parts[0]
        if sig and (path != path.upper() or ".." in path or "\\" in path or
                    not all(PART.match(p) for p in path.split("/"))):
            fail("%s: %s isn't an upper case 8.3 path with /" % (where, path))
        files.append((path.upper(), int(parts[1]), parts[2]))
    return files


def descriptor(entry, path):
    if not os.path.exists(path):
        fail("[%s]: games/%s is missing" % (entry, os.path.basename(path)))
        return "", ""
    text = open(path).read()
    section = ""
    name = ""
    for line in text.splitlines():
        strip = line.strip()
        if strip.startswith("[") and strip.endswith("]"):
            section = strip[1:-1].strip().lower()
            if section in RELEASE_SECTIONS:
                fail("[%s]: the descriptor has [%s], which belongs to releases" % (entry, section))
        elif "=" in strip and section == "game":
            key, value = [x.strip() for x in strip.split("=", 1)]
            if key.lower() == "source":
                fail("[%s]: the descriptor has [game] source" % entry)
            if key.lower() == "name":
                name = value
    if not name:
        fail("[%s]: the descriptor has no [game] name" % entry)
    return text, name


entries = []
releases = []
for entry, keys in read_index(os.path.join(script_dir, "gamedb.ini")):
    if not ID.match(entry):
        fail("[%s]: the id must be a-z, 0-9 and _" % entry)
    values = dict(keys)
    if values.get("gdox") != entry + ".gdox":
        fail("[%s]: gdox must be %s.gdox" % (entry, entry))
    text, name = descriptor(entry, os.path.join(script_dir, "games", entry + ".gdox"))
    index = len(entries)
    entries.append((entry, name, text))
    labels = [k for k, v in keys if k != "gdox" and "." not in k]
    if not labels:
        fail("[%s]: no release" % entry)
    for key, value in keys:
        if key == "gdox" or "." not in key:
            continue
        label, rest = key.split(".", 1)
        if label not in labels:
            fail("[%s]: %s belongs to no release" % (entry, key))
        elif rest not in ("layout", "when", "patch") and not (rest.count(".") == 1 and
                                                             rest.split(".")[0] in RELEASE_SECTIONS):
            fail("[%s]: %s isn't rN.layout, rN.when, rN.patch or rN.sound/drives.<key>" % (entry, key))
    for label in labels:
        where = "[%s] %s" % (entry, label)
        if not LABEL.match(label):
            fail("%s: the label must be r1, r2..." % where)
        sig = files_of(where, values[label], True)
        if len(sig) == 1 and sig[0][1] < SINGLE_MIN:
            fail("%s: a one-file release needs a program of at least %d KB" % (where, SINGLE_MIN // 1024))
        when = files_of(where + " when", values.get(label + ".when", ""), True) if label + ".when" in values else []
        rules = []
        for rule in values.get(label + ".layout", "").split(","):
            if not rule.strip():
                continue
            parts = [p.strip().replace("\\", "/").strip("/").upper() for p in rule.split(">")]
            if len(parts) != 2 or not parts[0]:
                fail("%s: layout rule '%s' isn't SRC > DST" % (where, rule.strip()))
                continue
            rules.append("%s>%s" % (parts[0], parts[1] or "."))
        patches = []
        sizes = dict((path, size) for path, size, crc in sig)
        for item in values.get(label + ".patch", "").split(","):
            if not item.strip():
                continue
            parts = item.split()
            if len(parts) != 4 or not HEX.match(parts[1]) or not BYTES.match(parts[2]) or not BYTES.match(parts[3]) \
                    or len(parts[2]) != len(parts[3]) or parts[2] == parts[3]:
                fail("%s: patch '%s' isn't PATH, hex offset, old and new bytes of one length" % (where, item.strip()))
            elif parts[0] not in sizes:
                fail("%s: patch %s isn't one of the release's files" % (where, parts[0]))
            elif int(parts[1], 16) + len(parts[2]) // 2 > sizes[parts[0]]:
                fail("%s: patch %s at %s goes past the file's end" % (where, parts[0], parts[1]))
            else:
                patches.append(" ".join(parts))
        settings = {}
        for key, value in keys:
            if key.startswith(label + ".") and key.count(".") == 2 and key.split(".")[1] in RELEASE_SECTIONS:
                settings.setdefault(key.split(".")[1], []).append("%s = %s" % (key.split(".")[2], value))
        if when and "sound" not in settings:
            fail("%s: a when condition with no sound settings" % where)
        fragment = "".join("[%s]\n%s\n" % (s, "\n".join(lines)) for s, lines in sorted(settings.items()))
        releases.append((index, label, sig, when, ";".join(rules), ";".join(patches), fragment))

sets = {}
shared = []
for index, label, sig, when, rules, patches, fragment in releases:
    key = frozenset(sig)
    for other in sets:
        if other == key or other <= key or key <= other:
            for other_index, other_label in sets[other]:
                if other_index != index and entries[other_index][2] != entries[index][2]:
                    shared.append("[%s] %s and [%s] %s" % (entries[index][0], label, entries[other_index][0],
                                                           other_label))
    sets.setdefault(key, []).append((index, label))

if errors:
    for e in errors:
        print("error: " + e)
    sys.exit("build.sh: %d errors, game_db.h not written" % len(errors))


def literal(text):
    if text is None:
        return "NULL"
    out = text.replace("\\", "\\\\").replace("\"", "\\\"")
    lines = out.split("\n")
    parts = ["\"%s\\n\"" % l for l in lines[:-1]] + (["\"%s\"" % lines[-1]] if lines[-1] else [])
    return "\n        ".join(parts) if parts else "\"\""


files = []
keys = []
release_rows = []
for number, (index, label, sig, when, rules, patches, fragment) in enumerate(releases):
    first = len(files)
    for path, size, crc in sig + when:
        files.append((size, crc, path))
    for path, size, crc in sig:
        keys.append((size, int(crc, 16), number))
    release_rows.append((index, first, len(sig), len(when), rules, patches, fragment))
keys.sort()

header = """/*
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
 * Generated by platforms/shared/gamedb/build.sh from gamedb.ini and games/, do not edit
 */

#ifndef GAME_DB_H
#define GAME_DB_H

#include "../common/types.h"

struct GDOX_DB_Entry
{
    const char* id;
    const char* name;
    const char* descriptor;
};

struct GDOX_DB_Release
{
    u16 entry;
    u16 first_file;
    u8 file_count;
    u8 when_count;
    const char* layout;
    const char* patches;
    const char* settings;
};

struct GDOX_DB_File
{
    u32 size;
    u32 crc;
    const char* path;
};

struct GDOX_DB_Key
{
    u32 size;
    u32 crc;
    u16 release;
};
"""

with open(output, "w") as out:
    out.write(header)
    out.write("\nconst GDOX_DB_Entry k_game_database_entries[] =\n{\n")
    for entry, name, text in entries:
        out.write("    { \"%s\", %s,\n        %s },\n" % (entry, literal(name), literal(text)))
    out.write("};\n\nconst int k_game_database_entry_count = %d;\n" % len(entries))
    out.write("\nconst GDOX_DB_Release k_game_database_releases[] =\n{\n")
    for index, first, count, when, rules, patches, fragment in release_rows:
        out.write("    { %d, %d, %d, %d, %s, %s, %s },\n" % (index, first, count, when,
                                                           literal(rules) if rules else "NULL",
                                                           literal(patches) if patches else "NULL",
                                                           literal(fragment) if fragment else "NULL"))
    out.write("};\n\nconst int k_game_database_release_count = %d;\n" % len(release_rows))
    out.write("\nconst GDOX_DB_File k_game_database_files[] =\n{\n")
    for size, crc, path in files:
        out.write("    { %d, 0x%s, \"%s\" },\n" % (size, crc, path))
    out.write("};\n\nconst int k_game_database_file_count = %d;\n" % len(files))
    out.write("\nconst GDOX_DB_Key k_game_database_keys[] =\n{\n")
    for size, crc, release in keys:
        out.write("    { %d, 0x%08X, %d },\n" % (size, crc, release))
    out.write("};\n\nconst int k_game_database_key_count = %d;\n" % len(keys))
    out.write("\n#endif /* GAME_DB_H */\n")

for line in shared:
    print("note: %s match the same folders, the picker offers both" % line)

print("game_db.h: %d entries, %d releases, %d files, %d keys" % (len(entries), len(release_rows), len(files),
                                                                len(keys)))
PYTHON_EOF
