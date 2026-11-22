#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# pawl: "PAWL Always Wants Lines". A pawl is the part of a ratchet that stops it slipping back.
#
# Coverage ratchet (docs/design/14-build-test-and-ci.md, "Coverage ratchet").
#
# Builds the `coverage` preset, runs its tests, measures line and branch coverage of LINA's own
# library sources with llvm-cov and compares both figures with tools/coverage-baseline.txt.
# Exits nonzero if either figure dropped.
#
# What is measured: files under src/ only. Third-party code is not instrumented at all, test
# code under tests/ is instrumented but filtered out here, and generated code lives in the build
# directory, outside src/.
#
# Usage: tools/pawl.sh            measure and compare
#        tools/pawl.sh --update   measure and rewrite the baseline file
#
# States of the baseline file:
#   `none`                     LINA has no library sources yet. The script passes only while
#                              no file under src/ is measured; the first measured file makes it
#                              fail until the baseline is recorded with --update.
#   `lines <covered> <total>`  and
#   `branches <covered> <total>`
#                              The stored figures. Compared as exact fractions, not as rounded
#                              percentages.
#
# A .cpp file under src/ that no test executable links is invisible to llvm-cov, which would
# let unmeasured code slip past the ratchet; such files are reported and fail the run.
#
# The ratchet works in both directions: a figure below the baseline fails, and so does a figure
# above it, until the higher figure is recorded with --update in the same commit. Otherwise the
# baseline would lag behind and a later drop would go unnoticed.
#
# Environment: LINA_JOBS (parallel build jobs, default: half the cores), LLVM_COV,
# LLVM_PROFDATA.
set -euo pipefail

repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$repo/build/coverage"
baseline="$repo/tools/coverage-baseline.txt"
llvm_cov="${LLVM_COV:-llvm-cov}"
llvm_profdata="${LLVM_PROFDATA:-llvm-profdata}"
# Half the cores by default, so a local run leaves the machine usable.
jobs="${LINA_JOBS:-$((($(nproc) + 1) / 2))}"

update=0
case "${1:-}" in
"") ;;
--update) update=1 ;;
*)
  echo "usage: $0 [--update]" >&2
  exit 2
  ;;
esac

fail() {
  echo "pawl: FAIL: $*" >&2
  exit 1
}

cd "$repo"
cmake --preset coverage
cmake --build --preset coverage -j "$jobs"

profile_dir="$build_dir/profiles"
rm -rf -- "$profile_dir"
mkdir -p -- "$profile_dir"
# %p: process id, %m: binary signature. One raw profile per process and binary.
ctest_log="$build_dir/ctest.log"
LLVM_PROFILE_FILE="$profile_dir/%p-%m.profraw" ctest --preset coverage | tee "$ctest_log"
# A skipped test measures nothing, and what is skipped depends on the machine (installed
# locales, running as root). Figures taken with skips would differ between machines.
if grep -q -- '\*\*\*Skipped' "$ctest_log"; then
  grep -- '\*\*\*Skipped' "$ctest_log" >&2
  fail "tests were skipped (listed above); coverage would depend on this machine"
fi

