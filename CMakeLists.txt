cmake_minimum_required(VERSION 3.16)

# Use the MIPS toolchain file. Configure with:
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mipsel.cmake
# then:
#   cmake --build build
#
# The Makefile is the simplest way to build; this file is provided for parity
# with ps1-bare-metal-style projects.

project(ps1-cdloader C ASM)

set(CMAKE_C_STANDARD 11)

file(GLOB SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.c" "${CMAKE_SOURCE_DIR}/src/*.S")

add_link_options(
	-nostdlib
	-Wl,-T,${CMAKE_SOURCE_DIR}/ps1-exe.ld
	-Wl,--gc-sections
	-Wl,-Map,${CMAKE_BINARY_DIR}/cdloader.map
)

add_compile_options(-Os -fomit-frame-pointer -Wall -Wextra -Wno-unused-parameter)

add_executable(cdloader.elf ${SOURCES})
target_link_libraries(cdloader.elf gcc)

set_target_properties(cdloader.elf PROPERTIES LINK_DEPENDS ${CMAKE_SOURCE_DIR}/ps1-exe.ld)

find_program(PYTHON3 NAMES python3 python REQUIRED)

add_custom_command(
	OUTPUT ${CMAKE_BINARY_DIR}/cdloader.exe
	COMMAND ${PYTHON3} ${CMAKE_SOURCE_DIR}/tools/elf2psexe.py
	        $<TARGET_FILE:cdloader.elf> ${CMAKE_BINARY_DIR}/cdloader.exe
	DEPENDS cdloader.elf
	COMMENT "Converting ELF to PS-EXE"
)

add_custom_target(cdloader ALL DEPENDS ${CMAKE_BINARY_DIR}/cdloader.exe)
