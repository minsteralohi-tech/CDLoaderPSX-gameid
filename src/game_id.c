#include <stdbool.h>
#include <stddef.h>
#include <stdint-gcc.h>

#include "bios.h"
#include "game_id.h"
#include "gameid_psx_exe.h"
#include "memcardpro.h"
#include "screen.h"
#include "str.h"

/*
 * The established PS1 Game-ID command carries the boot path, normalized to:
 *
 *     cdrom:XXXX_XXX.XX;1
 *
 * MemCard Pro, MemCard Pro 2, SD2PSX and compatible firmware use this value
 * to select the virtual card for the game. The wire format and bus timing are
 * implemented by the official MemCardPro-ASM library.
 */

#define GAME_ID_BUFFER_SIZE 64
#define ISO_PVD_SECTOR 16
#define ISO_VOLUME_TIMESTAMP_OFFSET 0x32D

static char ascii_lower(char c)
{
	if (c >= 'A' && c <= 'Z') {
		return c + ('a' - 'A');
	}
	return c;
}

static const char *disc_leaf(const char *path)
{
	const char *leaf = path;

	while (*path) {
		if (*path == ':' || *path == '\\' || *path == '/') {
			leaf = path + 1;
		}
		path++;
	}

	return leaf;
}

bool game_id_needs_disc_lookup(const char *bootfile)
{
	static const char expected[] = "psx.exe";
	const char *leaf = disc_leaf(bootfile);

	for (unsigned int i = 0; i < sizeof(expected) - 1; i++) {
		if (ascii_lower(leaf[i]) != expected[i]) {
			return false;
		}
	}

	leaf += sizeof(expected) - 1;
	return *leaf == '\0' || *leaf == ';';
}

static bool append_text(
	char *dest,
	unsigned int dest_size,
	unsigned int *length,
	const char *text
)
{
	while (*text) {
		if (*length + 1 >= dest_size) {
			dest[*length] = '\0';
			return false;
		}
		dest[*length] = *text;
		(*length)++;
		text++;
	}

	dest[*length] = '\0';
	return true;
}

static bool build_from_leaf(
	const char *bootfile,
	char game_id[GAME_ID_BUFFER_SIZE]
)
{
	unsigned int length = 0;

	game_id[0] = '\0';
	return append_text(game_id, GAME_ID_BUFFER_SIZE, &length, "cdrom:") &&
	       append_text(
		       game_id,
		       GAME_ID_BUFFER_SIZE,
		       &length,
		       disc_leaf(bootfile)
	       );
}

static bool build_from_serial(
	const char *serial,
	bool serial_is_scps,
	char game_id[GAME_ID_BUFFER_SIZE]
)
{
	unsigned int length = 0;

	game_id[0] = '\0';
	return append_text(game_id, GAME_ID_BUFFER_SIZE, &length, "cdrom:") &&
	       append_text(
		       game_id,
		       GAME_ID_BUFFER_SIZE,
		       &length,
		       serial_is_scps ? "SCPS_" : "SLPS_"
	       ) &&
	       append_text(game_id, GAME_ID_BUFFER_SIZE, &length, serial) &&
	       append_text(game_id, GAME_ID_BUFFER_SIZE, &length, ";1");
}

static bool build_game_id(
	const char *bootfile,
	uint8_t *sector_buffer,
	char game_id[GAME_ID_BUFFER_SIZE]
)
{
	if (game_id_needs_disc_lookup(bootfile)) {
		if (CdReadSector(1, ISO_PVD_SECTOR, sector_buffer) == 1) {
			unsigned char timestamp[17];

			for (unsigned int i = 0; i < 16; i++) {
				timestamp[i] =
					sector_buffer[ISO_VOLUME_TIMESTAMP_OFFSET + i];
			}
			timestamp[16] = '\0';

			/* The imported lookup table only sets this flag for SCPS. */
			is_scps = false;
			const char *serial = get_psx_exe_gameid(timestamp);
			if (strcmp(serial, "0") != 0) {
				debug_write("PSX.EXE serial: %s%s",
				            is_scps ? "SCPS-" : "SLPS-",
				            serial);
				return build_from_serial(serial, is_scps, game_id);
			}

			debug_write("Unknown PSX.EXE disc; using filename ID");
		} else {
			debug_write("Game-ID sector 16 read failed");
		}
	}

	/*
	 * Directory-qualified boot paths are deliberately reduced to the leaf.
	 * Besides producing stable IDs, this prevents long paths from overflowing
	 * older Game-ID device firmware buffers.
	 */
	return build_from_leaf(bootfile, game_id);
}

bool game_id_send(const char *bootfile, uint8_t *sector_buffer)
{
	int result = MemCardPro_Ping(MCPRO_PORT_1);
	if (result != MCPRO_NO_ERROR) {
		debug_write("Game-ID device not detected");
		return false;
	}

	debug_write("Game-ID device detected in slot 1");

	char game_id[GAME_ID_BUFFER_SIZE];
	if (!build_game_id(bootfile, sector_buffer, game_id)) {
		debug_write("Game-ID path is too long");
		return false;
	}

	/*
	 * The official library requires an inter-command delay when a ping and a
	 * Game-ID command occur in the same video frame.
	 */
	MemcardPro_InterCommandDelay();
	result = MemCardPro_SendGameID(
		MCPRO_PORT_1,
		strlen(game_id),
		game_id
	);

	if (result != MCPRO_NO_ERROR) {
		debug_write("Game-ID send failed (%d)", result);
		return false;
	}

	debug_write("Game-ID sent: %s", game_id);
	return true;
}
