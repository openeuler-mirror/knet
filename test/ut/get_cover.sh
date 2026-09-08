#!/usr/bin/env bash
# get_cover.sh - generate coverage report after running UT
# Prerequisite: ./knet_ut_BIN has run and .gcda files exist
# Usage: bash test/ut/get_cover.sh [options]
#   --format text|html|xml|all   report format (default all)
#   --build-dir DIR              build dir containing .gcda (default: auto-detect)
#   --output DIR                 report output dir (default: <script>/build/coverage)
#   --filter PATH                gcovr source filter path (default <root>/src/knet)
#   --diff BRANCH                incremental coverage: added lines in BRANCH...HEAD
#   --fail-under N               min incremental line coverage % (default 80)
#   --full-fail-under N          min full line coverage % (default 70)
#   -h, --help
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

FORMAT="all"
FILTER=""
BUILD_DIR="${KNET_BUILD_DIR:-}"
OUTPUT="${KNET_COVERAGE_OUTPUT:-}"
DIFF_BRANCH="${KNET_COVERAGE_DIFF_BRANCH:-}"
FAIL_UNDER="${KNET_COVERAGE_FAIL_UNDER:-80}"
FULL_FAIL_UNDER="${KNET_COVERAGE_FULL_FAIL_UNDER:-70}"

while [ $# -gt 0 ]; do
    case "$1" in
        --format) shift; FORMAT="$1" ;;
        --build-dir) shift; BUILD_DIR="$1" ;;
        --output) shift; OUTPUT="$1" ;;
        --filter) shift; FILTER="$1" ;;
        --diff) shift; DIFF_BRANCH="$1" ;;
        --fail-under) shift; FAIL_UNDER="$1" ;;
        --full-fail-under) shift; FULL_FAIL_UNDER="$1" ;;
        -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
        *) echo "Unknown option: $1 (see --help)" >&2; exit 1 ;;
    esac
    shift
done

# Auto-enable incremental when --diff not given: bare run yields full + incremental
AUTO_DIFF=0
if [ -z "${DIFF_BRANCH}" ]; then
    AUTO_DIFF=1   # base ref auto-detected later inside run_incremental
fi

# Locate the build dir containing .gcda files
locate_build() {
    local candidates=()
    [ -n "${BUILD_DIR}" ] && candidates+=("${BUILD_DIR}")
    candidates+=("/root/knet_wsl/test/ut/build")
    candidates+=("${SCRIPT_DIR}/build")
    local c
    for c in "${candidates[@]}"; do
        if [ -d "$c" ]; then
            local n
            n=$(find "$c" -name '*.gcda' 2>/dev/null | wc -l)
            if [ "${n:-0}" -gt 0 ]; then
                BUILD_DIR="$c"
                return 0
            fi
        fi
    done
    echo "Error: no build dir containing .gcda found" >&2
    echo "  Run ./knet_ut_BIN first to generate .gcda" >&2
    echo "  or specify the build dir via --build-dir" >&2
    exit 1
}

locate_build
GCDA_COUNT=$(find "${BUILD_DIR}" -name '*.gcda' 2>/dev/null | wc -l)

# Derive project root from build dir (build is at <root>/test/ut/build, up 3 levels)
KNET_ROOT="$(cd "${BUILD_DIR}/../../.." 2>/dev/null && pwd)"
if [ ! -d "${KNET_ROOT}/src/knet" ]; then
    KNET_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
fi
[ -z "${FILTER}" ] && FILTER="${KNET_ROOT}/src/knet"

echo "==> build dir : ${BUILD_DIR}"
echo "    .gcda count: ${GCDA_COUNT}"
echo "    root       : ${KNET_ROOT}"
echo "    filter     : ${FILTER}"

if ! command -v gcovr >/dev/null 2>&1; then
    echo "==> gcovr not found, installing via: pip install gcovr"
fi

# Output dir (default <script>/build/coverage, convenient for Windows access)
[ -z "${OUTPUT}" ] && OUTPUT="${SCRIPT_DIR}/build/coverage"
mkdir -p "${OUTPUT}"
echo "    output     : ${OUTPUT}"
echo "    format     : ${FORMAT}"

case "${FORMAT}" in
    text|html|xml|all) ;;
    *) echo "Error: invalid format '${FORMAT}', valid: text|html|xml|all" >&2; exit 1 ;;
esac

gcovr_args=()
gcovr_args+=(
    --root "${KNET_ROOT}"
    --filter "${FILTER}"
    --exclude '.*test.*'
    --exclude '.*build.*'
    --exclude '.*opensource.*'
    --exclude '.*mock.*'
    --exclude '.*common.*'
    --gcov-ignore-parse-errors negative_hits.warn_once_per_file
    --print-summary
)

