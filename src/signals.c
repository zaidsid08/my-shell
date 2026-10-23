/* msh, written October 2026 by Zaid Siddiqui.
 * From-scratch rewrite of university coursework; see README for the timeline. */

#include "signals.h"

#include <string.h>

volatile sig_atomic_t g_interrupted = 0;

static void on_sigint(int sig) {
    (void)sig;
    g_interrupted = 1;
}

static void set_handler(int sig, void (*handler)(int), int flags) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = handler;
    sa.sa_flags = flags;
    sigemptyset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}

void signals_setup_interactive(void) {
    /* No SA_RESTART: we want getline() to return EINTR on Ctrl-C. */
    set_handler(SIGINT, on_sigint, 0);
    set_handler(SIGQUIT, SIG_IGN, 0);
    set_handler(SIGTSTP, SIG_IGN, 0);
    set_handler(SIGTTIN, SIG_IGN, 0);
    set_handler(SIGTTOU, SIG_IGN, 0);
}

void signals_reset_defaults(void) {
    int sigs[] = {SIGINT, SIGQUIT, SIGTSTP, SIGTTIN, SIGTTOU, SIGPIPE};
    for (size_t i = 0; i < sizeof sigs / sizeof sigs[0]; i++) {
        set_handler(sigs[i], SIG_DFL, 0);
    }
}
