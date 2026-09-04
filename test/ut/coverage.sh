#!/usr/bin/env bash
# knet UT coverage script
# Pipeline: build -> run UT -> gcovr reports -> (optional) incremental -> (optional) threshold
# Modeled on numpy pipeline: gcovr produces Cobertura XML/HTML, diff-cover does incremental coverage.
set -euo pipefail

# ========== Path locations ==========
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KNET_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"                 # CMake build dir; .gcno/.gcda live under its CMakeFiles subdir
UT_BIN="${SCRIPT_DIR}/knet_ut_BIN"              # executable output to test/ut/ root (RUNTIME_OUTPUT_DIRECTORY)
COVERAGE_OUT_DEFAULT="${BUILD_DIR}/coverage"    # default report output dir

# ========== Defaults ==========
BUILD=1
RUN=1
FORMAT="all"
OUTPUT=""
FILTER="${KNET_ROOT}/src/knet"
FAIL_UNDER=0
DIFF_BRANCH=""
JOBS=8

# ========== Env vars override defaults ==========
FORMAT="${KNET_COVERAGE_FORMAT:-${FORMAT}}"
FAIL_UNDER="${KNET_COVERAGE_FAIL_UNDER:-${FAIL_UNDER}}"
DIFF_BRANCH="${KNET_COVERAGE_DIFF_BRANCH:-${DIFF_BRANCH}}"
JOBS="${KNET_UT_BUILD_JOBS:-${JOBS}}"
OUTPUT="${KNET_COVERAGE_OUTPUT:-${OUTPUT}}"

# COVERAGE_OUT final value (env var layer)
if [ -n "${OUTPUT}" ]; then
    COVERAGE_OUT="${OUTPUT}"
else
    COVERAGE_OUT="${COVERAGE_OUT_DEFAULT}"
fi

# ========== Help ==========
print_help() {
    cat <<'EOF'
knet UT coverage script

Usage: bash test/ut/coverage.sh [options]

Options:
  --build              run build (default)
  --no-build           skip build, reuse existing UT binary and .gcno
  --run                run UT (default)
  --no-run             skip running, reuse existing .gcda
  --format FORMAT      report format: text|html|xml|all (default all)
  --output DIR         output dir, overrides default test/ut/build/coverage
  --filter PATH        gcovr --filter source path (default <root>/src/knet)
  --fail-under N       min line coverage threshold, non-zero exit if below (default 0 = off)
  --diff BRANCH        incremental coverage, compares BRANCH...HEAD
  --jobs N             make parallelism (default 8)
  -h, --help           print this help and exit

Environment variables (lower priority than CLI args):
  KNET_COVERAGE_OUTPUT         overrides --output
  KNET_COVERAGE_FORMAT         overrides --format
  KNET_COVERAGE_FAIL_UNDER    overrides --fail-under
  KNET_COVERAGE_DIFF_BRANCH   overrides --diff
  KNET_UT_BUILD_JOBS          overrides --jobs
  ASAN_OPTIONS                overrides default ASan options

Examples:
  bash test/ut/coverage.sh --no-build --no-run --format html
  bash test/ut/coverage.sh --fail-under 80
  bash test/ut/coverage.sh --diff origin/master --fail-under 90
EOF
}

# ========== Argument parsing ==========
parse_args() {
    while [ $# -gt 0 ]; do
        case "$1" in
            --build) BUILD=1 ;;
            --no-build) BUILD=0 ;;
            --run) RUN=1 ;;
            --no-run) RUN=0 ;;
            --format)
                shift
                [ $# -eq 0 ] && { echo "Error: --format requires an argument" >&2; exit 1; }
                FORMAT="$1"
                ;;
            --output)
                shift
                [ $# -eq 0 ] && { echo "Error: --output requires an argument" >&2; exit 1; }
                OUTPUT="$1"
                COVERAGE_OUT="$1"
                ;;
            --filter)
                shift
                [ $# -eq 0 ] && { echo "Error: --filter requires an argument" >&2; exit 1; }
                FILTER="$1"
                ;;
            --fail-under)
                shift
                [ $# -eq 0 ] && { echo "Error: --fail-under requires an argument" >&2; exit 1; }
                FAIL_UNDER="$1"
                ;;
            --diff)
                shift
                [ $# -eq 0 ] && { echo "Error: --diff requires an argument" >&2; exit 1; }
                DIFF_BRANCH="$1"
                ;;
            --jobs)
                shift
                [ $# -eq 0 ] && { echo "Error: --jobs requires an argument" >&2; exit 1; }
                JOBS="$1"
                ;;
            -h|--help) print_help; exit 0 ;;
            *) echo "Unknown option: $1 (use --help for usage)" >&2; exit 1 ;;
        esac
        shift
    done
}