case "${FORMAT}" in
    text) gcovr_args+=(--txt "${OUTPUT}/coverage.txt") ;;
    html) gcovr_args+=(--html "${OUTPUT}/coverage.html" --html-details) ;;
    xml) gcovr_args+=(--xml "${OUTPUT}/coverage.xml" --xml-pretty) ;;
    all)
        gcovr_args+=(--txt "${OUTPUT}/coverage.txt")
        gcovr_args+=(--html "${OUTPUT}/coverage.html" --html-details)
        gcovr_args+=(--xml "${OUTPUT}/coverage.xml" --xml-pretty)
        ;;
esac

# Incremental/threshold paths need coverage.xml; force it if not already included
need_xml=0
{ [ -n "${DIFF_BRANCH}" ] || [ "${AUTO_DIFF}" = "1" ]; } && need_xml=1
case "${FAIL_UNDER}" in ''|0|0.0|0) ;; *) need_xml=1 ;; esac
if [ "${need_xml}" -eq 1 ] && [ "${FORMAT}" != "xml" ] && [ "${FORMAT}" != "all" ]; then
    gcovr_args+=(--xml "${OUTPUT}/coverage.xml" --xml-pretty)
fi

echo "==> Running gcovr..."
set +e
gcovr "${gcovr_args[@]}" "${BUILD_DIR}" 2>"${OUTPUT}/gcovr.stderr"
GRC=$?

# Generate module-aggregated HTML (parse coverage.xml, aggregate to src dir level)
FULL_RC=0
if [ -f "${OUTPUT}/coverage.xml" ] && command -v python3 >/dev/null 2>&1; then
    echo "==> Generating module-aggregated report (coverage_modules.html)..."
    MOD_OUT="$(python3 - "${OUTPUT}/coverage.xml" <<'PYEOF'
import xml.etree.ElementTree as ET
from collections import defaultdict
import os, re, sys
xml_path = sys.argv[1]
out_html = os.path.join(os.path.dirname(xml_path), 'coverage_modules.html')
tree = ET.parse(xml_path); root = tree.getroot()
def parse_cc(s):
    m = re.search(r'\((\d+)/(\d+)\)', s or '')
    return (int(m.group(1)), int(m.group(2))) if m else (0, 0)
dd = defaultdict(lambda: {'tl':0,'cl':0,'tb':0,'cb':0,'tf':0,'cf':0,'files':[]})
for cls in root.iter('class'):
    fn = cls.get('filename','')
    if not fn.endswith('.c'): continue
    d = os.path.dirname(fn); tl=cl=tb=cb=tf=cf=0
    for method in cls.iter('method'):
        tf += 1; mhit = False
        for line in method.iter('line'):
            tl += 1; h = int(line.get('hits','0'))
            if h > 0: cl += 1; mhit = True
            if line.get('branch') == 'true':
                c,t = parse_cc(line.get('condition-coverage','')); cb += c; tb += t
        if mhit: cf += 1
    a = dd[d]; a['tl']+=tl; a['cl']+=cl; a['tb']+=tb; a['cb']+=cb; a['tf']+=tf; a['cf']+=cf
    a['files'].append({'name':os.path.basename(fn),'tl':tl,'cl':cl,'tb':tb,'cb':cb,'tf':tf,'cf':cf})
def pct(c,t): return (100.0*c/t) if t>0 else 0.0
def col(p): return 'red' if p<75 else ('yellow' if p<90 else 'green')
def disp(d): return d.replace('src/knet/','',1) if d.startswith('src/knet/') else d
g_tl=sum(a['tl'] for a in dd.values()); g_cl=sum(a['cl'] for a in dd.values())
g_tb=sum(a['tb'] for a in dd.values()); g_cb=sum(a['cb'] for a in dd.values())
g_tf=sum(a['tf'] for a in dd.values()); g_cf=sum(a['cf'] for a in dd.values())
rows=[]
for d in sorted(dd.keys()):
    a=dd[d]; pl,pb,pf=pct(a['cl'],a['tl']),pct(a['cb'],a['tb']),pct(a['cf'],a['tf'])
    fr=[]
    for f in sorted(a['files'],key=lambda x:x['name']):
        fpl,fpb,fpf=pct(f['cl'],f['tl']),pct(f['cb'],f['tb']),pct(f['cf'],f['tf'])
        fr.append(f"<tr class='file'><td>{f['name']}</td><td class='{col(fpl)}'>{fpl:.1f}%</td><td>{f['cl']}/{f['tl']}</td><td class='{col(fpb)}'>{fpb:.1f}%</td><td>{f['cb']}/{f['tb']}</td><td class='{col(fpf)}'>{fpf:.1f}%</td><td>{f['cf']}/{f['tf']}</td></tr>")
    rows.append(f"<tr class='dir' onclick=\"this.classList.toggle('open')\"><td>{disp(d)}</td><td class='{col(pl)}'>{pl:.1f}%</td><td>{a['cl']}/{a['tl']}</td><td class='{col(pb)}'>{pb:.1f}%</td><td>{a['cb']}/{a['tb']}</td><td class='{col(pf)}'>{pf:.1f}%</td><td>{a['cf']}/{a['tf']}</td></tr><tr class='files'><td colspan='7'><table><tr><th>File</th><th>Lines</th><th>L</th><th>Branches</th><th>B</th><th>Functions</th><th>F</th></tr>{''.join(fr)}</table></td></tr>")
