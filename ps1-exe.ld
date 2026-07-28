/*
 * Linker script for the PS1 bare-metal CD loader.
 *
 * The loader is placed high in RAM (0x801EA300, matching tonyhax) so that:
 *   1. it does not overlap the region where PS1 game executables load
 *      (0x80010000 upward), and
 *   2. before launching a game we can zero-fill everything from 0x80010000 up
 *      to here, reproducing the clean RAM state the BIOS provides on a cold
 *      boot. Games rely on that; clearing too little leaves this loader's own
 *      code in RAM the game expects to be zeroed, which makes many games hang
 *      the instant they start.
 */

MEMORY
{
	ram (rwx) : ORIGIN = 0x801EA300, LENGTH = 0x00015C00
}

SECTIONS
{
	. = ORIGIN(ram);
	PROVIDE(__RO_START__ = .);

	.start : { KEEP(*(.start)) } > ram

	.text :
	{
		*(.text .text.*)
	} > ram

	.rodata :
	{
		*(.rodata .rodata.*)
		*(.rodata1)
	} > ram

	.data :
	{
		*(.data .data.*)
		*(.sdata .sdata.*)
	} > ram

	. = ALIGN(4);
	PROVIDE(__bss_start = .);
	.bss (NOLOAD) :
	{
		*(.sbss .sbss.*)
		*(.bss .bss.*)
		*(COMMON)
	} > ram
	. = ALIGN(4);
	PROVIDE(__bss_end = .);

	PROVIDE(__sp_top = ORIGIN(ram) + LENGTH(ram) - 16);

	/DISCARD/ :
	{
		*(.comment)
		*(.mdebug*)
		*(.reginfo)
		*(.MIPS.abiflags)
		*(.pdr)
		*(.gnu.attributes)
	}
}

ENTRY(_start)
