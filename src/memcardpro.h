#pragma once

/*
 * MemCard Pro / compatible Game-ID protocol entry points.
 *
 * The implementation is the Cybdyn Systems MemCardPro-ASM library, licensed
 * under Apache-2.0. See third_party/memcardpro/LICENSE.
 */

#define MCPRO_PORT_1 0
#define MCPRO_PORT_2 1

enum {
	MCPRO_NO_ERROR = 0,
	MCPRO_BUS_SELECT_FAIL = 1,
	MCPRO_OPERATION_FAILED = 2,
	MCPRO_RESERVED_FAIL = 3,
	MCPRO_LENGTH_OR_SUCCESS_FAIL = 4,
	MCPRO_DATA_TRANSMISSION_FAIL = 5
};

int MemCardPro_Ping(int portnum);
int MemCardPro_SendGameID(int portnum, int length, const char *game_id);
int MemcardPro_InterCommandDelay(void);
