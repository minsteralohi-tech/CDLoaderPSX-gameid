#pragma once
#include <stdbool.h>
#include <stdint-gcc.h>

/*
 * Detect the CD controller version and drive region, and (on unlockable,
 * non-Japanese consoles) run the licence backdoor so the drive will accept
 * copied / imported discs. Safe to call on modchipped or already-unlocked
 * systems and on emulators. Returns after configuring the drive.
 */
void loader_init_drive(void);

/*
 * Wait for the user to swap in the target disc, then read its SYSTEM.CNF,
 * load the referenced PS-EXE and hand control to it. Does not return on
 * success.
 */
void loader_boot_disc(void);
