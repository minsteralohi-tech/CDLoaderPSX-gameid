#include <stdint-gcc.h>
#include <stdbool.h>
#include <stddef.h>
#include "loader.h"
#include "bios.h"
#include "cdrom.h"
#include "cfgparse.h"
#include "gpu.h"
#include "game_id.h"
#include "str.h"
#include "screen.h"

/*
 * PS1 bare-metal CD loader - unlock + boot logic.
 *
 * This is a trimmed-down derivative of tonyhax International's loader
 * (secondary.c), keeping only what a minimal loader needs: detect the drive,
 * run the licence backdoor to unlock it, let the user swap in a backup /
 * import disc, then read SYSTEM.CNF and boot the referenced executable.
 *
 * Everything else in tonyhax (the anti-anti-piracy database, GameShark cheat
 * engine and the various exploit entry points) has been removed. Game-ID
 * support is retained as a small, self-contained pre-boot notification.
 *
 * Original tonyhax is (C) socram8888, alex-free and contributors, GPLv3.
 */

/*
 * Disc boot mode.
 *
 * The secret unlock commands (50h..56h) leave the drive able to read the
 * currently-inserted CD-R directly, with no lid cycle. See the psx-spx ReadN
 * notes: an unlicensed disc fails with INT5 "unless you first unlock the
 * drive". So by default we just unlock and read whatever is already in the
 * drive - instant, no "open/close the lid" step. If that read fails (e.g. you
 * are booting from a separate loader disc and the real game is on another
 * disc, or the drive spun down), we fall back once to the classic swap prompt.
 *
 * Set WAIT_FOR_SWAP to 1 to force the classic "open and close the lid" swap
 * before every boot (useful only when booting from a dedicated loader disc).
 */
#ifndef WAIT_FOR_SWAP
#define WAIT_FOR_SWAP 0
#endif

/* Start of our own read-only image, provided by the linker script. */
extern uint8_t __RO_START__;

/* Where PS1 executables are normally loaded. */
static uint8_t * const user_start = (uint8_t *) 0x80010000;

/* Drive state, established by loader_init_drive(). */
static bool enable_unlock = true;
static bool bugged_setsession = false;
static bool calibrate_laser = false;
static const char * region_name = "unknown";
static const char * p5_localized = "";

/* --- drive unlock -------------------------------------------------------- */

static bool backdoor_cmd(uint_fast8_t cmd, const char * string)
{
	uint8_t cd_reply[16];

	cd_command(cmd, (const uint8_t *) string, string ? strlen(string) : 0);

	if (cd_wait_int() != 5) {
		return false;
	}

	uint_fast8_t reply_len = cd_read_reply(cd_reply);
	if (reply_len != 2) {
		return false;
	}
	if (!(cd_reply[0] & 0x01)) {
		return false;
	}
	if (cd_reply[1] != 0x40) {
		return false;
	}

	return true;
}

static bool unlock_drive(void)
{
	if (!backdoor_cmd(0x50, NULL) ||
	    !backdoor_cmd(0x51, "Licensed by") ||
	    !backdoor_cmd(0x52, "Sony") ||
	    !backdoor_cmd(0x53, "Computer") ||
	    !backdoor_cmd(0x54, "Entertainment") ||
	    !backdoor_cmd(0x55, p5_localized) ||
	    !backdoor_cmd(0x56, NULL)) {
		debug_write("Backdoor failed");
		return false;
	}

	return true;
}