# ========== Dependency check ==========
check_deps() {
    if ! command -v gcov >/dev/null 2>&1; then
        echo "Missing required command: gcov" >&2
        exit 1
    fi
    if ! command -v gcovr >/dev/null 2>&1; then
        echo "Missing required command: gcovr" >&2
        echo "Install with: pip install gcovr" >&2
        exit 1
    fi
    if [ -n "${DIFF_BRANCH}" ]; then
        if ! command -v diff-cover >/dev/null 2>&1; then
            echo "Missing required command: diff-cover" >&2
            echo "Install with: pip install diff-cover" >&2
            exit 1
        fi
    fi
}

# ========== Build stage ==========
run_build() {
    echo "==> Build stage: invoking test/build.sh"
    export KNET_UT_BUILD_JOBS="${JOBS}"
    bash "${SCRIPT_DIR}/../build.sh"
    if [ ! -f "${UT_BIN}" ]; then
        echo "Error: UT binary not found after build: ${UT_BIN}" >&2
        exit 1
    fi
    echo "==> Build complete"
}

# ========== Skip-build validation ==========
verify_no_build() {
    if [ ! -f "${UT_BIN}" ]; then
        echo "UT binary not found: ${UT_BIN}. Run with --build or test/build.sh first." >&2
        exit 1
    fi
    # Verify .gcno files exist under BUILD_DIR (disable pipefail so find failure isn't misjudged)
    set +o pipefail
    local gcno_count
    gcno_count=$(find "${BUILD_DIR}" -name '*.gcno' 2>/dev/null | wc -l)
    set -o pipefail
    if [ "${gcno_count}" -eq 0 ]; then
        echo ".gcno not found, run --build first" >&2
        exit 1
    fi
    echo "==> Skipping build, reusing existing UT binary and .gcno"
}

# ========== Run stage ==========
run_tests() {
    echo "==> Run stage: executing UT binary"
    # Clean stale .gcda before running (like numpy)
    find "${BUILD_DIR}" -name '*.gcda' -delete 2>/dev/null || true
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0:abort_on_error=1}"
    # Run in a subshell cd'd to test/ut/ (binary lives there), without affecting script cwd
    set +e
    ( cd "${SCRIPT_DIR}" && "${UT_BIN}" )
    local ut_status=$?
    set -e
    if [ "${ut_status}" -ne 0 ]; then
        echo "WARNING: UT binary exited with code ${ut_status}, coverage may be incomplete" >&2
    fi
    echo "==> UT run finished (exit=${ut_status})"
}

# ========== gcovr stage ==========
run_gcovr() {
    echo "==> gcovr stage: generating coverage reports"
    mkdir -p "${COVERAGE_OUT}"

    # Validate format
    case "${FORMAT}" in
        text|html|xml|all) ;;
        *) echo "Error: invalid format '${FORMAT}', valid: text|html|xml|all" >&2; exit 1 ;;
    esac

    # Build gcovr args
    local gcovr_args=()
    gcovr_args+=(
        --root "${KNET_ROOT}"
        --search-dir "${BUILD_DIR}"
        --filter "${FILTER}"
        --exclude '.*test.*'
        --exclude '.*build.*'
        --exclude '.*opensource.*'
        --exclude '.*mock.*'
        --exclude '.*common.*'
        --gcov-ignore-parse-errors negative_hits.warn_once_per_file
        --print-summary
    )

    # Determine whether xml is needed (threshold check / incremental both need coverage.xml)
    local need_xml=0
    case "${FORMAT}" in
        xml|all) need_xml=1 ;;
    esac
    if awk -v n="${FAIL_UNDER}" 'BEGIN { exit (n+0 > 0) ? 0 : 1 }'; then
        need_xml=1
    fi
    if [ -n "${DIFF_BRANCH}" ]; then
        need_xml=1
    fi

    # Append output args by format
    case "${FORMAT}" in
        text)
            gcovr_args+=(--txt "${COVERAGE_OUT}/coverage.txt")
            ;;
        html)
            gcovr_args+=(--html "${COVERAGE_OUT}/coverage.html" --html-details)
            ;;
        xml)
            gcovr_args+=(--xml "${COVERAGE_OUT}/coverage.xml" --xml-pretty)
            ;;
        all)
            gcovr_args+=(--txt "${COVERAGE_OUT}/coverage.txt")
            gcovr_args+=(--html "${COVERAGE_OUT}/coverage.html" --html-details)
            gcovr_args+=(--xml "${COVERAGE_OUT}/coverage.xml" --xml-pretty)
            ;;
    esac

    # Auto-append xml when needed by threshold/incremental but format lacks it
    if [ "${need_xml}" -eq 1 ] && [ "${FORMAT}" != "xml" ] && [ "${FORMAT}" != "all" ]; then
        gcovr_args+=(--xml "${COVERAGE_OUT}/coverage.xml" --xml-pretty)
    fi

    # Run gcovr, capture stderr (parse warnings are not fatal)
    local stderr_file
    stderr_file="$(mktemp)"
    set +e
    gcovr "${gcovr_args[@]}" 2> "${stderr_file}"
    local gcovr_status=$?
    set -e

    if [ -s "${stderr_file}" ]; then
        echo "gcovr warnings:" >&2
        cat "${stderr_file}" >&2
    fi
    rm -f "${stderr_file}"

    if [ "${gcovr_status}" -ne 0 ]; then
        echo "WARNING: gcovr exited with code ${gcovr_status}, reports may be incomplete" >&2
    fi
    echo "==> gcovr done"
}