shopt -s nullglob
profiles=("$profile_dir"/*.profraw)
shopt -u nullglob
((${#profiles[@]} > 0)) || fail "the tests wrote no profile data"
"$llvm_profdata" merge -sparse -o "$profile_dir/merged.profdata" "${profiles[@]}"

# Written by CMake at generation time: one instrumented test executable per line.
binaries_file="$build_dir/coverage-binaries.txt"
[[ -f "$binaries_file" ]] || fail "$binaries_file is missing"
object_args=()
while IFS= read -r binary; do
  [[ -n "$binary" ]] || continue
  if ((${#object_args[@]} == 0)); then
    object_args+=("$binary")
  else
    object_args+=("-object" "$binary")
  fi
done <"$binaries_file"
((${#object_args[@]} > 0)) || fail "no instrumented test executables are registered"

lcov="$profile_dir/coverage.lcov"
"$llvm_cov" export -format=lcov -instr-profile "$profile_dir/merged.profdata" \
  "${object_args[@]}" >"$lcov"

# Sum the per-file lcov records (LF/LH: lines found/hit, BRF/BRH: branches found/hit) for
# files under src/, and list those files.
measured_files="$profile_dir/measured-files.txt"
read -r lines_hit lines_found branches_hit branches_found < <(
  awk -v prefix="$repo/src/" -v list="$measured_files" '
    BEGIN { lh = lf = bh = bf = 0; printf "" > list }
    /^SF:/ { own = (index(substr($0, 4), prefix) == 1); if (own) print substr($0, 4) > list }
    own && /^LF:/ { lf += substr($0, 4) }
    own && /^LH:/ { lh += substr($0, 4) }
    own && /^BRF:/ { bf += substr($0, 5) }
    own && /^BRH:/ { bh += substr($0, 5) }
    END { print lh, lf, bh, bf }
  ' "$lcov"
)

# The listing below runs in a process substitution, whose failure the shell does not see.
# Fail here instead if git cannot read the repository.
git -C "$repo" rev-parse --git-dir >/dev/null

unmeasured=0
while IFS= read -r -d '' source; do
  [[ -f "$repo/$source" ]] || continue
  if ! grep -qxF -- "$repo/$source" "$measured_files"; then
    echo "pawl: not linked into any test executable: $source" >&2
    unmeasured=1
  fi
done < <(git -C "$repo" ls-files -z --cached --others --exclude-standard -- 'src/*.cpp')
((unmeasured == 0)) || fail "sources under src/ are not measured (listed above)"

percent() { # covered total
  if (($2 == 0)); then
    echo "n/a"
  else
    awk -v c="$1" -v t="$2" 'BEGIN { printf "%.2f%%", 100 * c / t }'
  fi
}

echo "pawl: lines    $lines_hit/$lines_found ($(percent "$lines_hit" "$lines_found"))"
echo "pawl: branches $branches_hit/$branches_found ($(percent "$branches_hit" "$branches_found"))"

if ((update)); then
  {
    echo "# SPDX-License-Identifier: GPL-3.0-or-later"
    echo "# Coverage baseline of LINA's own sources (src/), written by"
    echo "# tools/pawl.sh --update. Format: <figure> <covered> <total>, or \`none\`"
    echo "# while LINA has no library sources. Do not edit by hand."
    if ((lines_found == 0)); then
      echo "none"
    else
      echo "lines $lines_hit $lines_found"
      echo "branches $branches_hit $branches_found"
    fi
  } >"$baseline.tmp"
  mv -- "$baseline.tmp" "$baseline"
  echo "pawl: baseline written to $baseline"
  exit 0
fi

[[ -f "$baseline" ]] || fail "$baseline is missing"
base_state=""
base_lines_hit=0 base_lines_found=0 base_branches_hit=0 base_branches_found=0
while read -r key covered total; do
  case "$key" in
  "" | "#"*) ;;
  none) base_state="none" ;;
  lines)
    base_state="figures"
    base_lines_hit="$covered" base_lines_found="$total"
    ;;
  branches)
    base_state="figures"
    base_branches_hit="$covered" base_branches_found="$total"
    ;;
  *) fail "unrecognised line in $baseline: $key" ;;
  esac
done <"$baseline"

if [[ "$base_state" == "none" ]]; then
  if ((lines_found == 0)); then
    echo "pawl: PASS (no library sources under src/ yet; nothing to compare)"
    exit 0
  fi
  fail "src/ now has measured sources but the baseline is \`none\`; record it with --update"
fi
[[ "$base_state" == "figures" ]] || fail "$baseline holds neither figures nor \`none\`"
for value in "$base_lines_hit" "$base_lines_found" "$base_branches_hit" "$base_branches_found"; do
  [[ "$value" =~ ^[0-9]+$ ]] || fail "$baseline holds a value that is not a count: '$value'"
done
((lines_found > 0)) || fail "the baseline holds figures but no file under src/ was measured"

# Fractions are compared exactly: a/b < c/d  <=>  a*d < c*b for positive denominators.
# A figure with a zero total on both sides (code without a single branch) has nothing to
# compare. A zero total on one side only means the figure appeared or vanished, which the
# baseline must record.
status=0
compare() { # name hit found base_hit base_found
  local name="$1" hit="$2" found="$3" base_hit="$4" base_found="$5"
  local now="$hit/$found ($(percent "$hit" "$found"))"
  local base="$base_hit/$base_found ($(percent "$base_hit" "$base_found"))"
  if ((found == 0 && base_found == 0)); then
    return 0
  fi
  if ((found == 0 || base_found == 0)); then
    echo "pawl: $name coverage has no counterpart: now $now, baseline $base" >&2
    status=1
  elif ((hit * base_found < base_hit * found)); then
    echo "pawl: $name coverage dropped: now $now, baseline $base" >&2
    status=1
  elif ((hit * base_found > base_hit * found)); then
    echo "pawl: $name coverage rose: now $now, baseline $base;" \
      "record it with --update in this commit" >&2
    status=1
  fi
  return 0
}
compare line "$lines_hit" "$lines_found" "$base_lines_hit" "$base_lines_found"
compare branch "$branches_hit" "$branches_found" "$base_branches_hit" "$base_branches_found"
((status == 0)) || fail "coverage differs from the baseline"
echo "pawl: PASS (baseline: lines $base_lines_hit/$base_lines_found, branches $base_branches_hit/$base_branches_found)"