void loader_init_drive(void)
{
	uint8_t cd_controller_version[4];

	debug_write("BIOS re-init");
	bios_inject_disc_error();

	debug_write("Stopping motor");
	debug_write("CD init");
	cd_drive_init();

	const int8_t version = CD_TEST_VERSION;
	cd_command(CD_CMD_TEST, (const uint8_t *) &version, 1);
	cd_wait_int();
	cd_read_reply(cd_controller_version);
	debug_write("CD BIOS: %x", *(uint32_t *) cd_controller_version);

	if (cd_controller_version[0] == 0x94) {
		/* VC0A / VC0B: no getregion, cannot be unlocked this way. */
		bugged_setsession = true;
		enable_unlock = false;
	} else if (cd_controller_version[1] == 0x05 &&
	           cd_controller_version[2] == 0x16 &&
	           cd_controller_version[0] == 0x95 &&
	           cd_controller_version[3] == 0xC1) {
		bugged_setsession = true;
	} else if (cd_controller_version[3] == 0xC2 ||
	           cd_controller_version[3] == 0xC3) {
		calibrate_laser = true;
	}

	if (enable_unlock) {
		uint8_t cd_reply[16];
		uint8_t test = CD_TEST_REGION;

		debug_write("Unlocking drive");
		cd_command(CD_CMD_TEST, &test, 1);

		if (cd_wait_int() != 3) {
			debug_write("Region read failed");
			enable_unlock = false;
			return;
		}

		int len = cd_read_reply(cd_reply);
		cd_reply[len] = 0;

		if (strcmp((char *) cd_reply, "for Europe") == 0) {
			region_name = "European";
			p5_localized = "(Europe)";
		} else if (strcmp((char *) cd_reply, "for U/C") == 0) {
			region_name = "American";
			p5_localized = "of America";
		} else if (strcmp((char *) cd_reply, "for NETEU") == 0) {
			region_name = "NetYaroze (EU)";
			p5_localized = "World wide";
		} else if (strcmp((char *) cd_reply, "for NETNA") == 0) {
			region_name = "NetYaroze (US)";
			p5_localized = "World wide";
		} else if (strcmp((char *) cd_reply, "for Japan") == 0) {
			/* Japanese drives are already region-free for JP discs. */
			enable_unlock = false;
		} else {
			debug_write("Unsupported region: %s", (char *) (cd_reply + 4));
			enable_unlock = false;
			return;
		}

		if (enable_unlock) {
			if (!unlock_drive()) {
				enable_unlock = false;
			} else {
				debug_write("Drive region: %s", region_name);
			}
		}
	}
}

/* --- disc boot ----------------------------------------------------------- */

static bool exe_file_okay(const exe_header_t * exe_header)
{
	if (strncmp(exe_header->signature, "PS-X EXE", 8)) {
		debug_write("Warning: bad EXE signature");
	}

	uint32_t masked_load = (uint32_t) exe_header->offsets.load_addr & 0xFF800000;
	if (masked_load != 0x00000000 && masked_load != 0x80000000 &&
	    masked_load != 0xA0000000) {
		debug_write("Bad load address");
		return false;
	}

	uint32_t masked_pc = (uint32_t) exe_header->offsets.initial_pc & 0xFF800003;
	if (masked_pc != 0x00000000 && masked_pc != 0x80000000 &&
	    masked_pc != 0xA0000000) {
		debug_write("Bad start address");
		return false;
	}

	return true;
}

/* Poll the drive shell/lid status until it reaches the requested state. Used
 * only by the swap fallback / WAIT_FOR_SWAP path. */
static void wait_lid_status(bool open)
{
	uint8_t cd_reply[16];
	uint8_t expected = open ? 0x10 : 0x00;
	do {
		gpu_wait_vblank();
		cd_command(CD_CMD_GETSTAT, NULL, 0);
		cd_wait_int();
		cd_read_reply(cd_reply);
	} while ((cd_reply[0] & 0x10) != expected);
}

/* Prompt for and wait through a manual disc swap (open lid, then close). */
static void wait_for_swap(void)
{
	debug_write("Insert a backup/import disc then");
	debug_write("close the lid to boot.");
	wait_lid_status(true);
	wait_lid_status(false);
}

static void re_cd_init(void)
{
	debug_write("BIOS re-init");
	bios_reinitialize();
	bios_inject_disc_error();
	debug_write("Stopping motor");
	cd_command(CD_CMD_STOP, NULL, 0);
	/*
	 * Stop gives two responses (INT3 then INT2) on success, but only ONE
	 * (INT5, error) when the drive is empty - waiting unconditionally for a
	 * second interrupt would then hang forever, since none is coming.
	 */
	if (cd_wait_int() != 5) {
		cd_wait_int();
	}
	debug_write("CD init");
	if (!CdInit()) {
		debug_write("CD init failed");
	}
}

