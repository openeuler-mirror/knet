#!/usr/bin/env python3
# -*- encoding: utf-8 -*-
# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
#
# K-NET is licensed under the Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#      http://license.coscl.org.cn/MulanPSL2
#
# 代码行覆盖率收集/统计脚本（不依赖 lcov）。
#
# 原理：
#   1. K-NET 的 UT 工程(test/ut/CMakeLists.txt)已使用 -fprofile-arcs -ftest-coverage 编译，
#      运行测试可执行文件后,构建目录下会生成 .gcda 运行数据和 .gcno 静态数据。
#   2. 本脚本调用 GCC 自带的 gcov(-b 选项)对每个源码文件产生 .gcov 文本，
#      该文本天然包含：行覆盖、函数覆盖、分支覆盖信息。
#   3. 脚本解析 .gcov，汇总 line / function / branch 覆盖率，并生成：
#        - 控制台汇总
#        - report/index.html       (总览+各文件覆盖率表格)
#        - report/src_*.html       (每个源文件的覆盖视图，绿=已覆盖，红=未覆盖)
#        - report/coverage.json    (机器可读汇总)
#        - report/gcov/*.gcov      (gcov 原始文件，可人工查看)
#
# 运行方式(在 linux 上)：
#   python3 gen_coverage.py                               # 先运行测试再统计
#   python3 gen_coverage.py --no-run                      # 只统计已存在的 .gcda
#   python3 gen_coverage.py --binary /path --build-dir /path --source-root /path --outdir /path
#
# 依赖：gcc/gcov、python3（仅使用标准库，无需安装 lcov 等第三方工具）。

import os
import re
import sys
import glob
import json
import shutil
import argparse
import subprocess
from html import escape

# ---------------------------------------------------------------------
# 常量
# ---------------------------------------------------------------------
LINE_NEUTRAL = 0     # 非可执行行（空行/注释/大括号/头信息）
LINE_COVERED = 1     # 已覆盖行
LINE_UNCOVERED = 2   # 未覆盖行

# gcov .gcov 文本解析用正则
RE_SOURCE_LINE = re.compile(r'^\s*(\S+):\s*(\d+):(.*)$')
RE_FUNCTION    = re.compile(r'^\s*function\s+(\S+)\s+called\s+(\d+)')
RE_BR_TAKEN    = re.compile(r'^\s*branch\s+\d+\s+taken\s+(\d+)\s*%')
RE_BR_UNTAKEN  = re.compile(r'^\s*branch\s+\d+\s+never executed')


def natural_int(text):
    """把 gcov 计数文本转为 int；非数字返回 None。"""
    try:
        return int(text)
    except ValueError:
        return None


# ---------------------------------------------------------------------
# 数据收集：定位 .gcda/.gcno
# ---------------------------------------------------------------------
def collect_artifacts(build_dir):
    """收集构建目录下所有编译产物。返回 .gcda 绝对路径列表。"""
    return sorted(glob.glob(os.path.join(build_dir, '**', '*.gcda'), recursive=True))


def collect_sources(src_root):
    """递归收集源码目录下所有 .c 文件。"""
    return sorted(glob.glob(os.path.join(src_root, '**', '*.c'), recursive=True))


def map_source(known_sources, src_abs):
    """把 gcov 报告的源码路径对齐到 src_root 下的已知文件。

    gcov 反推的 Source 路径可能与本工程收集到的路径表示不完全一致
    （绝对/相对、符号链接、NFS 挂载、大小写等差异）。若直接做严格字符串
    匹配会导致真正被执行的源码被误判为“无数据”而回退为 0% 覆盖率。
    这里先做完整路径(后缀)匹配，再退化为 basename + 目录贴合度匹配。

    known_sources: 本工程 src_root 下收集到的绝对路径列表。
    返回匹配到的真实路径；找不到返回 None。
    """
    norm = os.path.normpath(os.path.abspath(src_abs))
    base = os.path.basename(norm)

    for k in known_sources:
        if k == norm or k.endswith(norm):
            return k

    cands = [k for k in known_sources if os.path.basename(k) == base]
    if not cands:
        return None
    if len(cands) == 1:
        return cands[0]

    # 多个同名文件时，选取目录后缀重合最长者（最有可能的归属）。
    best, best_len = None, -1
    parts = norm.split(os.sep)
    for k in cands:
        kp = k.split(os.sep)
        n = 0
        for a, b in zip(reversed(parts), reversed(kp)):
            if a != b:
                break
            n += 1
        if n > best_len:
            best_len, best = n, k
    return best


