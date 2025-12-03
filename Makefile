# ============================================================================
# RetroFuture OS Makefile
# ============================================================================
#
# Targets:
#   all        - Build everything
#   kernel     - Build kernel only
#   boot       - Build bootloader only
#   floppy     - Create bootable floppy image
#   iso        - Create bootable CD-ROM image
#   clean      - Remove build artifacts
#   run        - Run in QEMU
#   debug      - Run in QEMU with GDB
#
# Requirements:
#   - i686-elf cross compiler (or modify CC/LD)
#   - NASM assembler
#   - QEMU for testing
#   - xorriso/genisoimage for ISO creation
#
# ============================================================================

# Cross compiler prefix (change if using different toolchain)
CROSS = i686-elf-
CC = $(CROSS)gcc
LD = $(CROSS)ld
AS = nasm
OBJCOPY = $(CROSS)objcopy

# If no cross compiler, try native with -m32
ifeq ($(shell which $(CC) 2>/dev/null),)
    CC = gcc
    LD = ld
    OBJCOPY = objcopy
    CFLAGS_ARCH = -m32
    LDFLAGS_ARCH = -m elf_i386
endif

# Directories
BOOT_DIR = boot
KERNEL_DIR = kernel
DRIVERS_DIR = drivers
INCLUDE_DIR = include
BUILD_DIR = build
ISO_DIR = $(BUILD_DIR)/iso

# Compiler flags
CFLAGS = $(CFLAGS_ARCH) -ffreestanding -O2 -Wall -Wextra -nostdlib -nostdinc
CFLAGS += -fno-builtin -fno-stack-protector -fno-pic
CFLAGS += -I$(INCLUDE_DIR) -I$(KERNEL_DIR) -I$(DRIVERS_DIR)
CFLAGS += -march=pentium3 -mtune=pentium3

# Linker flags  
LDFLAGS = $(LDFLAGS_ARCH) -T linker.ld -nostdlib

# Assembler flags
ASFLAGS = -f bin
ASFLAGS_ELF = -f elf32

# Source files
BOOT_SRC = $(BOOT_DIR)/boot.asm $(BOOT_DIR)/stage2.asm
KERNEL_ASM = $(KERNEL_DIR)/entry.asm $(KERNEL_DIR)/idt_asm.asm
KERNEL_C = $(KERNEL_DIR)/kernel.c $(DRIVERS_DIR)/font_8x16.c

# Object files
KERNEL_OBJ = $(BUILD_DIR)/entry.o $(BUILD_DIR)/idt_asm.o $(BUILD_DIR)/kernel.o $(BUILD_DIR)/font_8x16.o

# Output files
BOOT_BIN = $(BUILD_DIR)/boot.bin
STAGE2_BIN = $(BUILD_DIR)/stage2.bin
KERNEL_ELF = $(BUILD_DIR)/kernel.elf
KERNEL_BIN = $(BUILD_DIR)/kernel.bin
FLOPPY_IMG = $(BUILD_DIR)/RetroFuture.img
ISO_IMG = $(BUILD_DIR)/RetroFuture.iso

# Text-mode output files (for older hardware)
STAGE2_TEXT_BIN = $(BUILD_DIR)/stage2-text.bin
FLOPPY_TEXT_IMG = $(BUILD_DIR)/RetroFuture-text.img
ISO_TEXT_IMG = $(BUILD_DIR)/RetroFuture-text.iso

# ============================================================================
# Targets
# ============================================================================

.PHONY: all kernel boot floppy iso clean run debug dirs textmode

all: dirs floppy iso

dirs:
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(ISO_DIR)/boot

# ============================================================================
# Bootloader
# ============================================================================

boot: dirs $(BOOT_BIN) $(STAGE2_BIN)

$(BOOT_BIN): $(BOOT_DIR)/boot.asm
	$(AS) $(ASFLAGS) -o $@ $<

$(STAGE2_BIN): $(BOOT_DIR)/stage2.asm
	$(AS) $(ASFLAGS) -o $@ $<

# ============================================================================
# Kernel
# ============================================================================

kernel: dirs $(KERNEL_BIN)

