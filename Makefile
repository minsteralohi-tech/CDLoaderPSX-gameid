# PS1 bare-metal CD loader - build
#
# Produces cdloader.exe, a PlayStation PS-EXE.
#
# Requires a bare-metal little-endian MIPS toolchain. On Ubuntu:
#     sudo apt-get install gcc-mipsel-linux-gnu python3
# and (if your toolchain uses a different prefix) override CROSS, e.g.:
#     make CROSS=mipsel-none-elf-

CROSS   ?= mipsel-linux-gnu-
CC      := $(CROSS)gcc
PYTHON  ?= python3

TARGET  := cdloader
LDSCRIPT := ps1-exe.ld

# Bare-metal / freestanding flags for the PS1 R3000A.
CFLAGS := \
	-march=mips1 -mabi=32 -EL -G0 \
	-mno-abicalls -fno-pic -mno-gpopt -msoft-float \
	-nostdlib -ffreestanding -fno-builtin -fno-stack-protector \
	-ffunction-sections -fdata-sections \
	-Os -fomit-frame-pointer \
	-Wall -Wextra -Wno-unused-parameter

LDFLAGS := -nostdlib -Wl,-T,$(LDSCRIPT) -Wl,--gc-sections -Wl,-Map,$(TARGET).map

CSRC := $(wildcard src/*.c)
ASRC := $(wildcard src/*.S)
OBJ  := $(CSRC:.c=.o) $(ASRC:.S=.o)

.PHONY: all clean

all: $(TARGET).exe

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJ) $(LDSCRIPT)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJ) -lgcc -o $@

$(TARGET).exe: $(TARGET).elf
	$(PYTHON) tools/elf2psexe.py $< $@

clean:
	rm -f src/*.o $(TARGET).elf $(TARGET).exe $(TARGET).map
