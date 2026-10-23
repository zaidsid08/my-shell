#!/usr/bin/env python3
# msh, written October 2026. Rewrite of university coursework; see README.
"""Drive msh through a pseudo-terminal to check job control end to end:
Ctrl-C, Ctrl-Z, jobs, bg, fg and kill %n. Timing-based, so it is kept out
of the default `make test`; run it with `make test-tty`."""
import os
import pty
import select
import sys
import time

MSH = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "./msh")

pid, fd = pty.fork()
if pid == 0:
    os.environ["PS1"] = "PROMPT> "
    os.execv(MSH, [MSH])


def read(seconds):
    out, end = b"", time.time() + seconds
    while time.time() < end:
        ready, _, _ = select.select([fd], [], [], 0.05)
        if ready:
            try:
                out += os.read(fd, 4096)
            except OSError:
                break
    return out.decode(errors="replace")


def send(text, wait=0.8):
    os.write(fd, text.encode())
    return read(wait)


failures = 0


def check(name, output, needle):
    global failures
    ok = needle in output
    failures += not ok
    print(f"  {'ok  ' if ok else 'FAIL'} {name}")
    if not ok:
        print(f"    expected to see {needle!r} in {output!r}")


print("tty job control")
read(0.5)
send("sleep 30\n", 0.5)
send("\x03")
check("Ctrl-C kills foreground job", send("echo status=$?\n"), "status=130")

send("sleep 30\n", 0.5)
check("Ctrl-Z stops it", send("\x1a"), "Stopped  sleep 30")
check("jobs lists it", send("jobs\n"), "[1]+  Stopped  sleep 30")
send("bg\n")
check("bg resumes it", send("jobs\n"), "Running  sleep 30")
send("kill %1\n", 1.0)
check("kill %1 finishes it", send("echo next\n"), "Done")

send("cat\n", 0.4)
send("\x1a", 0.6)
send("fg\n", 0.5)
check("fg gives the terminal back", send("typed\n", 0.5), "typed\r\ntyped")
send("\x04")
check("shell survives Ctrl-C at prompt", send("\x03echo alive\n"), "alive")
send("exit\n", 0.5)

print(f"{failures} failed")
sys.exit(1 if failures else 0)
