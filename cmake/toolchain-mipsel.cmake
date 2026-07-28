# CMake toolchain file for the PS1 (R3000A, little-endian MIPS-I, freestanding).
#
# Override the toolchain prefix with -DTOOLCHAIN_PREFIX=... if needed
# (e.g. mipsel-none-elf-). Defaults to the Ubuntu package prefix.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR mipsel)

if(NOT DEFINED TOOLCHAIN_PREFIX)
	set(TOOLCHAIN_PREFIX mipsel-linux-gnu-)
endif()

set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}gcc)

set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_ASM_COMPILER_WORKS 1)

set(PS1_ARCH_FLAGS
	"-march=mips1 -mabi=32 -EL -G0 -mno-abicalls -fno-pic -mno-gpopt -msoft-float")

set(CMAKE_C_FLAGS_INIT
	"${PS1_ARCH_FLAGS} -nostdlib -ffreestanding -fno-builtin -fno-stack-protector -ffunction-sections -fdata-sections")
set(CMAKE_ASM_FLAGS_INIT "${PS1_ARCH_FLAGS}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
