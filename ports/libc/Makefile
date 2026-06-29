CC         := clang -target x86_64-elf
AR         := llvm-ar
CFLAGS     := -std=gnu11 -O2 -ffreestanding -I../sysroot/usr/include -Wall -Wextra -fno-stack-protector -fno-builtin -fno-pic

TARGET     := ../sysroot/usr/lib/libc.a
OBJ        := libc.o

all: $(TARGET)

$(TARGET): $(OBJ)
	@mkdir -p ../sysroot/usr/lib
	$(AR) rcs $@ $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)
