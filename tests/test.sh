#!/bin/sh
export HOME=$(mktemp -d)

# Tests for vmd. The real ~/vmd is never touched: HOME is replaced above
# before anything runs, and the trap restores write permission before the
# temporary tree is removed (read-only test folders would block rm -rf).

set -u

BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$BASE/../.." && pwd)
if [ -x "$BASE/../vmd" ]; then
    VMD=${VMD:-$BASE/../vmd}
else
    VMD=${VMD:-$ROOT/vmd-bin/vmd}
fi
export VMD

cleanup(){
    chmod -R u+w "$HOME" 2>/dev/null
    rm -rf "$HOME"
}
trap cleanup EXIT HUP INT TERM

pass=0
fail=0

ok(){
    pass=$((pass + 1))
    printf 'ok %d - %s\n' "$((pass + fail))" "$1"
}

no(){
    fail=$((fail + 1))
    printf 'not ok %d - %s\n' "$((pass + fail))" "$1"
}

run_case(){
    if "$2"; then
        ok "$1"
    else
        no "$1"
    fi
}

# Remove the whole sandbox between cases. Read-only test folders would stop
# a plain rm -rf, so write permission is restored first.
reset(){
    chmod -R u+w "$HOME" 2>/dev/null
    rm -rf "$HOME/vmd" "$HOME/work" "$HOME/out" "$HOME/outside"
}

add_project(){
    name=$1
    ( cd "$HOME/work/$name" && "$VMD" add "$name" ) >/dev/null 2>&1
}

make_locked_tree(){
    dir=$1
    mkdir -p "$dir/locked/inner" || return 1
    printf 'x\n' > "$dir/locked/inner/file" || return 1
    chmod 555 "$dir/locked/inner" || return 1
    chmod 555 "$dir/locked" || return 1
}

# --- 1. deletion of read-only trees -----------------------------------------

case_readonly_version(){
    reset
    mkdir -p "$HOME/work/ro/locked" || return 1
    printf 'x\n' > "$HOME/work/ro/locked/file" || return 1
    chmod 555 "$HOME/work/ro/locked" || return 1
    add_project ro || return 1
    ( cd "$HOME" && "$VMD" rm ro 1 ) >/dev/null 2>&1 || return 1
    [ ! -d "$HOME/vmd/projects/vmd-ro/v1" ] || return 1
    [ -z "$(find "$HOME/vmd/projects/vmd-ro" -name '.*deleting*' 2>/dev/null)" ]
}

case_readonly_nested(){
    reset
    make_locked_tree "$HOME/work/ron" || return 1
    add_project ron || return 1
    ( cd "$HOME" && "$VMD" rm ron 1 ) >/dev/null 2>&1 || return 1
    [ ! -d "$HOME/vmd/projects/vmd-ron/v1" ]
}

case_readonly_project_all(){
    reset
    make_locked_tree "$HOME/work/roa" || return 1
    add_project roa || return 1
    ( cd "$HOME" && "$VMD" rm roa all --yes ) >/dev/null 2>&1 || return 1
    [ ! -d "$HOME/vmd/projects/vmd-roa" ]
}

case_readonly_rm_all_continues(){
    reset
    make_locked_tree "$HOME/work/all1" || return 1
    make_locked_tree "$HOME/work/all2" || return 1
    add_project all1 || return 1
    add_project all2 || return 1
    ( cd "$HOME" && "$VMD" rm all --yes ) >/dev/null 2>&1 || return 1
    [ ! -d "$HOME/vmd/projects/vmd-all1" ] || return 1
    [ ! -d "$HOME/vmd/projects/vmd-all2" ] || return 1
    [ -z "$(find "$HOME/vmd/projects" -maxdepth 1 -name '.*deleting*' 2>/dev/null)" ]
}

case_symlink_target_untouched(){
    reset
    mkdir -p "$HOME/work/lnk" "$HOME/outside" || return 1
    printf 'keep\n' > "$HOME/outside/keep" || return 1
    chmod 700 "$HOME/outside" || return 1
    ln -s "$HOME/outside" "$HOME/work/lnk/out" || return 1
    add_project lnk || return 1
    ( cd "$HOME" && "$VMD" rm lnk 1 ) >/dev/null 2>&1 || return 1
    [ "$(stat -c %a "$HOME/outside")" = "700" ] || return 1
    [ -f "$HOME/outside/keep" ]
}

# --- 2. permission decision (0777, no setuid/setgid) ------------------------

case_file_mode_0777(){
    reset
    mkdir -p "$HOME/work/perm/bin" || return 1
    printf '#!/bin/sh\n' > "$HOME/work/perm/bin/tool" || return 1
    chmod 4755 "$HOME/work/perm/bin/tool" || return 1
    add_project perm || return 1
    [ "$(stat -c %a "$HOME/vmd/projects/vmd-perm/v1/bin/tool")" = "777" ] || return 1
    [ "$(stat -c %a "$HOME/work/perm/bin/tool")" = "4755" ]
}