# ---------------------------------------------------------------------
# gcov 解析
# ---------------------------------------------------------------------
# 提取 .gcov 头部的 Source: 路径
RE_SOURCE_HDR = re.compile(r'^\s*-\s*:\s*0\s*:\s*Source:\s*(.+?)\s*$', re.M)


def extract_source_path(content):
    m = RE_SOURCE_HDR.search(content)
    if m:
        return m.group(1).strip()
    return None


def run_gcov(gcov_bin, gcda_path, work_dir):
    """对单个 .gcda 调用 gcov -b，由 gcov 自行定位同名的 .gcno。

    直接传入真实的 .gcda 文件，避免因源码名推导与对象名(.gcno)不一致导致的
    找不到 notes 文件问题。

    注意：一次 gcov 可能产生多个 .gcov（一个编译单元含多个源码时）。
    我们按本 .gcda 对应的源码 basename 精准认领属于自己的 .gcov，
    避免把其它源码(可能恰为 0 覆盖率)的数据误记到本文件头上。

    返回 (content, gcov_src_path, source_abs)；失败返回 (None, None, None)。
    """
    base = os.path.basename(gcda_path)
    if base.endswith('.gcda'):
        base = base[:-len('.gcda')]   # 对象名，如 knet_pktpool

    cmd = [gcov_bin, '-b', gcda_path]
    stderr_text = ''
    try:
        proc = subprocess.run(cmd, cwd=work_dir, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE)
        stderr_text = proc.stderr.decode('utf-8', errors='replace')
    except (OSError, subprocess.SubprocessError) as err:
        print(f"  [warn] 运行 {gcov_bin} 失败: {err}", file=sys.stderr)
        return None, None, None

    picked = None
    for path in glob.glob(os.path.join(work_dir, '*.gcov')):
        try:
            with open(path, 'r', encoding='utf-8', errors='replace') as f:
                content = f.read()
        except OSError:
            continue
        source_abs = extract_source_path(content)
        if not source_abs:
            continue
        src_base = os.path.basename(source_abs)
        # 源码名与该 gcda 的对象名相关才属于本文件：
        #   对象 knet_pktpool -> 源码 knet_pktpool.c(.h)
        if src_base == base or src_base.startswith(base + '.'):
            return content, path, source_abs
        if picked is None:
            picked = (content, path, source_abs)

    if picked:
        p_content, p_path, p_src = picked
        if base and not os.path.basename(p_src).startswith(base + '.'):
            print(f"  [debug] {gcda_path} 未匹配到源码 {base}.*，采用首个 .gcov "
                  f"(Source: {p_src})", file=sys.stderr)
        return p_content, p_path, p_src

    print(f"  [warn] 处理 {gcda_path} 后未生成 .gcov，gcov stderr: {stderr_text.strip()[:300]}",
          file=sys.stderr)
    return None, None, None


def parse_gcov(content):
    """解析 .gcov 文本，返回 dict 聚合结果。

    返回字段：
      lines:         [ {line_no, status, count, branches_taken, branches_total} ]
      funcs:         [ {name, called} ]
      lines_total / lines_covered
      funcs_total / funcs_covered
      branches_total / branches_covered
    """
    lines = []
    funcs = []
    last_line = None        # 最近一条源文件行(用于把 branch 挂到其所属判断行)

    for raw in content.splitlines():
        mf = RE_FUNCTION.match(raw)
        if mf:
            funcs.append({'name': mf.group(1), 'called': int(mf.group(2))})
            continue

        mb_t = RE_BR_TAKEN.match(raw)
        mb_u = RE_BR_UNTAKEN.match(raw)
        if mb_t or mb_u:
            taken = mb_t is not None and int(mb_t.group(1)) > 0
            if last_line is not None:
                last_line['branches_total'] = last_line.get('branches_total', 0) + 1
                if taken:
                    last_line['branches_taken'] = last_line.get('branches_taken', 0) + 1
            continue

        ms = RE_SOURCE_LINE.match(raw)
        if not ms:
            continue
        counter, lineno_s, _text = ms.group(1), ms.group(2), ms.group(3)
        lineno = int(lineno_s)
        if lineno == 0:
            continue  # Source/Graph/Data/Runs 头信息

        # 计算本行覆盖状态
        if counter == '-':
            status = LINE_NEUTRAL
            count = None
        else:
            c = natural_int(counter)
            if c is not None and c > 0:
                status = LINE_COVERED
                count = c
            else:
                # '#####' / '=====' 均为未执行的可执行行
                status = LINE_UNCOVERED
                count = 0

        entry = {
            'line_no': lineno,
            'status': status,
            'count': count,
            'branches_taken': 0,
            'branches_total': 0,
        }
        lines.append(entry)
        last_line = entry

    lines_total = sum(1 for l in lines if l['status'] != LINE_NEUTRAL)
    lines_covered = sum(1 for l in lines if l['status'] == LINE_COVERED)
    branches_total = sum(l['branches_total'] for l in lines)
    branches_covered = sum(l['branches_taken'] for l in lines)
    funcs_total = len(funcs)
    funcs_covered = sum(1 for f in funcs if f['called'] > 0)

    return {
        'lines': lines,
        'funcs': funcs,
        'lines_total': lines_total,
        'lines_covered': lines_covered,
        'funcs_total': funcs_total,
        'funcs_covered': funcs_covered,
        'branches_total': branches_total,
        'branches_covered': branches_covered,
    }


