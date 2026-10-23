#!/usr/bin/env bash
# msh, written October 2026. Rewrite of university coursework; see README.
# End-to-end tests: run msh -c '...' and compare stdout (and exit status).
# Usage: tests/integration/run.sh path/to/msh
set -u

MSH="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cd "$WORK" || exit 1

pass=0
fail=0

# expect NAME EXPECTED_STDOUT SCRIPT [EXPECTED_STATUS]
expect() {
    local name="$1" want="$2" script="$3" want_status="${4:-}"
    local got status
    got="$("$MSH" -c "$script" 2>/dev/null)"
    status=$?
    if [[ "$got" != "$want" ]]; then
        fail=$((fail + 1))
        printf '  FAIL %s\n    script:   %s\n    expected: %q\n    got:      %q\n' \
            "$name" "$script" "$want" "$got"
    elif [[ -n "$want_status" && "$status" != "$want_status" ]]; then
        fail=$((fail + 1))
        printf '  FAIL %s: exit status %s, expected %s\n' "$name" "$status" "$want_status"
    else
        pass=$((pass + 1))
        printf '  ok   %s\n' "$name"
    fi
}

echo "integration"

# --- basics ---------------------------------------------------------------
expect "echo"               "a b c"          'echo a   b c'
expect "echo -n"            "x"              'echo -n x'
expect "quoting"            'a  b $HOME c'   "echo \"a  b\" '\$HOME' c"
expect "external command"   "hello"          'printf "%s\n" hello'
expect "command not found"  "127"            'definitely_not_a_cmd; echo $?'
expect "exit status"        ""               'exit 7' 7

# --- variables and expansion ----------------------------------------------
expect "assignment"         "v=1"            'x=1; echo v=$x'
expect "default value"      "fallback"       'echo ${nope:-fallback}'
expect "prefix assignment"  "bar|"           'FOO=bar sh -c "printf \$FOO"; echo "|$FOO"'
expect "export"             "1"              'export A=1; sh -c "echo \$A"'
expect "unset"              "[]"             'x=1; unset x; echo "[$x]"'
expect "command subst"      "n=3"            'echo n=$(printf "a\nb\nc\n" | wc -l)'
expect "nested subst"       "inner"          'echo $(echo $(echo inner))'
expect "glob"               "a.g b.g"        'touch b.g a.g; echo *.g'
expect "quoted glob"        "*.g"            'touch a.g; echo "*.g"'
expect "status var"         "1 0"            'false; a=$?; true; echo $a $?'

# --- control operators ----------------------------------------------------
expect "and"                "yes"            'true && echo yes'
expect "and short-circuit"  ""               'false && echo no'
expect "or"                 "fallback"       'false || echo fallback'
expect "and-or chain"       "c"              'false && echo a || echo c'
expect "sequence"           "1
2"                                           'echo 1; echo 2'

# --- pipes ----------------------------------------------------------------
expect "two-stage pipe"     "3"              'printf "a\nb\nc\n" | wc -l | tr -d " "'
expect "five-stage pipe"    "C"              'printf "c\nb\na\n" | sort | tail -n 1 | tr a-z A-Z | cat'
expect "pipe status"        "0"              'false | true; echo $?'
expect "builtin in pipe"    "HI"             'echo hi | tr a-z A-Z'
expect "large pipe"         "200000"         'seq 1 200000 | cat | wc -l | tr -d " "'

# --- redirection ----------------------------------------------------------
expect "redirect out"       "data"           'echo data > f; cat f'
expect "append"             "1
2"                                           'echo 1 > f; echo 2 >> f; cat < f'
expect "stderr to file"     "1"              'ls /nonexistent 2> err; wc -l < err | tr -d " "'
expect "dup 2>&1"           "1"              'ls /nonexistent 2>&1 | wc -l | tr -d " "'
expect "builtin redirect"   "kept"           'echo kept > g; echo visible >/dev/null; cat g'
expect "bad input file"     "1"              'cat < missing_file; echo $?'

# --- builtins -------------------------------------------------------------
expect "cd and pwd"         "/tmp"           'cd /tmp && pwd'
expect "cd -"               "/
/tmp"                                        'cd /tmp; cd /; pwd; cd - >/dev/null; pwd'
expect "type"               "cd is a shell builtin"  'type cd'
expect "source"             "sourced"        'echo "echo sourced" > s.msh; source s.msh'

# --- jobs -----------------------------------------------------------------
expect "background + wait"  "after
bg"                                          'sleep 0.2 && echo bg & echo after; wait'
expect "\$! is set"         "yes"            'sleep 0.1 & [ -n "$!" ] && echo yes; wait'
expect "kill %job"          "143"            'sleep 5 & kill %1; wait; echo $?'
expect "jobs listing"       "[1]+  Running  sleep 1 &"  'sleep 1 & jobs; kill %1'

# --- /dev/tcp -------------------------------------------------------------
if command -v python3 >/dev/null; then
    python3 - <<'PY' &
import socket
s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("127.0.0.1", 0)); s.listen(1)
open("port", "w").write(str(s.getsockname()[1]))
c, _ = s.accept()
data = b""
while chunk := c.recv(1024):
    data += chunk
open("received", "wb").write(data)
PY
    for _ in $(seq 50); do [[ -s port ]] && break; sleep 0.05; done
    port="$(cat port)"
    "$MSH" -c "echo over-tcp > /dev/tcp/127.0.0.1/$port"
    wait
    expect "/dev/tcp redirect" "over-tcp"    'cat received'
    expect "/dev/tcp refused"  "1"           'echo x > /dev/tcp/127.0.0.1/1; echo $?'
fi

# --- scripts --------------------------------------------------------------
printf 'echo "$0 got $# args: $1 $2"\nexit 4\necho unreachable\n' > script.msh
got="$("$MSH" script.msh a b)"; status=$?
if [[ "$got" == "script.msh got 2 args: a b" && $status == 4 ]]; then
    pass=$((pass + 1)); echo "  ok   script mode"
else
    fail=$((fail + 1)); echo "  FAIL script mode: $got (status $status)"
fi

# --- syntax errors --------------------------------------------------------
expect "syntax error status" "" '| nope' 2
expect "unterminated quote"  "" 'echo "open' 2

echo "$pass passed, $fail failed"
[[ $fail -eq 0 ]]
