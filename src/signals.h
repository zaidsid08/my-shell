/* msh, written October 2026 by Zaid Siddiqui.
 * From-scratch rewrite of university coursework; see README for the timeline. */

#ifndef MSH_SIGNALS_H
#define MSH_SIGNALS_H

#include <signal.h>

/* Set by the SIGINT handler while the interactive shell is at its prompt. */
extern volatile sig_atomic_t g_interrupted;

/* Interactive shells ignore job-control signals and catch SIGINT so that
 * Ctrl-C clears the current line instead of killing the shell. */
void signals_setup_interactive(void);

/* Restore default dispositions; called in children before exec. */
void signals_reset_defaults(void);

#endif
