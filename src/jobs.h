/* msh, written October 2026 by Zaid Siddiqui.
 * From-scratch rewrite of university coursework; see README for the timeline. */

#ifndef MSH_JOBS_H
#define MSH_JOBS_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

/* One process belonging to a job (a job is a pipeline or a background
 * subshell). */
typedef struct {
    pid_t pid;
    int status;   /* raw status from waitpid, valid once done */
    bool done;
    bool stopped;
} Proc;

typedef enum { JOB_RUNNING, JOB_STOPPED, JOB_DONE } JobState;

typedef struct {
    int id;          /* the n in %n */
    pid_t pgid;      /* process group, 0 if the job has none */
    Proc *procs;
    size_t nprocs;
    char *command;   /* text shown by `jobs` */
    bool reported_stop;
} Job;

typedef struct {
    Job **items;
    size_t len;
} JobTable;

Job *job_new(const char *command);
void job_add_proc(Job *job, pid_t pid);
void job_free(Job *job);
JobState job_state(const Job *job);

/* Exit status of the job's last process, shell-style (128+sig if killed). */
int job_exit_status(const Job *job);

/* Record a waitpid() result. Returns false if pid isn't part of the job. */
bool job_record_status(Job *job, pid_t pid, int status);

/* Block until every process in job has exited or one has stopped.
 * Returns the job's exit status (or 128+SIGTSTP when stopped). */
int job_wait(Job *job);

/* Add job to the table, assigning it the next free id. */
void jobs_add(JobTable *t, Job *job);
void jobs_remove(JobTable *t, Job *job);
Job *jobs_find(const JobTable *t, int id);
Job *jobs_latest(const JobTable *t);
void jobs_free_all(JobTable *t);

/* Collect status changes without blocking. When notify is set, print
 * "Done"/"Stopped" lines for finished or newly stopped jobs. Finished jobs
 * are removed from the table. */
void jobs_reap(JobTable *t, bool notify);

void jobs_print(const Job *job, const JobTable *t);
int status_to_exit_code(int status);

#endif