$(BUILD_DIR)/entry.o: $(KERNEL_DIR)/entry.asm
	$(AS) $(ASFLAGS_ELF) -o $@ $<

$(BUILD_DIR)/idt_asm.o: $(KERNEL_DIR)/idt_asm.asm
	$(AS) $(ASFLAGS_ELF) -o $@ $<

$(BUILD_DIR)/kernel.o: $(KERNEL_DIR)/kernel.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/font_8x16.o: $(DRIVERS_DIR)/font_8x16.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(KERNEL_ELF): $(KERNEL_OBJ)
	$(LD) $(LDFLAGS) -o $@ $^

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

# ============================================================================
# Floppy Image (1.44MB)
# ============================================================================

floppy: dirs boot kernel $(FLOPPY_IMG)

$(FLOPPY_IMG): $(BOOT_BIN) $(STAGE2_BIN) $(KERNEL_BIN)
	@echo "Creating floppy image..."
	# Create 1.44MB floppy image
	dd if=/dev/zero of=$@ bs=512 count=2880 2>/dev/null
	# Write boot sector (sector 0)
	dd if=$(BOOT_BIN) of=$@ bs=512 count=1 conv=notrunc 2>/dev/null
	# Write stage2 (sectors 1-32)
	dd if=$(STAGE2_BIN) of=$@ bs=512 seek=1 conv=notrunc 2>/dev/null
	# Write kernel (sectors 33+)
	dd if=$(KERNEL_BIN) of=$@ bs=512 seek=33 conv=notrunc 2>/dev/null
	@echo "Floppy image created: $@"

# ============================================================================
# ISO Image (El Torito bootable CD)
# ============================================================================

iso: dirs floppy $(ISO_IMG)

$(ISO_IMG): $(FLOPPY_IMG)
	@echo "Creating ISO image..."
	cp $(FLOPPY_IMG) $(ISO_DIR)/boot/RetroFuture.img
	# Use floppy emulation mode for proper bootloader operation
	@if command -v xorriso >/dev/null 2>&1; then \
		xorriso -as mkisofs \
			-b boot/RetroFuture.img \
			-o $@ $(ISO_DIR); \
	elif command -v genisoimage >/dev/null 2>&1; then \
		genisoimage \
			-b boot/RetroFuture.img \
			-o $@ $(ISO_DIR); \
	elif command -v mkisofs >/dev/null 2>&1; then \
		mkisofs \
			-b boot/RetroFuture.img \
			-o $@ $(ISO_DIR); \
	else \
		echo "Warning: No ISO creation tool found (xorriso/genisoimage/mkisofs)"; \
		echo "ISO image not created, but floppy image is ready."; \
	fi
	@echo "ISO image created: $@"

# ============================================================================
# Text Mode Build (for older hardware like Compaq Armada)
# ============================================================================

textmode: dirs $(BOOT_BIN) $(STAGE2_TEXT_BIN) $(KERNEL_BIN) $(FLOPPY_TEXT_IMG) $(ISO_TEXT_IMG)
	@echo "Text-mode build complete!"
	@echo "  Floppy: $(FLOPPY_TEXT_IMG)"
	@echo "  CD-ROM: $(ISO_TEXT_IMG)"

$(STAGE2_TEXT_BIN): $(BOOT_DIR)/stage2.asm
	$(AS) $(ASFLAGS) -DSKIP_VESA -o $@ $<

$(FLOPPY_TEXT_IMG): $(BOOT_BIN) $(STAGE2_TEXT_BIN) $(KERNEL_BIN)
	@echo "Creating text-mode floppy image..."
	dd if=/dev/zero of=$@ bs=512 count=2880 2>/dev/null
	dd if=$(BOOT_BIN) of=$@ bs=512 seek=0 conv=notrunc 2>/dev/null
	dd if=$(STAGE2_TEXT_BIN) of=$@ bs=512 seek=1 conv=notrunc 2>/dev/null
	dd if=$(KERNEL_BIN) of=$@ bs=512 seek=33 conv=notrunc 2>/dev/null
	@echo "Text-mode floppy image created: $@"