def fallback_zero(source_text, source_path):
    """当某源码无 gcov 数据时，近似认为其所有可执行行均未覆盖。"""
    lines = []
    for i, raw in enumerate(source_text.splitlines(), start=1):
        s = raw.strip()
        if not s:
            continue
        if s.startswith('//') or s.startswith('/*') or s.startswith('*'):
            continue
        if s.startswith('#') or s in ('{', '}', '};', ';'):
            continue
        lines.append({'line_no': i, 'status': LINE_UNCOVERED,
                      'count': 0, 'branches_taken': 0, 'branches_total': 0})
    return {
        'lines': lines,
        'funcs': [],
        'lines_total': len(lines),
        'lines_covered': 0,
        'funcs_total': 0,
        'funcs_covered': 0,
        'branches_total': 0,
        'branches_covered': 0,
    }


# ---------------------------------------------------------------------
# 测试执行
# ---------------------------------------------------------------------
def run_tests(binary, work_dir):
    """运行测试可执行文件（在构建目录下，确保 .gcda 定位正确）。"""
    if not os.path.exists(binary):
        print(f"[error] 未找到测试可执行文件: {binary}", file=sys.stderr)
        sys.exit(1)
    env = dict(os.environ)
    # 尽力把依赖库路径加入环境（若缺失不影响）
    print(f"[info ] 运行测试: {binary}")
    print(f"[info ] 工作目录: {work_dir}")
    try:
        proc = subprocess.run([binary], cwd=work_dir, env=env)
    except OSError as err:
        print(f"[error] 无法执行测试: {err}", file=sys.stderr)
        sys.exit(1)
    print(f"[info ] 测试进程退出码: {proc.returncode}")


# ---------------------------------------------------------------------
# HTML / JSON 报告生成
# ---------------------------------------------------------------------
def pct(cnt, tot):
    if tot <= 0:
        return 100.0 if cnt > 0 else 0.0
    return cnt * 100.0 / tot


def fmt_pct(cnt, tot):
    return "%.2f%%" % pct(cnt, tot)


def cell(cnt, tot):
    return "%d/%d (%s)" % (cnt, tot, fmt_pct(cnt, tot))


