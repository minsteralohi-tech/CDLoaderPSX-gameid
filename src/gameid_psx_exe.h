#pragma once

#include <stdbool.h>

/*
 * Look up the retail serial for an early Japanese game whose boot executable
 * is only named PSX.EXE. The input is the first 16 bytes of the ISO-9660
 * volume creation timestamp, followed by a null terminator.
 *
 * Returns the serial suffix (for example "000.01") or "0" when unknown.
 * is_scps is set when the returned serial uses the SCPS prefix; callers must
 * reset it before each lookup.
 */
const char *get_psx_exe_gameid(
	const unsigned char volume_creation_timestamp[17]
);

extern bool is_scps;
