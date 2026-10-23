/* msh, written October 2026 by Zaid Siddiqui.
 * From-scratch rewrite of university coursework; see README for the timeline. */

#include "builtins.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "parser.h"
#include "shell.h"
#include "strbuf.h"

#define DEFAULT_PATH "/usr/local/bin:/usr/bin:/bin"

static int usage_error(const char *name, const char *msg) {
    fprintf(stderr, "msh: %s: %s\n", name, msg);
    return 2;
}

char *resolve_command(Shell *sh, const char *name) {
    struct stat st;
    if (strchr(name, '/')) {
        return access(name, F_OK) == 0 ? xstrdup(name) : NULL;
    }
    const char *path = vars_get(&sh->vars, "PATH");
    if (!path) path = DEFAULT_PATH;

    StrBuf candidate;
    sb_init(&candidate);
    const char *dir = path;
    for (;;) {
        const char *colon = strchr(dir, ':');
        size_t len = colon ? (size_t)(colon - dir) : strlen(dir);
        sb_clear(&candidate);
        if (len == 0) {
            sb_putc(&candidate, '.'); /* empty entry means cwd */
        } else {
            sb_putn(&candidate, dir, len);
        }
        sb_putc(&candidate, '/');
        sb_puts(&candidate, name);
        if (stat(candidate.data, &st) == 0 && S_ISREG(st.st_mode) &&
            access(candidate.data, X_OK) == 0) {
            return sb_take(&candidate);
        }
        if (!colon) break;
        dir = colon + 1;
    }
    sb_free(&candidate);
    return NULL;
}

/* ---- directory -------------------------------------------------------- */

static int bi_cd(Shell *sh, int argc, char **argv) {
    const char *target;
    bool print_dir = false;
    if (argc > 2) return usage_error("cd", "too many arguments");
    if (argc == 1) {
        target = vars_get(&sh->vars, "HOME");
        if (!target) return usage_error("cd", "HOME not set");
    } else if (strcmp(argv[1], "-") == 0) {
        target = vars_get(&sh->vars, "OLDPWD");
        if (!target) return usage_error("cd", "OLDPWD not set");
        print_dir = true;
    } else {
        target = argv[1];
    }

    char *dest = xstrdup(target); /* target may point into vars we update */
    char old[PATH_MAX];
    bool have_old = getcwd(old, sizeof old) != NULL;

    if (chdir(dest) != 0) {
        fprintf(stderr, "msh: cd: %s: %s\n", dest, strerror(errno));
        free(dest);
        return 1;
    }
    free(dest);

    char now[PATH_MAX];
    if (have_old) vars_set(&sh->vars, "OLDPWD", old);
    if (getcwd(now, sizeof now)) {
        vars_set(&sh->vars, "PWD", now);
        if (print_dir) printf("%s\n", now);
    }
    return 0;
}

static int bi_pwd(Shell *sh, int argc, char **argv) {
    (void)sh; (void)argc; (void)argv;
    char buf[PATH_MAX];
    if (!getcwd(buf, sizeof buf)) {
        fprintf(stderr, "msh: pwd: %s\n", strerror(errno));
        return 1;
    }
    printf("%s\n", buf);
    return 0;
}

/* ---- shell control ---------------------------------------------------- */

static int bi_exit(Shell *sh, int argc, char **argv) {
    if (argc > 2) return usage_error("exit", "too many arguments");
    if (sh->interactive && !sh->warned_stopped_jobs) {
        for (size_t i = 0; i < sh->jobs.len; i++) {
            if (job_state(sh->jobs.items[i]) == JOB_STOPPED) {
                fprintf(stderr, "msh: there are stopped jobs (run exit again to quit)\n");
                sh->warned_stopped_jobs = true;
                return 1;
            }
        }
    }
    int code = sh->last_status;
    if (argc == 2) {
        char *end;
        long v = strtol(argv[1], &end, 10);
        if (*argv[1] == '\0' || *end != '\0') {
            return usage_error("exit", "numeric argument required");
        }
        code = (int)(v & 0xff);
    }
    sh->exit_requested = true;
    sh->exit_code = code;
    return code;
}

static int bi_source(Shell *sh, int argc, char **argv) {
    if (argc < 2) return usage_error(argv[0], "filename argument required");
    FILE *fp = fopen(argv[1], "r");
    if (!fp) {
        fprintf(stderr, "msh: %s: %s\n", argv[1], strerror(errno));
        return 1;
    }
    int status = shell_run_stream(sh, fp);
    fclose(fp);
    return status;
}

/* ---- variables -------------------------------------------------------- */

