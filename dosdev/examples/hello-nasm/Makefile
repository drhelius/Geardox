NASM = nasm
LD = i686-elf-ld
OBJCOPY = i686-elf-objcopy
GEARDOX ?= geardox
GDXDBG_DIR ?= $(if $(wildcard include/gdxdbg.inc),include,../../include)

all: build/hello.com

build/hello.com: build/hello.elf
	$(OBJCOPY) -O binary $< $@
	i686-elf-nm $< > build/hello.nm

build/hello.elf: src/hello.asm $(GDXDBG_DIR)/gdxdbg.inc
	mkdir -p build
	$(NASM) -f elf32 -g -F dwarf -I$(GDXDBG_DIR)/ -o build/hello.o src/hello.asm
	$(LD) -m elf_i386 -Ttext=0x100 -e start -o $@ build/hello.o

run: build/hello.com
	$(GEARDOX) --run "BUILD\HELLO.COM" --exit-on-exit --e9 hello.gdox

test: build/hello.com
	$(GEARDOX) --headless --run "BUILD\HELLO.COM" --exit-on-exit --timeout 60 --no-write-back hello.gdox

clean:
	rm -rf build

.PHONY: all run test clean