opl,opb,opf=pct(g_cl,g_tl),pct(g_cb,g_tb),pct(g_cf,g_tf)
html=f"""<!DOCTYPE html>
<html><head><meta charset='utf-8'><title>knet UT Coverage (by module)</title>
<style>
body {{ font-family: sans-serif; margin:16px; font-size:14px; }}
table {{ border-collapse:collapse; width:100%; margin-bottom:8px; }}
th,td {{ border:1px solid #ccc; padding:4px 8px; text-align:right; }}
th {{ background:#eee; }}
td:first-child {{ text-align:left; }}
tr.dir {{ cursor:pointer; background:#f5f5f5; }}
tr.dir:hover {{ background:#e0e8f0; }}
tr.files {{ display:none; }}
tr.dir.open + tr.files {{ display:table-row; }}
tr.files table {{ margin:0 0 8px 24px; width:calc(100% - 24px); }}
tr.file td:first-child {{ padding-left:8px; }}
.red {{ color:#c00; font-weight:bold; }}
.yellow {{ color:#b80; font-weight:bold; }}
.green {{ color:#080; font-weight:bold; }}
.summary td {{ background:#d0d8e0; font-weight:bold; }}
</style></head>
<body>
<h2>knet UT Coverage (by module directory)</h2>
<table><tr class='summary'><td>TOTAL</td><td class='{col(opl)}'>{opl:.1f}%</td><td>{g_cl}/{g_tl}</td><td class='{col(opb)}'>{opb:.1f}%</td><td>{g_cb}/{g_tb}</td><td class='{col(opf)}'>{opf:.1f}%</td><td>{g_cf}/{g_tf}</td></tr></table>
<table><thead><tr><th>Module (directory)</th><th>Lines</th><th>L (cov/tot)</th><th>Branches</th><th>B (cov/tot)</th><th>Functions</th><th>F (cov/tot)</th></tr></thead><tbody>{''.join(rows)}</tbody></table>
<p style='color:#666;margin-top:12px'>Click any directory row to expand/collapse per-file coverage for that directory</p>
</body></html>"""
with open(out_html,'w',encoding='utf-8') as f: f.write(html)
print(f"    modules: lines {opl:.1f}%, branches {opb:.1f}%, functions {opf:.1f}% ({len(dd)} dirs)")
print(f"MODULES_LINES={opl:.1f}")
PYEOF
)"
    echo "${MOD_OUT}" | grep "^    modules:"
    FULL_PCT="$(echo "${MOD_OUT}" | grep "^MODULES_LINES=" | cut -d= -f2)"
    if [ -n "${FULL_PCT}" ]; then
        if awk -v p="${FULL_PCT}" -v t="${FULL_FAIL_UNDER}" 'BEGIN{exit (p>=t)?0:1}'; then
            echo "    full threshold : ${FULL_FAIL_UNDER}% -> PASS (${FULL_PCT}% >= ${FULL_FAIL_UNDER}%)"
        else
            echo "    full threshold : ${FULL_FAIL_UNDER}% -> FAIL (${FULL_PCT}% < ${FULL_FAIL_UNDER}%)"
            FULL_RC=1
        fi
    fi
fi

# ===== Incremental coverage (folly-style: git diff added lines ∩ coverage.xml line hits) =====
# Methodology ported from folly run_coverage.sh: parse unified diff for added line numbers,
# then intersect with per-line coverage from Cobertura XML. No diff-cover dependency.
# Note: incremental threshold is report-only (non-blocking); only --full-fail-under gates CI.