static int bi_export(Shell *sh, int argc, char **argv) {
    if (argc == 1) {
        char **env = vars_build_env(&sh->vars);
        for (char **e = env; *e; e++) printf("export %s\n", *e);
        vars_free_env(env);
        return 0;
    }
    int status = 0;
    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        size_t namelen = eq ? (size_t)(eq - argv[i]) : strlen(argv[i]);
        if (!is_valid_name(argv[i], namelen)) {
            fprintf(stderr, "msh: export: '%s': not a valid identifier\n", argv[i]);
            status = 1;
            continue;
        }
        char *name = xstrndup(argv[i], namelen);
        if (eq) vars_set(&sh->vars, name, eq + 1);
        vars_export(&sh->vars, name);
        free(name);
    }
    return status;
}

static int bi_unset(Shell *sh, int argc, char **argv) {
    for (int i = 1; i < argc; i++) vars_unset(&sh->vars, argv[i]);
    return 0;
}

typedef struct {
    const VarEntry **items;
    size_t len;
} VarList;

static void collect_var(const VarEntry *e, void *ctx) {
    VarList *l = ctx;
    l->items[l->len++] = e;
}

static int cmp_var(const void *a, const void *b) {
    return strcmp((*(const VarEntry *const *)a)->name, (*(const VarEntry *const *)b)->name);
}

static int bi_set(Shell *sh, int argc, char **argv) {
    (void)argc; (void)argv;
    VarList l = {xmalloc((sh->vars.count + 1) * sizeof *l.items), 0};
    vars_foreach(&sh->vars, collect_var, &l);
    qsort(l.items, l.len, sizeof *l.items, cmp_var);
    for (size_t i = 0; i < l.len; i++) printf("%s=%s\n", l.items[i]->name, l.items[i]->value);
    free(l.items);
    return 0;
}

/* ---- output ----------------------------------------------------------- */

static int bi_echo(Shell *sh, int argc, char **argv) {
    (void)sh;
    int i = 1;
    bool newline = true;
    if (argc > 1 && strcmp(argv[1], "-n") == 0) {
        newline = false;
        i = 2;
    }
    for (; i < argc; i++) {
        fputs(argv[i], stdout);
        if (i + 1 < argc) putchar(' ');
    }
    if (newline) putchar('\n');
    return 0;
}

static int bi_history(Shell *sh, int argc, char **argv) {
    (void)argc; (void)argv;
    for (size_t i = 0; i < sh->history.len; i++) {
        printf("%5zu  %s\n", i + 1, sh->history.lines[i]);
    }
    return 0;
}

static int bi_type(Shell *sh, int argc, char **argv) {
    int status = 0;
    for (int i = 1; i < argc; i++) {
        if (builtin_lookup(argv[i])) {
            printf("%s is a shell builtin\n", argv[i]);
            continue;
        }
        char *path = resolve_command(sh, argv[i]);
        if (path) {
            printf("%s is %s\n", argv[i], path);
            free(path);
        } else {
            fprintf(stderr, "msh: type: %s: not found\n", argv[i]);
            status = 1;
        }
    }
    return status;
}

/* ---- job control ------------------------------------------------------ */

/* Resolve "%n", "n" or nothing (latest job). */
static Job *find_job(Shell *sh, const char *name, int argc, char **argv) {
    Job *job;
    if (argc < 2) {
        job = jobs_latest(&sh->jobs);
    } else {
        const char *spec = argv[1][0] == '%' ? argv[1] + 1 : argv[1];
        job = jobs_find(&sh->jobs, atoi(spec));
    }
    if (!job) fprintf(stderr, "msh: %s: no such job\n", name);
    return job;
}

static void continue_job(Job *job) {
    for (size_t i = 0; i < job->nprocs; i++) job->procs[i].stopped = false;
    job->reported_stop = false;
    if (job->pgid > 0) {
        kill(-job->pgid, SIGCONT);
    } else {
        for (size_t i = 0; i < job->nprocs; i++) {
            if (!job->procs[i].done) kill(job->procs[i].pid, SIGCONT);
        }
    }
}

static int bi_jobs(Shell *sh, int argc, char **argv) {
    (void)argc; (void)argv;
    jobs_reap(&sh->jobs, false);
    for (size_t i = 0; i < sh->jobs.len; i++) jobs_print(sh->jobs.items[i], &sh->jobs);
    return 0;
}

static int bi_fg(Shell *sh, int argc, char **argv) {
    Job *job = find_job(sh, "fg", argc, argv);
    if (!job) return 1;
    printf("%s\n", job->command);
    fflush(stdout);

    if (sh->interactive && job->pgid > 0) tcsetpgrp(sh->tty_fd, job->pgid);
    continue_job(job);
    int status = job_wait(job);
    if (sh->interactive) tcsetpgrp(sh->tty_fd, sh->pgid);

    if (job_state(job) == JOB_STOPPED) {
        putchar('\n');
        jobs_print(job, &sh->jobs);
        job->reported_stop = true;
    } else {
        jobs_remove(&sh->jobs, job);
    }
    return status;
}

