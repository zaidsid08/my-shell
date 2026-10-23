# msh: a Unix shell in C

> **Timeline**
> | | |
> |---|---|
> | **Original coursework** | University of Toronto, systems programming course (exact term to be added) |
> | **This public rewrite** | October 2026 |
>
> I first learned this material by building a shell for a university assignment. That code stays private because it's an assessed solution. Everything in this repository was written from scratch in October 2026, with a new design, new code, and tests. The skills are the same ones I picked up the first time; the implementation is not.

`msh` is a small POSIX-style shell. It runs pipelines of any length, handles redirections, quoting, variable and command substitution, and globbing, and does real job control: Ctrl-Z, `fg`, `bg`, `jobs`, `kill %1`. It can also open TCP connections through `/dev/tcp`, the same way bash does.

I wrote it to understand what actually happens between pressing Enter and a program running: `fork`, `exec`, file descriptors, process groups, and who gets the terminal.

```
msh:~/code$ printf "pear\napple\nfig\n" | sort | head -n 2 | tr a-z A-Z
APPLE
FIG
msh:~/code$ echo "built on $(uname -s) by $USER" > note.txt; cat < note.txt
built on Linux by zaid
msh:~/code$ sleep 60
^Z
[1]+  Stopped  sleep 60
msh:~/code$ bg
[1] sleep 60
msh:~/code$ kill %1
msh:~/code$ echo "GET / HTTP/1.0" > /dev/tcp/localhost/8080
```

## Features

- **Pipelines** of any length (`a | b | c | d`). Each stage is its own process, joined by `pipe(2)` and `dup2(2)`.
- **Redirection**: `<`, `>`, `>>`, `<>`, fd prefixes (`2> err`), and fd duplication (`2>&1`, `>&-`).
- **Control operators**: `;`, `&&`, `||`, and `&` for background jobs.
- **Quoting**: single quotes, double quotes, and backslash escapes, all handled properly.
- **Expansion**: `$VAR`, `${VAR}`, `${VAR:-default}`, `$?`, `$$`, `$!`, `$#`, `$0`–`$9`, `$@`, `~`, command substitution `$(...)` (nesting works), and glob patterns (`*.c`).
- **Job control**: every pipeline gets its own process group. The shell hands the terminal to the foreground job with `tcsetpgrp`, so Ctrl-C and Ctrl-Z reach the job, not the shell. `jobs`, `fg`, `bg`, `wait` and `kill %n` all work.
- **`/dev/tcp/HOST/PORT`**: redirecting to this path opens a TCP socket (via `getaddrinfo` + `connect`) instead of a file.
- **Builtins**: `cd` (including `cd -`), `pwd`, `echo`, `export`, `unset`, `set`, `source`/`.`, `type`, `history`, `jobs`, `fg`, `bg`, `wait`, `kill`, `help`, `exit`.
- **Three ways to run**: interactive REPL, `msh script.sh args...`, or `msh -c 'command'`.

## Build and run

You need a C11 compiler and `make` on Linux or another POSIX system. There are no other dependencies.

```sh
git clone https://github.com/zaidsid08/my-shell.git
cd my-shell
make            # produces ./msh
./msh           # interactive
./msh -c 'echo hi | tr a-z A-Z'
./msh myscript.sh arg1 arg2
```

## Tests

```sh
make test       # unit tests + end-to-end tests
make debug      # same suite, rebuilt with AddressSanitizer and UBSan
make test-tty   # job control through a pseudo-terminal (needs python3)
```

- **Unit tests** (`tests/unit/`) cover the lexer, parser, variable table and expander. They use a tiny single-header assertion helper in place of a test framework.
- **Integration tests** (`tests/integration/run.sh`) run about 45 real command lines through `msh -c`. They check stdout and exit codes, covering pipes, redirections, jobs, scripts, and a `/dev/tcp` round trip against a throwaway Python listener.
- CI runs the whole suite on every push with both gcc and clang, including the sanitizer build.