$(ISO_TEXT_IMG): $(FLOPPY_TEXT_IMG)
	@echo "Creating text-mode ISO image..."
	@mkdir -p $(BUILD_DIR)/iso-text/boot
	@cp $(FLOPPY_TEXT_IMG) $(BUILD_DIR)/iso-text/boot/RetroFuture.img
	@if command -v xorriso >/dev/null 2>&1; then \
		xorriso -as mkisofs -o $@ -b boot/RetroFuture.img $(BUILD_DIR)/iso-text 2>/dev/null; \
	elif command -v genisoimage >/dev/null 2>&1; then \
		genisoimage -o $@ -b boot/RetroFuture.img $(BUILD_DIR)/iso-text 2>/dev/null; \
	else \
		echo "No ISO creation tool found (xorriso or genisoimage)"; \
		echo "ISO image not created, but floppy image is ready."; \
	fi
	@echo "Text-mode ISO image created: $@"

# ============================================================================
# QEMU Testing
# ============================================================================

run: floppy
	qemu-system-i386 \
		-fda $(FLOPPY_IMG) \
		-m 256M \
		-cpu pentium3 \
		-vga std \
		-serial stdio

run-iso: iso
	qemu-system-i386 \
		-cdrom $(ISO_IMG) \
		-m 256M \
		-cpu pentium3 \
		-vga std \
		-serial stdio

debug: floppy
	qemu-system-i386 \
		-fda $(FLOPPY_IMG) \
		-m 256M \
		-cpu pentium3 \
		-vga std \
		-serial stdio \
		-s -S &
	@echo "QEMU waiting for GDB connection on port 1234"
	@echo "Run: gdb -ex 'target remote localhost:1234' $(KERNEL_ELF)"

# ============================================================================
# Bochs Testing (alternative emulator)
# ============================================================================

bochs: floppy
	@echo "Creating Bochs config..."
	@echo 'megs: 256' > $(BUILD_DIR)/bochsrc
	@echo 'romimage: file=/usr/share/bochs/BIOS-bochs-latest' >> $(BUILD_DIR)/bochsrc
	@echo 'vgaromimage: file=/usr/share/bochs/VGABIOS-lgpl-latest' >> $(BUILD_DIR)/bochsrc
	@echo 'floppya: 1_44=$(FLOPPY_IMG), status=inserted' >> $(BUILD_DIR)/bochsrc
	@echo 'boot: floppy' >> $(BUILD_DIR)/bochsrc
	@echo 'cpu: model=pentium_mmx' >> $(BUILD_DIR)/bochsrc
	@echo 'log: $(BUILD_DIR)/bochs.log' >> $(BUILD_DIR)/bochsrc
	bochs -f $(BUILD_DIR)/bochsrc -q

# ============================================================================
# Clean
# ============================================================================

clean:
	rm -rf $(BUILD_DIR)

# ============================================================================
# Help
# ============================================================================

help:
	@echo "RetroFuture OS Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all      - Build floppy and ISO images"
	@echo "  floppy   - Create bootable floppy image"
	@echo "  iso      - Create bootable CD-ROM image"
	@echo "  kernel   - Build kernel only"
	@echo "  boot     - Build bootloader only"
	@echo "  run      - Run in QEMU (floppy)"
	@echo "  run-iso  - Run in QEMU (CD-ROM)"
	@echo "  debug    - Run in QEMU with GDB server"
	@echo "  bochs    - Run in Bochs emulator"
	@echo "  clean    - Remove build artifacts"
	@echo ""
	@echo "Output files:"
	@echo "  $(FLOPPY_IMG)  - 1.44MB floppy image"
	@echo "  $(ISO_IMG)     - Bootable CD-ROM image"

# ============================================================================
# Physical Media Writing (use with caution!)
# ============================================================================

write-floppy: floppy
	@echo "WARNING: This will overwrite /dev/fd0!"
	@echo "Press Ctrl+C to cancel, or Enter to continue..."
	@read dummy
	sudo dd if=$(FLOPPY_IMG) of=/dev/fd0 bs=512

