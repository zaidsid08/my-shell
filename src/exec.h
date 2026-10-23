/* msh, written October 2026 by Zaid Siddiqui.
 * From-scratch rewrite of university coursework; see README for the timeline. */

#ifndef MSH_EXEC_H
#define MSH_EXEC_H

#include "parser.h"

struct Shell;

/* Execute a parsed command list. Returns the final $?. */
int exec_list(struct Shell *sh, const CommandList *list);

#endif