Job control can't be tested without a real terminal. `make test-tty` covers it by driving `msh` through a pseudo-terminal: Ctrl-C, Ctrl-Z, `jobs`, `bg`, `fg`, and `kill %1`.

## How it works

A line goes through four stages. Each one lives in its own module.

```
"ls *.c | wc -l > n"
      │
      ▼  lexer.c      tokens, with quotes left in place
[ls] [*.c] [|] [wc] [-l] [>] [n]
      │
      ▼  parser.c     list → and-or → pipeline → command (+ redirects)
AndOr{ Pipeline{ Command{ls *.c}, Command{wc -l, >n} } }
      │
      ▼  expand.c     at run time: $vars, $(…), ~, quote removal, glob
argv = ["ls", "a.c", "b.c"]
      │
      ▼  exec.c       fork per stage, pipe/dup2, process group, wait
```

| File | What it does |
|---|---|
| `lexer.c` | Splits input into words and operators. It keeps quotes in place and only checks that they're balanced. Expansion has to wait until execution, otherwise `x=1; echo $x` would expand `$x` before `x` exists. |
| `parser.c` | A recursive-descent parser for `list := and_or ((';'|'&') and_or)*`, down to individual commands and redirections. It produces a small AST. |
| `expand.c` | Builds two strings in one pass: the literal value, and a glob pattern in which every quoted character is escaped. That's how `"*.c"` stays literal while `*.c` expands. `$(...)` forks a copy of the shell with stdout connected to a pipe. |
| `exec.c` | Runs the AST. A builtin on its own runs inside the shell process, so `cd` and `export` stick. Anything else forks one child per pipeline stage. Background jobs fork a subshell that becomes a new process group, so `a && b &` is a single job. |
| `redirect.c` | Opens files or TCP sockets and `dup2`s them into place. For builtins it first saves the original fds (to fds ≥ 10) and restores them afterwards. |
| `jobs.c` | The job table. It records `waitpid` results, works out running/stopped/done, and reaps finished background jobs before each prompt. |
| `vars.c` | Shell variables in an FNV-1a hash table with separate chaining, resized at 0.75 load. Exported variables become the child's `envp`. |
| `signals.c` | The interactive shell ignores `SIGTSTP`/`SIGTTOU`/`SIGTTIN` and catches `SIGINT` without `SA_RESTART`, so Ctrl-C at the prompt clears the line. Children reset everything to default before running. |
| `shell.c`, `main.c` | Interpreter state (one `Shell` struct, no hidden globals), the REPL, script mode, and `-c` mode. |

### Decisions worth explaining

- **The parent waits on specific pids, not `waitpid(-1)`.** A foreground job waits only for its own processes, so a background job finishing in the middle can't be mistaken for the foreground one. Background jobs are reaped with a non-blocking `waitpid(-1, WNOHANG | WUNTRACED | WCONTINUED)` before each prompt.
- **`setpgid` happens in both parent and child.** Whichever runs first after `fork` wins the race, so the process group exists before the shell calls `tcsetpgrp` or the child calls `exec`.
- **There's no SIGCHLD handler.** A handler would only set a flag that the loop checks anyway. Reaping at the prompt is simpler and avoids async-signal-safety problems.
- **Expansion results aren't split into fields.** In POSIX sh, `$x` containing spaces becomes several arguments. I left that out on purpose: it makes the rules easier to predict and removes a whole class of quoting bugs. It's the main known difference from `sh`.

## Limitations

These are deliberately out of scope for now: `if`/`while`/`for`, functions, subshells with `( )`, here-docs, field splitting, arithmetic `$(( ))`, and line editing.

## Possible next steps

- Here-documents (`<<EOF`) built on a pipe or a temp file
- `if`/`while`, using the existing AST with a new node type
- Line editing and persistent history via raw-mode termios
- A `trap` builtin for running commands on signals

## License

MIT
