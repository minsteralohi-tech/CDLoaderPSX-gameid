#include <stdbool.h>
#include "bios.h"
#include "audio.h"
#include "screen.h"
#include "loader.h"

/*
 * PS1 bare-metal CD loader.
 *
 * A minimal loader that unlocks the CD drive and boots a burned / imported
 * disc. Built as a standard PS-EXE for modchipped, already-unlocked or
 * emulated consoles. Derived from tonyhax International (GPLv3).
 */

void main(void)
{
	/* Return the kernel to a known-good state. */
	bios_reinitialize();

	/* Silence any audio left playing by whatever launched us. */
	audio_halt();

	/* Bring up the display and text console (draws the title bar). */
	debug_init();

	debug_write("Console: %s", bios_is_ps1() ? "PS1" : "PS2");

	/* Detect and, where possible, unlock the drive. */
	loader_init_drive();

	/* Boot the disc. On success this never returns. */
	while (true) {
		loader_boot_disc();
		debug_write("");
		debug_write("Boot failed, retrying...");
	}
}
