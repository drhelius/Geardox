CC = i586-pc-msdosdjgpp-gcc
GEARDOX ?= geardox
GDXDBG_DIR ?= $(if $(wildcard include/gdxdbg.h),include,../../include)
CFLAGS = -g -O0 -Wall -I$(GDXDBG_DIR)
SOURCES = src/hello.c $(GDXDBG_DIR)/gdxdbg.c

all: build/hello.exe

build/hello.exe: $(SOURCES) $(GDXDBG_DIR)/gdxdbg.h
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(SOURCES) -Wl,-Map,build/hello.map
	@if [ -f CWSDPMI.EXE ]; then cp CWSDPMI.EXE build/; fi

run: build/hello.exe
	$(GEARDOX) --run "BUILD\HELLO.EXE" --exit-on-exit --e9 hello.gdox

test: build/hello.exe
	$(GEARDOX) --headless --run "BUILD\HELLO.EXE" --exit-on-exit --timeout 60 --no-write-back hello.gdox

clean:
	rm -rf build

.PHONY: all run test clean