def write_json(report, outdir):
    jf = os.path.join(outdir, 'coverage.json')
    with open(jf, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    return jf


def read_source_text(path):
    try:
        with open(path, 'r', encoding='utf-8', errors='replace') as f:
            return f.read()
    except OSError:
        return ''


def render_source_html(rel, data, src_abs):
    """渲染单个源文件的覆盖视图 HTML。"""
    text = read_source_text(src_abs)
    text_lines = text.splitlines()

    by_no = {l['line_no']: l for l in data['lines']}
    max_no = max(by_no) if by_no else 0
    if len(text_lines) < max_no:
        text_lines += [''] * (max_no - len(text_lines))

    for line in data['lines']:
        branch_info = ''
        if line['branches_total']:
            branch_info = ('<span class="b">branches %d/%d taken</span>'
                           % (line['branches_taken'], line['branches_total']))
        line['branch_html'] = branch_info

    rows = []
    for idx, raw in enumerate(text_lines, start=1):
        info = by_no.get(idx)
        if info is None:
            rows.append('<tr class="neutral"><td class="num">%d</td>'
                        '<td class="cnt"></td><td class="src"></td></tr>' % idx)
            continue
        if info['status'] == LINE_COVERED:
            cls = 'covered'
            cnt = str(info['count'])
        elif info['status'] == LINE_UNCOVERED:
            cls = 'uncovered'
            cnt = '#####'
        else:
            cls = 'neutral'
            cnt = ''
        rows.append('<tr class="%s"><td class="num">%d</td><td class="cnt">%s</td>'
                    '<td class="src">%s %s</td></tr>'
                    % (cls, idx, cnt, escape(raw), info['branch_html']))

    func_rows = ''.join(
        '<tr><td>%s</td><td>%s</td><td class="%s">%s</td></tr>'
        % (escape(f['name']), f['called'],
           'ok' if f['called'] > 0 else 'bad',
           '是' if f['called'] > 0 else '否')
        for f in data['funcs'])

    html = '''<!DOCTYPE html>
<html lang="zh"><head><meta charset="utf-8">
<title>{rel} - 覆盖率</title>
<style>
body {{ font-family: Consolas, Menlo, monospace; padding: 12px; }}
table.gcov {{ border-collapse: collapse; width: 100%; }}
tr.covered td {{ background: #e6f5e6; }}
tr.uncovered td {{ background: #ffe0e0; }}
tr.neutral td {{ color: #999; }}
td.num {{ width: 50px; text-align: right; color: #999; padding-right: 8px; }}
td.cnt {{ width: 60px; text-align: right; padding-right: 8px; }}
td.src {{ white-space: pre; }}
.b {{ color: #666; font-size: 0.85em; }}
.overline {{ margin-bottom: 6px; }}
.ok {{ color: green; font-weight: bold; }}
.bad {{ color: red; font-weight: bold; }}
</style></head><body>
<h3><a href="index.html">← 返回总览</a>　{rel}</h3>
<div class="overline">
行: {lc}/{lt} ({lp}) 　函数: {fc}/{ft} ({fp}) 　分支: {bc}/{bt} ({bp})
</div>
<table class="gcov">
<thead><tr><th>行号</th><th>次数</th><th>源码</th></tr></thead>
<tbody>
{rows}
</tbody></table>
<h4>函数覆盖</h4>
<table class="gcov"><thead><tr><th>函数</th><th>调用次数</th><th>已覆盖</th></tr></thead>
<tbody>{func_rows}</tbody></table>
</body></html>'''.format(
        rel=escape(rel),
        lc=data['lines_covered'], lt=data['lines_total'], lp=fmt_pct(data['lines_covered'], data['lines_total']),
        fc=data['funcs_covered'], ft=data['funcs_total'], fp=fmt_pct(data['funcs_covered'], data['funcs_total']),
        bc=data['branches_covered'], bt=data['branches_total'], bp=fmt_pct(data['branches_covered'], data['branches_total']),
        rows='\n'.join(rows),
        func_rows=func_rows,
    )
    return html


def render_index(files, totals, outdir):
    file_rows = []
    for f in files:
        file_rows.append(
            '<tr><td class="fname"><a href="%s">%s</a></td>'
            '<td>%s</td><td>%s</td><td>%s</td></tr>'
            % (f['page'], escape(f['rel']),
               cell(f['data']['lines_covered'], f['data']['lines_total']),
               cell(f['data']['funcs_covered'], f['data']['funcs_total']),
               cell(f['data']['branches_covered'], f['data']['branches_total'])))

    html = '''<!DOCTYPE html>
<html lang="zh"><head><meta charset="utf-8">
<title>K-NET UT 覆盖率报告</title>
<style>
body {{ font-family: Arial, sans-serif; padding: 16px; }}
table {{ border-collapse: collapse; width: 100%; }}
th, td {{ border: 1px solid #ccc; padding: 6px 10px; font-size: 13px; }}
th {{ background: #f0f0f0; }}
tr.total td {{ font-weight: bold; background: #faf6e6; }}
.fname a {{ color: #06c; }}
.p {{
display:inline-block; height: 14px; background: #4caf50; min-width: 0;
}}
.pbar {{ background:#eee; }}
</style></head><body>
<h2>K-NET 单元测试覆盖率报告</h2>
<p>统计时间: {time}</p>
<table>
<thead><tr><th>源码文件</th><th>行覆盖</th><th>函数覆盖</th><th>分支覆盖</th></tr></thead>
<tbody>
<tr class="total">
<td>总共 {n} 个文件</td>
<td>{line}</td><td>{func}</td><td>{branch}</td>
</tr>
{rows}
</tbody></table>
</body></html>'''.format(
        time=__import__('datetime').datetime.now().strftime('%Y-%m-%d %H:%M:%S'),
        n=len(files),
        line=cell(totals['lines_covered'], totals['lines_total']),
        func=cell(totals['funcs_covered'], totals['funcs_total']),
        branch=cell(totals['branches_covered'], totals['branches_total']),
        rows='\n'.join(file_rows),
    )
    return html


# ---------------------------------------------------------------------
# 主流程
# ---------------------------------------------------------------------
def main():
    here = os.path.dirname(os.path.abspath(__file__))
    parser = argparse.ArgumentParser(description='K-NET UT 覆盖率统计（不依赖 lcov）')
    parser.add_argument('--binary',
                        default=os.path.join(here, 'knet_ut_BIN'),
                        help='UT 测试可执行文件路径')
    parser.add_argument('--build-dir',
                        default=os.path.join(here, 'build'),
                        help='UT 构建目录（包含 .gcda/.gcno）')
    parser.add_argument('--source-root',
                        default=os.path.join(here, '..', '..', 'src', 'knet'),
                        help='源码根目录（默认只统计 src/knet 下的源码）')
    parser.add_argument('--outdir',
                        default=os.path.join(here, 'coverage_report'),
                        help='报告输出目录')
    parser.add_argument('--gcov', default='gcov', help='gcov 可执行文件名或路径')
    parser.add_argument('--no-run', action='store_true',
                        help='不重新运行测试，只统计已存在的 .gcda')
    parser.add_argument('--pattern', default='',
                        help='仅统计源码相对 src 路径中包含该子串的文件')
    args = parser.parse_args()

    binary = os.path.abspath(args.binary)
    build_dir = os.path.abspath(args.build_dir)
    src_root = os.path.abspath(args.source_root)
    outdir = os.path.abspath(args.outdir)

    for d in (build_dir, src_root):
        if not os.path.isdir(d):
            print(f"[error] 目录不存在: {d}", file=sys.stderr)
            sys.exit(1)

    # 1) 运行测试（可选）
    if not args.no_run:
        run_tests(binary, build_dir)

    # 2) 收集编译产物
    gcda_all = collect_artifacts(build_dir)
    if not gcda_all:
        print("[error] 构建目录中未找到 .gcda。请先通过 test/build.sh 构建并运行测试。",
              file=sys.stderr)
        sys.exit(1)

    # 3) 准备输出目录
    if os.path.isdir(outdir):
        shutil.rmtree(outdir)
    os.makedirs(outdir)
    gcov_dump = os.path.join(outdir, 'gcov')
    os.makedirs(gcov_dump)
    work_dir = os.path.join(outdir, '_work')
    os.makedirs(work_dir)

    # 4) 用真实 .gcda 逐个调用 gcov，得到各源文件覆盖数据
    #    results: src_abs -> {data, gcov_src}
    #    先用本工程已知源码建表，把 gcov 报告的路径可靠对齐到真实文件，
    #    避免路径表示差异(绝对/相对/符号链接/NFS)导致已执行源码被误判为无数据。
    known_sources = [os.path.abspath(p) for p in collect_sources(src_root)]
    results = {}
    for gcda in gcda_all:
        content, gcov_src, src_abs = run_gcov(args.gcov, gcda, work_dir)
        # 清理本次工作目录中的 .gcov，避免多个文件串扰
        for f in glob.glob(os.path.join(work_dir, '*.gcov')):
            os.remove(f)
        if not content or not src_abs:
            continue
        # 只统计 KNET 源码目录下的文件（忽略测试用例/ mock 等）
        src_abs = map_source(known_sources, src_abs)
        if src_abs is None:
            continue
        if src_abs in results:
            continue
        results[src_abs] = {
            'data': parse_gcov(content),
            'gcov_src': gcov_src if os.path.isfile(gcov_src) else None,
        }

    # 5) 组装文件列表；未产生 gcda 数据的源码按未覆盖处理
    files = []
    fallback_files = []
    for src in collect_sources(src_root):
        src_abs = os.path.abspath(src)
        rel = os.path.relpath(src_abs, src_root)
        if args.pattern and args.pattern not in rel.replace('\\', '/'):
            continue
        source_text = read_source_text(src_abs)
        if src_abs in results:
            data = results[src_abs]['data']
            gcov_src = results[src_abs]['gcov_src']
            status_note = "" if data['lines_total'] else \
                "无可执行行数据"
        else:
            data = fallback_zero(source_text, src_abs)
            gcov_src = None
            status_note = "未生成gcda(可能从未执行到该文件)"
            fallback_files.append(rel)

        # 保存 gcov 原始文件（存在才复制）
        gcov_dst = os.path.join(gcov_dump, rel.replace('/', '__'))
        if gcov_src:
            shutil.copy2(gcov_src, gcov_dst)

        rel_for_page = rel.replace('/', '__')
        page = 'src_%s.html' % rel_for_page
        files.append({
            'rel': rel,
            'page': page,
            'src_abs': src_abs,
            'data': data,
            'note': status_note,
        })

    # 6) 渲染输出文件
    for f in files:
        page = os.path.join(outdir, f['page'])
        with open(page, 'w', encoding='utf-8') as fp:
            fp.write(render_source_html(f['rel'], f['data'], f['src_abs']))

    totals = {
        'lines_total': sum(f['data']['lines_total'] for f in files),
        'lines_covered': sum(f['data']['lines_covered'] for f in files),
        'funcs_total': sum(f['data']['funcs_total'] for f in files),
        'funcs_covered': sum(f['data']['funcs_covered'] for f in files),
        'branches_total': sum(f['data']['branches_total'] for f in files),
        'branches_covered': sum(f['data']['branches_covered'] for f in files),
    }

    index_html = render_index(files, totals, outdir)
    with open(os.path.join(outdir, 'index.html'), 'w', encoding='utf-8') as f:
        f.write(index_html)

    json_dump = {
        'summary': {
            'files': len(files),
            'lines': {'covered': totals['lines_covered'], 'total': totals['lines_total'],
                      'rate': pct(totals['lines_covered'], totals['lines_total'])},
            'functions': {'covered': totals['funcs_covered'], 'total': totals['funcs_total'],
                          'rate': pct(totals['funcs_covered'], totals['funcs_total'])},
            'branches': {'covered': totals['branches_covered'], 'total': totals['branches_total'],
                         'rate': pct(totals['branches_covered'], totals['branches_total'])},
        },
        'details': [{
            'file': f['rel'], 'note': f['note'],
            'lines': {'covered': f['data']['lines_covered'], 'total': f['data']['lines_total'],
                      'rate': pct(f['data']['lines_covered'], f['data']['lines_total'])},
            'functions': {'covered': f['data']['funcs_covered'], 'total': f['data']['funcs_total'],
                          'rate': pct(f['data']['funcs_covered'], f['data']['funcs_total'])},
            'branches': {'covered': f['data']['branches_covered'], 'total': f['data']['branches_total'],
                         'rate': pct(f['data']['branches_covered'], f['data']['branches_total'])},
        } for f in files],
    }
    jf = write_json(json_dump, outdir)

    shutil.rmtree(work_dir, ignore_errors=True)

    # 6) 控制台汇总
    print('=' * 62)
    print('K-NET UT coverage')
    print('=' * 62)
    print('  line      : %9d / %-9d  %s' %
          (totals['lines_covered'], totals['lines_total'],
           fmt_pct(totals['lines_covered'], totals['lines_total'])))
    print('  funcs     : %9d / %-9d  %s' %
          (totals['funcs_covered'], totals['funcs_total'],
           fmt_pct(totals['funcs_covered'], totals['funcs_total'])))
    print('  branches  : %9d / %-9d  %s' %
          (totals['branches_covered'], totals['branches_total'],
           fmt_pct(totals['branches_covered'], totals['branches_total'])))
    print('-' * 62)
    print('  source files  : %d' % len(files))
    print('  report   : %s' % os.path.join(outdir, 'index.html'))
    print('  json data file   : %s' % jf)
    print('  raw gcov dump   : %s' % gcov_dump)
    print('=' * 62)
    if fallback_files:
        print('[warn] 以下 %d 个源码未产生 gcov 数据(统计为 0)，括号内为诊断:' %
              len(fallback_files))
        for r in fallback_files:
            rbase = os.path.basename(r)
            obj = rbase.rsplit('.', 1)[0] if '.' in rbase else rbase
            gcda_found = glob.glob(os.path.join(build_dir, '**',
                                                obj + '.gcda'), recursive=True)
            if gcda_found:
                print('   - %-70s gcda:%d个' % (r, len(gcda_found)))
            else:
                print('   - %-70s 无对应.gcda(未编译/未链接/未执行)' % r)
    print('=' * 62)


if __name__ == '__main__':
    main()