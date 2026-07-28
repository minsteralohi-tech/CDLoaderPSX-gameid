/*
 * Minimal crt0 for the PS1 bare-metal CD loader.
 *
 * The BIOS loads our PS-EXE, sets $gp and $sp from the executable header and
 * jumps here. We set up our own stack, zero the .bss section and call main().
 */

.set noreorder

.section .start, "ax"
.global _start
_start:
	/* Point the stack at the top of our reserved RAM region. */
	la    $sp, __sp_top
	move  $fp, $sp

	/* Zero .bss (__bss_start .. __bss_end). */
	la    $t0, __bss_start
	la    $t1, __bss_end
bss_loop:
	sltu  $t2, $t0, $t1
	beq   $t2, $zero, bss_done
	nop
	sw    $zero, 0($t0)
	j     bss_loop
	addiu $t0, $t0, 4
bss_done:

	/* Call main(). */
	jal   main
	nop

	/* Should never return; hang if it does. */
hang:
	j     hang
	nop