write-usb: floppy
	@echo "This will write the floppy image to a USB drive."
	@echo "Available drives:"
	@lsblk -d -o NAME,SIZE,MODEL
	@echo ""
	@echo "Enter device (e.g., sdb): "
	@read DEVICE && \
	echo "WARNING: This will DESTROY all data on /dev/$$DEVICE!" && \
	echo "Press Ctrl+C to cancel, or Enter to continue..." && \
	read dummy && \
	sudo dd if=$(FLOPPY_IMG) of=/dev/$$DEVICE bs=512 && \
	sync

# Programs directory
PROGRAMS_DIR = programs
PROGRAMS_BUILD = $(BUILD_DIR)/programs

# Programs floppy image
PROGRAMS_IMG = $(BUILD_DIR)/Programs.img

# List of programs to build (add more as needed)
PROGRAMS = hello.bin

# ============================================================================
# Programs Target
# ============================================================================

.PHONY: programs programs-floppy

programs: dirs $(addprefix $(PROGRAMS_BUILD)/, $(PROGRAMS))
	@echo "Programs built successfully!"

# Create programs directory
$(PROGRAMS_BUILD):
	@mkdir -p $(PROGRAMS_BUILD)

# ============================================================================
# Build Individual Programs
# ============================================================================

# Assembly programs (.asm -> .bin)
$(PROGRAMS_BUILD)/%.bin: $(PROGRAMS_DIR)/%.asm | $(PROGRAMS_BUILD)
	$(AS) -f bin -o $@ $<

# C programs - compile and link
# For C programs, we need a special link process
$(PROGRAMS_BUILD)/%.o: $(PROGRAMS_DIR)/%.c | $(PROGRAMS_BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

# Link C program to flat binary
# Note: This requires program.ld linker script in programs/
$(PROGRAMS_BUILD)/%.bin: $(PROGRAMS_BUILD)/%.o $(PROGRAMS_DIR)/program.ld
	$(LD) $(LDFLAGS_ARCH) -T $(PROGRAMS_DIR)/program.ld -o $@ $<

# ============================================================================
# Programs Floppy Image (FAT12)
# ============================================================================

programs-floppy: programs $(PROGRAMS_IMG)
	@echo "Programs floppy created: $(PROGRAMS_IMG)"

$(PROGRAMS_IMG): $(addprefix $(PROGRAMS_BUILD)/, $(PROGRAMS))
	@echo "Creating FAT12 programs floppy..."
	# Create blank 1.44MB image
	dd if=/dev/zero of=$@ bs=512 count=2880 2>/dev/null
	# Format as FAT12 using mformat (from mtools)
	mformat -i $@ -f 1440 ::
	# Copy all programs to floppy
	@for prog in $(PROGRAMS); do \
		echo "  Copying $$prog..."; \
		mcopy -i $@ $(PROGRAMS_BUILD)/$$prog ::/; \
	done
	# List contents
	@echo "Programs floppy contents:"
	@mdir -i $@ ::
	@echo ""

# ============================================================================
# Alternative: Programs Floppy without mtools
# ============================================================================
#
# If you don't have mtools, you can use this approach instead.
# It creates a FAT12 image using a pre-made template or loopback mount.
#
# programs-floppy-loop: programs
# 	dd if=/dev/zero of=$(PROGRAMS_IMG) bs=512 count=2880
# 	mkfs.fat -F 12 $(PROGRAMS_IMG)
# 	mkdir -p /tmp/rfos-mount
# 	sudo mount -o loop $(PROGRAMS_IMG) /tmp/rfos-mount
# 	sudo cp $(PROGRAMS_BUILD)/*.bin /tmp/rfos-mount/
# 	sudo umount /tmp/rfos-mount
# 	rmdir /tmp/rfos-mount

# ============================================================================
# Run with both floppies
# ============================================================================

run-dual: floppy programs-floppy
	qemu-system-i386 \
		-fda $(FLOPPY_IMG) \
		-fdb $(PROGRAMS_IMG) \
		-m 256M \
		-cpu pentium3 \
		-vga std \
		-serial stdio

# ============================================================================
# Clean programs
# ============================================================================

clean-programs:
	rm -rf $(PROGRAMS_BUILD)
	rm -f $(PROGRAMS_IMG)

# Add to main clean target:
# clean: clean-programs
#     rm -rf $(BUILD_DIR)