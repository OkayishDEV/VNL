# Use native gcc (x86_64-pc-linux-gnu) with freestanding flags.
# x86_64-elf-gcc is preferred if installed; fall back to system gcc.
CC         := clang -target x86_64-elf
AS         := nasm
LD         := ld.lld

CFLAGS     := -std=gnu11 -ffreestanding -O2 -Wall -Wextra \
              -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -msoft-float \
              -mcmodel=kernel -Iinclude \
              -fno-stack-protector -fno-builtin -fno-pic
ASFLAGS    := -f elf64
LDFLAGS    := -T linker.ld -nostdlib -z max-page-size=0x1000

SRCDIR     := src
BUILDDIR   := build

ASM_SRCS   := $(filter-out $(SRCDIR)/boot/bootloader.asm,$(wildcard $(SRCDIR)/*/*.asm))
C_SRCS     := $(wildcard $(SRCDIR)/*/*.c)

ASM_OBJS   := $(patsubst $(SRCDIR)/%.asm,$(BUILDDIR)/%.asm.o,$(ASM_SRCS))
C_OBJS     := $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.c.o,$(C_SRCS))

KERNEL     := $(BUILDDIR)/vnl.kernel
ISO        := vnl.iso

.PHONY: all clean run

all: $(KERNEL)

USER_CFLAGS := -std=gnu11 -O2 -ffreestanding -Iports/sysroot/usr/include -Iuserspace/include -Wall -Wextra -fno-stack-protector -fno-builtin -fno-pic

$(BUILDDIR)/tinywl.o: userspace/tinywl.c
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c -o $@ $<

$(BUILDDIR)/backend.o: userspace/backend/vnl/backend.c
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c -o $@ $<

$(BUILDDIR)/vnl-x.elf: $(BUILDDIR)/tinywl.o $(BUILDDIR)/backend.o userspace/vnl-x.ld
	@mkdir -p $(dir $@)
	$(LD) -T userspace/vnl-x.ld -o $@ $(BUILDDIR)/tinywl.o $(BUILDDIR)/backend.o \
		ports/sysroot/usr/lib/libwayland-server.a \
		ports/sysroot/usr/lib/libpixman-1.a \
		ports/sysroot/usr/lib/libffi.a \
		ports/sysroot/usr/lib/libxkbcommon.a \
		ports/sysroot/usr/lib/libc.a

$(BUILDDIR)/kernel/vnl_elf_blob.asm.o: $(BUILDDIR)/vnl-x.elf

$(KERNEL): $(ASM_OBJS) $(C_OBJS) $(BUILDDIR)/vnl-x.elf
	@mkdir -p $(BUILDDIR)
	$(LD) $(LDFLAGS) -o $@ $(ASM_OBJS) $(C_OBJS)
	@echo "[LD]  $@"

$(BUILDDIR)/%.asm.o: $(SRCDIR)/%.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@
	@echo "[AS]  $<"

$(BUILDDIR)/%.c.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
	@echo "[CC]  $<"

$(BUILDDIR)/bootloader.bin: src/boot/bootloader.asm
	@mkdir -p $(BUILDDIR)
	$(AS) -f bin src/boot/bootloader.asm -o $@

vnl.img: $(BUILDDIR)/bootloader.bin $(KERNEL)
	python -c "import sys; data = open(sys.argv[1], 'rb').read() + open(sys.argv[2], 'rb').read(); open(sys.argv[3], 'wb').write(data + b'\x00' * (10*1024*1024 - len(data)))" $(BUILDDIR)/bootloader.bin $(KERNEL) $@

run: vnl.img
	qemu-system-x86_64 -drive file=vnl.img,format=raw -m 256M -serial stdio -no-reboot -no-shutdown -net nic,model=rtl8139 -net user

clean:
	rm -rf $(BUILDDIR) $(ISO) vnl.img
