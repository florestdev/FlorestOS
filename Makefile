# FlorestOS 1.0 - Makefile
# Requires: nasm, g++-multilib, binutils, qemu-system-x86
# For ISO: genisoimage (or mkisofs / xorriso)

AS      = nasm
CXX     = g++
LD      = ld

ASFLAGS  = -f elf32
CXXFLAGS = -m32 -ffreestanding -fno-exceptions -fno-rtti -fno-pie -no-pie \
           -Wall -Wextra -O2 -Iinclude -std=c++17
LDFLAGS  = -m elf_i386 -T linker.ld -nostdlib

BUILD = build
SRC   = src
BOOT  = boot

KERNEL_OBJS = \
	$(BUILD)/kernel_entry.o \
	$(BUILD)/kernel.o \
	$(BUILD)/vga.o \
	$(BUILD)/string.o \
	$(BUILD)/pci.o \
	$(BUILD)/e1000.o \
	$(BUILD)/rtl8139.o \
	$(BUILD)/virtio_net.o \
	$(BUILD)/nic.o \
	$(BUILD)/netstack.o \
	$(BUILD)/keyboard.o \
	$(BUILD)/timer.o \
	$(BUILD)/dns.o \
	$(BUILD)/results.o \
	$(BUILD)/fln.o \
	$(BUILD)/boot_embed.o \
	$(BUILD)/gui.o \
	$(BUILD)/ata.o \
	$(BUILD)/mouse.o





$(BUILD)/boot_embed.c: $(BUILD)/bootloader.bin
	python3 tools_embed.py $(BUILD)/bootloader.bin $@

$(BUILD)/boot_embed.o: $(BUILD)/boot_embed.c
	$(CXX) $(CXXFLAGS) -c $< -o $@

.PHONY: all clean run run-iso iso info

all: $(BUILD)/florestos.img $(BUILD)/florestos.iso

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/bootloader.bin: $(BOOT)/bootloader.asm | $(BUILD)
	$(AS) -f bin $< -o $@

$(BUILD)/kernel_entry.o: $(BOOT)/kernel_entry.asm | $(BUILD)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/%.o: $(SRC)/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/kernel.bin: $(KERNEL_OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $(BUILD)/kernel.elf $(KERNEL_OBJS)
	objcopy -O binary $(BUILD)/kernel.elf $@

# Raw bootable disk image (HDD style for qemu -drive)
$(BUILD)/florestos.img: $(BUILD)/bootloader.bin $(BUILD)/kernel.bin
	# 8MB raw disk — attach in VirtualBox as hard disk (no guest install needed)
	dd if=/dev/zero of=$@ bs=1M count=8 2>/dev/null
	dd if=$(BUILD)/bootloader.bin of=$@ conv=notrunc 2>/dev/null
	dd if=$(BUILD)/kernel.bin of=$@ bs=512 seek=1 conv=notrunc 2>/dev/null
	@echo "  -> $@"
	@echo "  VirtualBox: VBoxManage convertfromraw $@ FlorestOS-hdd.vdi --format VDI"

# 1.44MB floppy image for El Torito floppy emulation
$(BUILD)/florestos.flp: $(BUILD)/bootloader.bin $(BUILD)/kernel.bin
	dd if=/dev/zero of=$@ bs=512 count=2880 2>/dev/null
	dd if=$(BUILD)/bootloader.bin of=$@ conv=notrunc 2>/dev/null
	dd if=$(BUILD)/kernel.bin of=$@ bs=512 seek=1 conv=notrunc 2>/dev/null

# Bootable ISO (El Torito floppy emulation)
$(BUILD)/florestos.iso: $(BUILD)/florestos.flp
	rm -rf $(BUILD)/iso
	mkdir -p $(BUILD)/iso
	cp $(BUILD)/florestos.flp $(BUILD)/iso/florestos.img
	@if command -v genisoimage >/dev/null 2>&1; then \
		genisoimage -quiet -o $@ -V FlorestOS \
			-b florestos.img -c boot.cat \
			$(BUILD)/iso; \
	elif command -v mkisofs >/dev/null 2>&1; then \
		mkisofs -quiet -o $@ -V FlorestOS \
			-b florestos.img -c boot.cat \
			$(BUILD)/iso; \
	elif command -v xorriso >/dev/null 2>&1; then \
		xorriso -as mkisofs -quiet -o $@ -V FlorestOS \
			-b florestos.img -c boot.cat \
			$(BUILD)/iso; \
	else \
		echo "ERROR: install genisoimage (sudo apt install genisoimage)"; \
		exit 1; \
	fi
	@echo "  -> $@"
	@echo ""
	@echo "=== FlorestOS 1.0 built ==="
	@echo "  Disk: $(BUILD)/florestos.img"
	@echo "  ISO:  $(BUILD)/florestos.iso"
	@echo "  make run / make run-iso"

run: $(BUILD)/florestos.img
	qemu-system-i386 \
		-drive format=raw,file=$(BUILD)/florestos.img \
		-m 64M \
		-netdev user,id=net0 \
		-device e1000,netdev=net0

run-iso: $(BUILD)/florestos.iso
	qemu-system-i386 \
		-cdrom $(BUILD)/florestos.iso \
		-boot d \
		-m 64M \
		-netdev user,id=net0 \
		-device e1000,netdev=net0

iso: $(BUILD)/florestos.iso

clean:
	rm -rf $(BUILD)

info:
	@echo "FlorestOS 1.0"
	@echo "  make          - .img + .iso"
	@echo "  make run      - boot disk image"
	@echo "  make run-iso  - boot ISO"
	@echo "  make clean"
