#pragma once

#include <stdbool.h>
#include <stdint-gcc.h>

/*
 * Returns true when the boot filename is the ambiguous PSX.EXE form used by
 * early Japanese discs. Those discs require an ISO-9660 sector-16 lookup.
 */
bool game_id_needs_disc_lookup(const char *bootfile);

/*
 * Probe slot 1 for a Game-ID-aware memory card and, when one is present,
 * transmit the canonical ID for bootfile. sector_buffer must hold one CD
 * sector and may be overwritten when PSX.EXE identification is required.
 *
 * Returns true only when a compatible device accepted the Game-ID command.
 * Failure is non-fatal: the caller should continue booting the game.
 */
bool game_id_send(const char *bootfile, uint8_t *sector_buffer);
