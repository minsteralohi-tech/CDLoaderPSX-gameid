#pragma once
#include <stdbool.h>

/*
 * On-screen text console for the PS1 bare-metal CD loader.
 *
 * Layout mirrors tonyhax's screen: a title bar (loader name in a large font,
 * subtitle underneath) with a divider line, then a scrolling log area below
 * it for step-by-step status messages.
 */

/* Bring up the GPU, video mode and draw the title bar. Call once at start. */
void debug_init(void);

/* Print one formatted line (printf-subset: %s %d %x %c) to the scrolling log
 * area below the title bar. Scrolls when full. */
void debug_write(const char * format, ...);

/* Switch the video standard between NTSC (false) and PAL (true) and redraw. */
void debug_switch_standard(bool pal);