static int bi_bg(Shell *sh, int argc, char **argv) {
    Job *job = find_job(sh, "bg", argc, argv);
    if (!job) return 1;
    continue_job(job);
    printf("[%d] %s\n", job->id, job->command);
    return 0;
}

static int bi_wait(Shell *sh, int argc, char **argv) {
    (void)argc; (void)argv;
    int status = 0;
    while (sh->jobs.len > 0) {
        Job *job = sh->jobs.items[0];
        if (job_state(job) == JOB_STOPPED) break; /* would block forever */
        status = job_wait(job);
        jobs_remove(&sh->jobs, job);
    }
    return status;
}

static const struct {
    const char *name;
    int num;
} SIGNAL_NAMES[] = {
    {"HUP", SIGHUP},   {"INT", SIGINT},   {"QUIT", SIGQUIT}, {"KILL", SIGKILL},
    {"USR1", SIGUSR1}, {"USR2", SIGUSR2}, {"TERM", SIGTERM}, {"CONT", SIGCONT},
    {"STOP", SIGSTOP}, {"TSTP", SIGTSTP},
};

static int parse_signal(const char *s) {
    if (strncasecmp(s, "SIG", 3) == 0) s += 3;
    char *end;
    long n = strtol(s, &end, 10);
    if (*s && *end == '\0') return (n > 0 && n < NSIG) ? (int)n : -1;
    for (size_t i = 0; i < sizeof SIGNAL_NAMES / sizeof SIGNAL_NAMES[0]; i++) {
        if (strcasecmp(s, SIGNAL_NAMES[i].name) == 0) return SIGNAL_NAMES[i].num;
    }
    return -1;
}

static int bi_kill(Shell *sh, int argc, char **argv) {
    int sig = SIGTERM;
    int i = 1;
    if (argc > 1 && argv[1][0] == '-' && argv[1][1]) {
        sig = parse_signal(argv[1] + 1);
        if (sig < 0) return usage_error("kill", "invalid signal");
        i = 2;
    }
    if (i >= argc) return usage_error("kill", "usage: kill [-SIGNAL] pid | %job ...");

    int status = 0;
    for (; i < argc; i++) {
        pid_t target;
        if (argv[i][0] == '%') {
            Job *job = jobs_find(&sh->jobs, atoi(argv[i] + 1));
            if (!job) {
                fprintf(stderr, "msh: kill: %s: no such job\n", argv[i]);
                status = 1;
                continue;
            }
            target = job->pgid > 0 ? -job->pgid : job->procs[0].pid;
        } else {
            char *end;
            target = (pid_t)strtol(argv[i], &end, 10);
            if (*end != '\0' || target == 0) {
                fprintf(stderr, "msh: kill: %s: arguments must be pids or %%jobs\n", argv[i]);
                status = 1;
                continue;
            }
        }
        if (kill(target, sig) != 0) {
            fprintf(stderr, "msh: kill: %s: %s\n", argv[i], strerror(errno));
            status = 1;
        }
    }
    return status;
}

static int bi_help(Shell *sh, int argc, char **argv);

static const Builtin BUILTINS[] = {
    {"cd", bi_cd, "cd [dir | -]"},
    {"pwd", bi_pwd, "pwd"},
    {"echo", bi_echo, "echo [-n] [arg ...]"},
    {"exit", bi_exit, "exit [n]"},
    {"export", bi_export, "export [NAME[=value] ...]"},
    {"unset", bi_unset, "unset NAME ..."},
    {"set", bi_set, "set            (list all variables)"},
    {"source", bi_source, "source FILE"},
    {".", bi_source, ". FILE"},
    {"history", bi_history, "history"},
    {"type", bi_type, "type NAME ..."},
    {"jobs", bi_jobs, "jobs"},
    {"fg", bi_fg, "fg [%job]"},
    {"bg", bi_bg, "bg [%job]"},
    {"wait", bi_wait, "wait           (wait for all background jobs)"},
    {"kill", bi_kill, "kill [-SIGNAL] pid | %job ..."},
    {"help", bi_help, "help"},
};

static int bi_help(Shell *sh, int argc, char **argv) {
    (void)sh; (void)argc; (void)argv;
    puts("msh builtins:");
    for (size_t i = 0; i < sizeof BUILTINS / sizeof BUILTINS[0]; i++) {
        printf("  %s\n", BUILTINS[i].usage);
    }
    return 0;
}

const Builtin *builtin_lookup(const char *name) {
    for (size_t i = 0; i < sizeof BUILTINS / sizeof BUILTINS[0]; i++) {
        if (strcmp(BUILTINS[i].name, name) == 0) return &BUILTINS[i];
    }
    return NULL;
}