run_incremental() {
    # Skip if neither explicit --diff nor auto mode
    [ -z "${DIFF_BRANCH}" ] && [ "${AUTO_DIFF:-0}" = "0" ] && return 0
    echo ""

    # Resolve git repo: prefer KNET_ROOT, fall back to the repo containing this script
    # (build dir may be a separate source copy without .git, e.g. WSL ext4 build)
    GIT_ROOT="${KNET_ROOT}"
    if ! git -C "${GIT_ROOT}" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        GIT_ROOT="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel 2>/dev/null || true)"
    fi
    if [ -z "${GIT_ROOT}" ] || ! git -C "${GIT_ROOT}" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        if [ "${AUTO_DIFF:-0}" = "1" ]; then
            echo "==> Incremental stage skipped (not a git repo)"
            return 0
        fi
        echo "Error: no git repo found (tried ${KNET_ROOT} and ${SCRIPT_DIR}), --diff unavailable" >&2
        exit 1
    fi
    [ "${GIT_ROOT}" != "${KNET_ROOT}" ] && echo "  git root  : ${GIT_ROOT} (build dir is not a git repo)"

    # Auto-detect base branch when --diff not explicitly given (local refs only, no fetch)
    if [ "${AUTO_DIFF:-0}" = "1" ]; then
        for cand in origin/master origin/main master main HEAD~1; do
            if git -C "${GIT_ROOT}" rev-parse --verify "${cand}^{commit}" >/dev/null 2>&1; then
                DIFF_BRANCH="${cand}"; break
            fi
        done
        if [ -z "${DIFF_BRANCH}" ]; then
            echo "==> Incremental stage skipped (no base ref found)"
            return 0
        fi
        echo "==> Incremental stage: auto base ${DIFF_BRANCH}...HEAD"
    else
        echo "==> Incremental stage: diff vs ${DIFF_BRANCH}...HEAD"
    fi

    # Resolve base ref: try as-is, origin/<x>, refs/remotes/origin/<x>, then fetch+retry
    BASE="${DIFF_BRANCH}"
    if ! git -C "${GIT_ROOT}" rev-parse --verify "${BASE}^{commit}" >/dev/null 2>&1; then
        if git -C "${GIT_ROOT}" rev-parse --verify "origin/${BASE}^{commit}" >/dev/null 2>&1; then
            BASE="origin/${BASE}"
        elif git -C "${GIT_ROOT}" rev-parse --verify "refs/remotes/origin/${BASE}^{commit}" >/dev/null 2>&1; then
            BASE="refs/remotes/origin/${BASE}"
        else
            lb="${BASE#origin/}"
            echo "  base ref ${DIFF_BRANCH} not found locally, fetching origin ${lb} ..." >&2
            git -C "${GIT_ROOT}" fetch origin "${lb}" 2>/dev/null || git -C "${GIT_ROOT}" fetch origin 2>/dev/null || true
            if git -C "${GIT_ROOT}" rev-parse --verify "origin/${lb}^{commit}" >/dev/null 2>&1; then
                BASE="origin/${lb}"
            else
                echo "Error: cannot resolve base ref ${DIFF_BRANCH}" >&2
                echo "  available branches: $(git -C "${GIT_ROOT}" branch -a 2>/dev/null | head -10 | tr '\n' ' ')" >&2
                exit 1
            fi
        fi
    fi
    HEAD_SHA="$(git -C "${GIT_ROOT}" rev-parse HEAD)"

    if [ ! -f "${OUTPUT}/coverage.xml" ]; then
        if [ "${AUTO_DIFF:-0}" = "1" ]; then
            echo "==> Incremental stage skipped (coverage.xml missing)"
            return 0
        fi
        echo "Error: ${OUTPUT}/coverage.xml missing, cannot run incremental" >&2
        exit 1
    fi

    INC_RESULT="$(SRC_DIR="${GIT_ROOT}" BASE_SHA="${BASE}" HEAD_SHA="${HEAD_SHA}" XML_PATH="${OUTPUT}/coverage.xml" python3 <<'PYEOF'
import os, re, subprocess, sys
import xml.etree.ElementTree as ET

SRC_DIR = os.environ['SRC_DIR']
BASE    = os.environ['BASE_SHA']
HEAD    = os.environ['HEAD_SHA']
XML     = os.environ['XML_PATH']

def norm(p):
    p = p.replace('\\', '/')
    if os.path.isabs(p):
        try: p = os.path.relpath(p, SRC_DIR).replace('\\', '/')
        except Exception: pass
    while p.startswith('./'): p = p[2:]
    return p

# 1) Parse unified diff -> added line numbers per file (ported from folly get_added_lines)
def get_added_lines(src_dir, base, head):
    r = subprocess.run(['git', '-C', src_dir, 'diff', base, head], capture_output=True, text=True)
    added = {}
    cur = None; nl = 0
    for line in r.stdout.split('\n'):
        if line.startswith('+++ '):
            if line.startswith('+++ b/'):
                cur = norm(line[6:])
            elif line[4:] == '/dev/null':
                cur = '/dev/null'
            else:
                cur = norm(line[4:])
            if cur != '/dev/null':
                added.setdefault(cur, set())
        elif line.startswith('@@'):
            m = re.search(r'\+(\d+)', line)
            if m: nl = int(m.group(1))
        elif line.startswith('+') and not line.startswith('+++'):
            if cur and cur != '/dev/null':
                added[cur].add(nl)
            nl += 1
        elif not line.startswith('-') and not line.startswith('\\'):
            nl += 1
    return added

added = get_added_lines(SRC_DIR, BASE, HEAD)

# 2) Read coverage.xml -> line hits per file (Cobertura <line nr hits>)
tree = ET.parse(XML); root = tree.getroot()
cov = {}
for cls in root.iter('class'):
    fn = norm(cls.get('filename', ''))
    if not fn: continue
    d = cov.setdefault(fn, {})
    for line in cls.iter('line'):
        nr = line.get('nr')
        if nr is not None:
            d[int(nr)] = int(line.get('hits', '0'))

# 3) Intersect: added lines covered (hits>0). Restrict to production source under src/.
total_cov = 0; total_exe = 0; out = []
for f in sorted(added.keys()):
    lset = added[f]
    if not lset: continue
    if not f.startswith('src/'): continue
    lc = cov.get(f, {})
    pr = {ln: lc.get(ln, 0) for ln in lset}
    cc = sum(1 for c in pr.values() if c > 0)
    ex = len(pr)
    if ex == 0: continue
    pct = 100.0 * cc / ex
    out.append("  %s: %d/%d (%.1f%%)" % (f, cc, ex, pct))
    total_cov += cc; total_exe += ex

if total_exe > 0:
    print('\n'.join(out))
    print("TOTAL:%d/%d/%.1f" % (total_cov, total_exe, 100.0 * total_cov / total_exe))
else:
    print("TOTAL:0/0/SKIP")
PYEOF
)"

    echo "${INC_RESULT}" | grep -v "^TOTAL:" > "${OUTPUT}/diff-coverage.txt"
    TOTAL_LINE="$(echo "${INC_RESULT}" | grep "^TOTAL:")"

    if [ -z "${TOTAL_LINE}" ] || [ "${TOTAL_LINE}" = "TOTAL:0/0/SKIP" ]; then
        echo "  no incremental executable lines, skipped"
    else
        T_COV="$(echo "${TOTAL_LINE}" | cut -d: -f2 | cut -d/ -f1)"
        T_EXE="$(echo "${TOTAL_LINE}" | cut -d: -f2 | cut -d/ -f2)"
        T_PCT="$(echo "${TOTAL_LINE}" | cut -d/ -f3)"
        echo "  incremental: ${T_COV}/${T_EXE} = ${T_PCT}%"
        do_thresh=$(awk -v t="${FAIL_UNDER}" 'BEGIN{print (t+0>0)?1:0}')
        if [ "${do_thresh}" = "1" ]; then
            if awk -v p="${T_PCT}" -v t="${FAIL_UNDER}" 'BEGIN{exit (p>=t)?0:1}'; then
                echo "  incr threshold : ${FAIL_UNDER}% -> PASS (${T_PCT}% >= ${FAIL_UNDER}%)"
            else
                echo "  incr threshold : ${FAIL_UNDER}% -> FAIL (${T_PCT}% < ${FAIL_UNDER}%) [report-only, non-blocking]"
            fi
        fi
    fi
    echo "  report     : ${OUTPUT}/diff-coverage.txt"
}

run_incremental

echo ""
echo "Coverage report:"
echo "  per-file  : ${OUTPUT}/coverage.html"
echo "  per-module: ${OUTPUT}/coverage_modules.html"
[ -n "${DIFF_BRANCH}" ] && echo "  incremental: ${OUTPUT}/diff-coverage.txt"

# Final exit code: gcovr failure OR full threshold failure.
# Incremental threshold is report-only (non-blocking): only --full-fail-under gates CI.
FINAL_RC=${GRC}
[ ${FULL_RC:-0} -ne 0 ] && FINAL_RC=1
exit ${FINAL_RC}
