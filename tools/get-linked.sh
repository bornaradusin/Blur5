#!/usr/bin/env bash
# Puts the linked projects (tools/linked-projects.txt) into a Blur5 checkout, each at the
# commit that file pins.
#
#   tools/get-linked.sh [--if-missing] ROOT [FROM] [FOLDER...]
#
# ROOT    the checkout to put them in (each goes to ROOT/<folder>)
# FROM    a folder that may already hold copies (FROM/<folder>, a git clone): a copy that has
#         the commit is cloned from instead of the internet
# FOLDER  only these (default: all six)
# --if-missing  leave a folder alone when it is already a clone that has its commit;
#         without it, each one is checked out at its commit again
set -euo pipefail
only_missing=0
[[ ${1:-} == --if-missing ]] && { only_missing=1; shift; }
root=${1:?usage: tools/get-linked.sh [--if-missing] ROOT [FROM] [FOLDER...]}
from=${2:-}
shift $(( $# >= 2 ? 2 : 1 ))
wanted=" $* "
list=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/linked-projects.txt
while read -r folder address commit; do
    [[ -z ${folder:-} || $folder == \#* ]] && continue
    [[ $wanted == "  " || $wanted == *" $folder "* ]] || continue
    [[ $commit =~ ^[0-9a-f]{40}$ ]] || { echo "$list: $folder: not a commit: $commit" >&2; exit 2; }
    target=$root/$folder
    if (( only_missing )) && [[ -d $target/.git ]] &&
        git -C "$target" cat-file -e "$commit^{commit}" 2>/dev/null; then
        continue
    fi
    source=$address
    if [[ -n $from && -d $from/$folder/.git ]] &&
        git -C "$from/$folder" cat-file -e "$commit^{commit}" 2>/dev/null; then
        source=$from/$folder
    fi
    if [[ ! -d $target/.git ]]; then
        rm -rf -- "$target"
        git clone -q --no-checkout "$source" "$target"
    fi
    git -C "$target" cat-file -e "$commit^{commit}" 2>/dev/null || git -C "$target" fetch -q "$source" "$commit"
    git -C "$target" checkout -q --force --detach "$commit"
done < "$list"
