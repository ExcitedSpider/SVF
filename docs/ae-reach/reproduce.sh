#!/usr/bin/env bash
set -euo pipefail

repo=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
kit=$(realpath "${1:?provide the path to ae-reach-eval}")
mkdir -p "${2:?provide a results directory}"
output=$(realpath "$2")
commit=$(git -C "$repo" rev-parse HEAD)
scratch=$(mktemp -d -t svf-ae-reach.XXXXXXXX)
base=a8a0f5181748059c2df2281e5e4ada63ccda3832
mkdir "$scratch/base" "$scratch/native"
git -C "$repo" archive "$base" | tar -x -C "$scratch/base"
git -C "$repo" archive "$commit" | tar -x -C "$scratch/native"
printf 'Build and run files are in %s\n' "$scratch"

podman run --rm --userns=keep-id --security-opt label=disable \
    -v "$scratch:/work:rw" -v "$kit:/e:ro" -v "$output:/results:rw" \
    localhost/svf22-z3:llvm22 bash -euc '
        for variant in base native; do
            build=/work/$variant-build
            mkdir "$build"
            touch "$build/compile_commands.json"
            cd "/work/$variant"
            cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
                -DSVF_ENABLE_ASSERTIONS=ON -DLLVM_DIR=/opt/llvm/lib/cmake/llvm
            cmake --build "$build" --target ae -j 8
        done
        python3 /work/native/docs/ae-reach/run.py --ae /work/native-build/bin/ae \
            --stock /work/base-build/bin/ae --commit "$1" --work /work/runs --output /results
    ' _ "$commit"
