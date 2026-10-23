/* msh, written October 2026 by Zaid Siddiqui.
 * From-scratch rewrite of university coursework; see README for the timeline. */

#include "jobs.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include "strbuf.h"

Job *job_new(const char *command) {
    Job *job = xmalloc(sizeof *job);
    memset(job, 0, sizeof *job);
    job->command = xstrdup(command ? command : "");
    return job;
}

void job_add_proc(Job *job, pid_t pid) {
    job->procs = xrealloc(job->procs, (job->nprocs + 1) * sizeof *job->procs);
    job->procs[job->nprocs++] = (Proc){.pid = pid};
}

void job_free(Job *job) {
    if (!job) return;
    free(job->procs);
    free(job->command);
    free(job);
}

JobState job_state(const Job *job) {
    bool any_stopped = false;
    for (size_t i = 0; i < job->nprocs; i++) {
        const Proc *p = &job->procs[i];
        if (!p->done && !p->stopped) return JOB_RUNNING;
        if (p->stopped) any_stopped = true;
    }
    return any_stopped ? JOB_STOPPED : JOB_DONE;
}

int status_to_exit_code(int status) {
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    if (WIFSTOPPED(status)) return 128 + WSTOPSIG(status);
    return 1;
}

int job_exit_status(const Job *job) {
    if (job->nprocs == 0) return 0;
    return status_to_exit_code(job->procs[job->nprocs - 1].status);
}

bool job_record_status(Job *job, pid_t pid, int status) {
    for (size_t i = 0; i < job->nprocs; i++) {
        Proc *p = &job->procs[i];
        if (p->pid != pid) continue;
        if (WIFSTOPPED(status)) {
            p->stopped = true;
        } else if (WIFCONTINUED(status)) {
            p->stopped = false;
        } else {
            p->done = true;
            p->stopped = false;
            p->status = status;
        }
        return true;
    }
    return false;
}

static void report_signal_death(int status) {
    if (!WIFSIGNALED(status)) return;
    int sig = WTERMSIG(status);
    if (sig != SIGINT && sig != SIGPIPE) {
        fprintf(stderr, "%s%s\n", strsignal(sig), WCOREDUMP(status) ? " (core dumped)" : "");
    }
}

int job_wait(Job *job) {
    for (size_t i = 0; i < job->nprocs; i++) {
        Proc *p = &job->procs[i];
        while (!p->done && !p->stopped) {
            int status;
            pid_t r = waitpid(p->pid, &status, WUNTRACED);
            if (r < 0) {
                if (errno == EINTR) continue;
                p->done = true; /* already reaped elsewhere; treat as gone */
                break;
            }
            job_record_status(job, r, status);
        }
    }
    if (job_state(job) == JOB_STOPPED) {
        return 128 + SIGTSTP;
    }
    report_signal_death(job->procs[job->nprocs - 1].status);
    return job_exit_status(job);
}

void jobs_add(JobTable *t, Job *job) {
    int id = 1;
    for (size_t i = 0; i < t->len; i++) {
        if (t->items[i]->id >= id) id = t->items[i]->id + 1;
    }
    job->id = id;
    t->items = xrealloc(t->items, (t->len + 1) * sizeof *t->items);
    t->items[t->len++] = job;
}

void jobs_remove(JobTable *t, Job *job) {
    for (size_t i = 0; i < t->len; i++) {
        if (t->items[i] == job) {
            memmove(&t->items[i], &t->items[i + 1], (t->len - i - 1) * sizeof *t->items);
            t->len--;
            job_free(job);
            return;
        }
    }
}

Job *jobs_find(const JobTable *t, int id) {
    for (size_t i = 0; i < t->len; i++) {
        if (t->items[i]->id == id) return t->items[i];
    }
    return NULL;
}

Job *jobs_latest(const JobTable *t) {
    return t->len ? t->items[t->len - 1] : NULL;
}

void jobs_free_all(JobTable *t) {
    for (size_t i = 0; i < t->len; i++) job_free(t->items[i]);
    free(t->items);
    t->items = NULL;
    t->len = 0;
}

static const char *state_name(const Job *job) {
    switch (job_state(job)) {
    case JOB_RUNNING: return "Running";
    case JOB_STOPPED: return "Stopped";
    case JOB_DONE: return "Done";
    }
    return "?";
}

void jobs_print(const Job *job, const JobTable *t) {
    char marker = (job == jobs_latest(t)) ? '+' : ' ';
    printf("[%d]%c  %-8s %s\n", job->id, marker, state_name(job), job->command);
    fflush(stdout);
}

void jobs_reap(JobTable *t, bool notify) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        for (size_t i = 0; i < t->len; i++) {
            if (job_record_status(t->items[i], pid, status)) break;
        }
    }

    for (size_t i = 0; i < t->len;) {
        Job *job = t->items[i];
        JobState st = job_state(job);
        if (st == JOB_DONE) {
            if (notify) jobs_print(job, t);
            jobs_remove(t, job);
            continue;
        }
        if (st == JOB_STOPPED && !job->reported_stop) {
            if (notify) jobs_print(job, t);
            job->reported_stop = true;
        } else if (st == JOB_RUNNING) {
            job->reported_stop = false;
        }
        i++;
    }
}