case_dir_mode_0777(){
    reset
    mkdir -p "$HOME/work/permd/data" || return 1
    chmod 2755 "$HOME/work/permd/data" || return 1
    add_project permd || return 1
    [ "$(stat -c %a "$HOME/vmd/projects/vmd-permd/v1/data")" = "777" ] || return 1
    [ "$(stat -c %a "$HOME/work/permd/data")" = "2755" ]
}

# --- 3. .vmdignore diagnostics ----------------------------------------------

case_ignore_bang_warns(){
    reset
    mkdir -p "$HOME/work/ign" || return 1
    printf '!\n*.o\n' > "$HOME/work/ign/.vmdignore" || return 1
    printf 'keep\n' > "$HOME/work/ign/a.c" || return 1
    printf 'drop\n' > "$HOME/work/ign/a.o" || return 1
    out=$( cd "$HOME/work/ign" && "$VMD" add ign 2>&1 >/dev/null )
    case "$out" in
        *"line 1"*"not supported"*) ;;
        *) return 1 ;;
    esac
    [ -f "$HOME/vmd/projects/vmd-ign/v1/a.c" ] || return 1
    [ ! -f "$HOME/vmd/projects/vmd-ign/v1/a.o" ]
}

case_ignore_long_line_warns(){
    reset
    mkdir -p "$HOME/work/long" || return 1
    i=0
    while [ "$i" -lt 300 ]; do
        printf 'a' >> "$HOME/work/long/.vmdignore"
        i=$((i + 1))
    done
    printf '\n' >> "$HOME/work/long/.vmdignore"
    out=$( cd "$HOME/work/long" && "$VMD" add long 2>&1 >/dev/null )
    case "$out" in
        *"line 1"*"too long"*) ;;
        *) return 1 ;;
    esac
    [ -d "$HOME/vmd/projects/vmd-long/v1" ]
}

case_ignore_limit_stops(){
    reset
    mkdir -p "$HOME/work/many" || return 1
    i=1
    while [ "$i" -le 65 ]; do
        printf 'name%d\n' "$i" >> "$HOME/work/many/.vmdignore"
        i=$((i + 1))
    done
    printf 'keep\n' > "$HOME/work/many/a.c" || return 1
    ( cd "$HOME/work/many" && "$VMD" add many ) >/dev/null 2>&1 && return 1
    [ ! -d "$HOME/vmd/projects/vmd-many/v1" ] || return 1
    [ -z "$(find "$HOME/vmd/projects/vmd-many" -name '.v1.tmp.*' 2>/dev/null)" ]
}

# --- 4. copy error reporting ------------------------------------------------

case_copy_open_error(){
    reset
    mkdir -p "$HOME/work/deny" || return 1
    printf 'secret\n' > "$HOME/work/deny/f" || return 1
    chmod 000 "$HOME/work/deny/f" || return 1
    out=$( cd "$HOME/work/deny" && "$VMD" add deny 2>&1 >/dev/null )
    status=$?
    chmod 600 "$HOME/work/deny/f" 2>/dev/null
    [ "$status" -ne 0 ] || return 1
    case "$out" in
        *"cannot open"*) ;;
        *) return 1 ;;
    esac
    [ ! -d "$HOME/vmd/projects/vmd-deny/v1" ]
}

# --- 5. index and normal behaviour still work ------------------------------

case_index_updated_after_rm(){
    reset
    mkdir -p "$HOME/work/idx" || return 1
    printf 'x\n' > "$HOME/work/idx/a" || return 1
    add_project idx || return 1
    ( cd "$HOME" && "$VMD" info idx ) >/dev/null 2>&1 || return 1
    ( cd "$HOME" && "$VMD" rm idx 1 ) >/dev/null 2>&1 || return 1
    ( cd "$HOME" && "$VMD" info idx ) >/dev/null 2>&1 && return 1
    return 0
}

case_export_works(){
    reset
    mkdir -p "$HOME/work/exp" || return 1
    printf 'x\n' > "$HOME/work/exp/a" || return 1
    add_project exp || return 1
    ( cd "$HOME" && "$VMD" imp exp 1 "$HOME/out" ) >/dev/null 2>&1 || return 1
    [ -f "$HOME/out/vmd-exp_v-1/a" ]
}

# ---------------------------------------------------------------------------

run_case "rm deletes a version with a read-only folder" case_readonly_version
run_case "rm deletes nested read-only folders" case_readonly_nested
run_case "rm <project> all deletes a read-only project" case_readonly_project_all
run_case "rm all continues past a read-only project" case_readonly_rm_all_continues
run_case "rm does not chmod a symlink target outside the tree" case_symlink_target_untouched
run_case "saved files use 0777 and lose setuid" case_file_mode_0777
run_case "saved folders use 0777 and lose setgid" case_dir_mode_0777
run_case ".vmdignore '!' warns with its line number" case_ignore_bang_warns
run_case ".vmdignore long line warns with its line number" case_ignore_long_line_warns
run_case ".vmdignore over the pattern limit stops the save" case_ignore_limit_stops
run_case "copy failure reports the file and the reason" case_copy_open_error
run_case "the index no longer lists a removed version" case_index_updated_after_rm
run_case "export still copies a stored version" case_export_works

printf '\npassed: %d  failed: %d\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