# ========== Incremental stage ==========
run_diff() {
    [ -z "${DIFF_BRANCH}" ] && return 0

    echo "==> Incremental stage: diff-cover comparing ${DIFF_BRANCH}...HEAD"

    if [ ! -f "${COVERAGE_OUT}/coverage.xml" ]; then
        echo "Error: ${COVERAGE_OUT}/coverage.xml missing, cannot run diff-cover (ensure xml is generated)" >&2
        exit 1
    fi

    # Validate branch, try to fetch if missing (like numpy)
    if ! git -C "${KNET_ROOT}" rev-parse --verify "${DIFF_BRANCH}" >/dev/null 2>&1; then
        local branch_name="${DIFF_BRANCH#origin/}"
        echo "Local ref ${DIFF_BRANCH} not found, trying fetch origin ${branch_name} ..." >&2
        git -C "${KNET_ROOT}" fetch origin "${branch_name}" || true
    fi

    set +e
    diff-cover "${COVERAGE_OUT}/coverage.xml" \
        --compare-branch "${DIFF_BRANCH}" \
        --fail-under "${FAIL_UNDER}" \
        > "${COVERAGE_OUT}/diff-coverage.txt"
    local diff_status=$?
    set -e

    echo "==> Incremental report: ${COVERAGE_OUT}/diff-coverage.txt"
    if [ "${diff_status}" -ne 0 ]; then
        echo "WARNING: diff-cover exit code ${diff_status}, incremental coverage below threshold ${FAIL_UNDER}%" >&2
    fi
    return ${diff_status}
}

# ========== Threshold stage ==========
check_threshold() {
    # When diff is enabled, diff-cover handles threshold; skip duplicate check
    [ -n "${DIFF_BRANCH}" ] && return 0
    # Skip when FAIL_UNDER <= 0
    if ! awk -v n="${FAIL_UNDER}" 'BEGIN { exit (n+0 > 0) ? 0 : 1 }'; then
        return 0
    fi

    echo "==> Threshold stage: checking line coverage >= ${FAIL_UNDER}%"

    if [ ! -f "${COVERAGE_OUT}/coverage.xml" ]; then
        echo "Error: ${COVERAGE_OUT}/coverage.xml missing, cannot run threshold check" >&2
        return 1
    fi

    local py_bin=python3
    if ! command -v python3 >/dev/null 2>&1; then
        py_bin=python
    fi

    # Parse Cobertura XML root line-rate attribute (modeled on numpy incremental_coverage.sh)
    set +e
    "${py_bin}" - "${COVERAGE_OUT}/coverage.xml" "${FAIL_UNDER}" <<'PYEOF'
import sys, xml.etree.ElementTree as ET
xml_path, fail_under = sys.argv[1], float(sys.argv[2])
rate = float(ET.parse(xml_path).getroot().attrib['line-rate']) * 100
print(f"Line coverage: {rate:.2f}% >= {fail_under:.2f}%")
sys.exit(0 if rate >= fail_under else 1)
PYEOF
    local thresh_status=$?
    set -e

    if [ "${thresh_status}" -ne 0 ]; then
        echo "WARNING: line coverage below threshold ${FAIL_UNDER}%" >&2
    fi
    return ${thresh_status}
}

# ========== Main flow ==========
main() {
    parse_args "$@"
    check_deps

    if [ "${BUILD}" -eq 1 ]; then
        run_build
    else
        verify_no_build
    fi

    if [ "${RUN}" -eq 1 ]; then
        run_tests
    fi

    run_gcovr

    local final_status=0

    # Incremental coverage (diff-cover already does threshold judgement)
    run_diff || final_status=$?

    # Standalone threshold check when diff is not enabled
    if [ -z "${DIFF_BRANCH}" ]; then
        check_threshold || final_status=$?
    fi

    echo ""
    echo "Coverage reports generated under: ${COVERAGE_OUT}"
    if [ -d "${COVERAGE_OUT}" ]; then
        ls -1 "${COVERAGE_OUT}" 2>/dev/null | sed 's/^/  /' || true
    fi

    echo "Final exit code: ${final_status}"
    exit "${final_status}"
}

main "$@"