/*
 * PS2s (and some late PS1s) mis-seek on very large jumps with 80-minute media.
 * Stepping the laser towards the target first works around it. Harmless on PS1.
 */
static void cdr_80_min_assist(uint32_t target_lba, uint8_t * data_buffer)
{
	if (target_lba > 1000) {
		for (int i = 1; i <= 5; i++) {
			CdReadSector(1, (target_lba / 6) * i, data_buffer);
		}
	}
}

/* Strip the "cdrom:" prefix (and one leading path separator) for CdGetLbn. */
static const char * disc_path(const char * bootfile)
{
	const char * p = bootfile;
	if (strncmp(p, "cdrom:", 6) == 0) {
		p += 6;
	}
	if (*p == '\\' || *p == '/') {
		p++;
	}
	return p;
}

void loader_boot_disc(void)
{
	int32_t read;

	debug_write("");

#if WAIT_FOR_SWAP
	/* Loader-disc mode: always wait for the user to swap in the target disc. */
	if (enable_unlock) {
		wait_for_swap();
	}
#endif

	/* Non-unlock (Japanese) drives: nudge the session so a swapped disc reads. */
	if (!enable_unlock) {
		int8_t session = 1;
		if (bugged_setsession) {
			session = CD_SET_SESSION_2;
			cd_command(CD_CMD_SET_SESSION, (uint8_t *) &session, 1);
			if (cd_wait_int() != 5) {
				cd_wait_int();
			}
			session = 1;
		}
		cd_command(CD_CMD_SET_SESSION, (uint8_t *) &session, 1);
		if (cd_wait_int() != 5) {
			cd_wait_int();
		}

		if (calibrate_laser) {
			uint8_t cbuf[4] = { 0x50, 0x38, 0x15, 0x0A };
			cd_command(CD_CMD_TEST, cbuf, 4);
			cd_wait_int();
		}
	}

	re_cd_init();

	/* Use the kernel's CD sector buffer as scratch (address differs PS1/PS2). */
	uint8_t * data_buffer = (uint8_t *) (bios_is_ps1() ? 0xA000B070 : 0xA000A8D0);

	/*
	 * Auto mode: try reading whatever is already in the drive first (instant
	 * boot when a disc is present - re_cd_init is now safe to call on an
	 * empty drive too, see its Stop-command handling above). Only prompt for
	 * a swap if that fails, then retry after each lid cycle until it works.
	 */
	debug_write("Reading disc");
	while (CdReadSector(1, 4, data_buffer) != 1) {
#if WAIT_FOR_SWAP
		debug_write("Failed to read sector 4");
		return;
#else
		wait_for_swap();
		re_cd_init();
		debug_write("Reading disc");
#endif
	}

	/* Region byte at 0x3C of the licence sector selects the video standard. */
	debug_write("Checking game region");
	bool game_is_pal = (data_buffer[0x3C] == 'E');
	debug_write("Game region is %s", game_is_pal ? "European" : "American");
	if (bios_is_ps1() && game_is_pal != gpu_is_pal()) {
		debug_switch_standard(game_is_pal);
	}

	uint32_t tcb = BIOS_DEFAULT_TCB;
	uint32_t event = BIOS_DEFAULT_EVCB;
	void * stacktop = BIOS_DEFAULT_STACKTOP;
	char * bootfile = "cdrom:PSX.EXE;1";
	char bootfilebuf[64];

	debug_write("Loading SYSTEM.CNF");
	uint32_t cnf_lba = CdGetLbn("SYSTEM.CNF;1");
	if (cnf_lba != 0) {
		debug_write("SYSTEM.CNF LBA: %d", cnf_lba);
		cdr_80_min_assist(cnf_lba, data_buffer);
	}

	int32_t cnf_fd = FileOpen("cdrom:SYSTEM.CNF;1", FILE_READ);
	if (cnf_fd >= 0) {
		read = FileRead(cnf_fd, data_buffer, 2048);
		FileClose(cnf_fd);
		if (read > 0) {
			data_buffer[read] = '\0';
			config_get_hex((char *) data_buffer, "TCB", &tcb);
			config_get_hex((char *) data_buffer, "EVENT", &event);
			config_get_hex((char *) data_buffer, "STACK", (uint32_t *) &stacktop);
			if (config_get_string((char *) data_buffer, "BOOT", bootfilebuf)) {
				bootfile = bootfilebuf;
			}
			debug_write(" * TCB = %x", tcb);
			debug_write(" * EVENT = %x", event);
			debug_write(" * STACK = %x", (uint32_t) stacktop);
			debug_write(" * BOOT = %s", bootfile);
		}
	} else {
		debug_write("No SYSTEM.CNF, trying PSX.EXE");
	}

	/*
	 * Early Japanese discs named only PSX.EXE need sector 16 to recover a
	 * unique retail serial. Reset the CD state first, matching Tonyhax's
	 * proven ordering. All Game-ID bus activity is followed by the existing
	 * re_cd_init() below so the game never inherits memory-card bus state.
	 */
	if (game_id_needs_disc_lookup(bootfile)) {
		re_cd_init();
	}
	game_id_send(bootfile, data_buffer);

	re_cd_init();

	debug_write("Configuring kernel");
	EnterCriticalSection();
	SetConf(event, tcb, stacktop);

	/* Clear low RAM (from the game load area up to our image). */
	debug_write("Clearing RAM");
	bzero(user_start, &__RO_START__ - user_start);

	/* Seek assist for the executable itself. */
	uint32_t exe_lba = CdGetLbn(disc_path(bootfile));
	if (exe_lba != 0) {
		debug_write("BOOTFILE LBA: %d", exe_lba);
		cdr_80_min_assist(exe_lba, data_buffer);
	}

	debug_write("Reading executable header");
	int32_t exe_fd = FileOpen(bootfile, FILE_READ);
	if (exe_fd < 0) {
		debug_write("Open error %d", GetLastError());
		return;
	}

	file_control_block_t * exe_fcb = *BIOS_FCBS + exe_fd;
	read = FileRead(exe_fd, data_buffer, 2048);
	if (read < 0) {
		debug_write("Header read error %d", exe_fcb->last_error);
		FileClose(exe_fd);
		return;
	}

	exe_header_t * exe_header = (exe_header_t *) data_buffer;
	exe_header->offsets.initial_sp_base = stacktop;
	exe_header->offsets.initial_sp_offset = 0;

	uint32_t actual_exe_size = exe_fcb->size - 2048;
	if (actual_exe_size < exe_header->offsets.load_size) {
		exe_header->offsets.load_size = actual_exe_size;
	}

	if (!exe_file_okay(exe_header)) {
		FileClose(exe_fd);
		return;
	}

	/*
	 * If the game would overlap our own image, we cannot load it manually
	 * (that would overwrite us mid-copy). Fall back to the BIOS LoadAndExecute,
	 * which loads and jumps without needing us resident.
	 */
	if ((uint8_t *) exe_header->offsets.load_addr + exe_header->offsets.load_size >=
	    &__RO_START__) {
		debug_write("Large game: using BIOS loader");
		bios_restore_disc_error();
		LoadAndExecute(bootfile, exe_header->offsets.initial_sp_base,
		               exe_header->offsets.initial_sp_offset);
		return;
	}

	debug_write("Loading executable (%d bytes @ %x)", exe_header->offsets.load_size,
	            (uint32_t) exe_header->offsets.load_addr);
	read = FileRead(exe_fd, exe_header->offsets.load_addr,
	                exe_header->offsets.load_size);
	if (read < 0) {
		debug_write("Body read error %d", exe_fcb->last_error);
		return;
	}
	FileClose(exe_fd);

	debug_write("Starting");
	bios_restore_disc_error();

	/* Games launched from a warm boot start with interrupts disabled. */
	EnterCriticalSection();
	FlushCache();
	DoExecute(&exe_header->offsets, 0, 0);
}
