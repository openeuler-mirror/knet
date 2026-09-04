# knet UT 覆盖率获取说明

## 功能简介

`test/ut/coverage.sh` 用于一键获取 knet 单元测试的代码覆盖率报告。脚本串联了完整的覆盖率流水线：

**构建（可选）→ 运行 UT → gcovr 生成报告 → 增量覆盖率（可选）→ 阈值检查（可选）**

覆盖率插桩由 `test/ut/CMakeLists.txt` 完成（`-fprofile-arcs -ftest-coverage`，链接 `gcov` 库），构建产物 `knet_ut_BIN` 输出到 `test/ut/` 根目录，`.gcno/.gcda` 生成在 `test/ut/build/CMakeFiles/knet_ut_BIN.dir/` 下。脚本通过 `gcovr` 将 `.gcda/.gcno` 转换为 Cobertura 格式的 `coverage.xml`/`coverage.html`，并支持 `diff-cover` 做增量覆盖率检查。

## 依赖

### 系统依赖

| 命令 | 用途 | 说明 |
|------|------|------|
| `bash` | 脚本运行 | 推荐 4.x 及以上 |
| `gcc` / `g++` | 编译插桩 | 由 CMakeLists.txt 调用 |
| `gcov` | gcno/gcda 解析 | gcc 配套工具 |
| `python3` | 阈值检查 / gcovr 依赖 | 3.6+ |

### Python 依赖

```bash
pip install gcovr          # 覆盖率报告生成
pip install diff-cover     # 增量覆盖率（仅 --diff 时需要）
```

## 快速开始

```bash
# 1. 首次构建（clone dpdk + build.py debug + cmake + make，生成 knet_ut_BIN）
cd test
bash build.sh

# 2. 复用已有二进制生成覆盖率报告
bash test/ut/coverage.sh --no-build

# 3. 一条命令完成构建+运行+报告
bash test/ut/coverage.sh
```

报告默认输出到 `test/ut/build/coverage/`，包含 `coverage.txt`、`coverage.html`、`coverage.xml`。

## 命令行选项

| 选项 | 说明 | 默认值 |
|------|------|--------|
| `--build` | 执行构建（调用 `test/build.sh`） | 默认行为 |
| `--no-build` | 跳过构建，复用已有 `knet_ut_BIN` 与 `.gcno`（缺失则报错退出） | — |
| `--run` | 运行 UT 二进制 | 默认行为 |
| `--no-run` | 跳过运行，复用已有 `.gcda` | — |
| `--format FORMAT` | 报告格式：`text` / `html` / `xml` / `all` | `all` |
| `--output DIR` | 报告输出目录 | `test/ut/build/coverage` |
| `--filter PATH` | gcovr `--filter` 被统计源码路径 | `<root>/src/knet` |
| `--fail-under N` | 行覆盖率最低阈值，低于则退出码非 0 | `0`（不检查） |
| `--diff BRANCH` | 增量覆盖率，对比 `BRANCH...HEAD` | 关闭 |
| `--jobs N` | make 并发数 | `8` |
| `-h, --help` | 打印帮助并退出 | — |

> 说明：`--jobs` 用于控制 make 并发数。当前 `test/build.sh` 内部硬编码 `make -j8`，`--jobs` 通过环境变量 `KNET_UT_BUILD_JOBS` 透传，待 `build.sh` 适配后生效。

## 环境变量

环境变量优先级低于命令行参数（即命令行参数会覆盖同名环境变量）。

| 变量 | 说明 | 对应选项 |
|------|------|----------|
| `KNET_COVERAGE_OUTPUT` | 覆盖默认输出目录 | `--output` |
| `KNET_COVERAGE_FORMAT` | 覆盖默认报告格式 | `--format` |
| `KNET_COVERAGE_FAIL_UNDER` | 覆盖默认阈值 | `--fail-under` |
| `KNET_COVERAGE_DIFF_BRANCH` | 覆盖默认增量分支 | `--diff` |
| `KNET_UT_BUILD_JOBS` | 覆盖默认 make 并发数 | `--jobs` |
| `ASAN_OPTIONS` | 覆盖默认 ASan 选项（默认 `detect_leaks=0:abort_on_error=1`） | — |

## 产物说明

报告默认输出在 `test/ut/build/coverage/` 下：

| 文件 | 说明 |
|------|------|
| `coverage.txt` | 文本版覆盖率摘要 |
| `coverage.html` | HTML 覆盖率报告（含逐行详情 `--html-details`） |
| `coverage.xml` | Cobertura XML 报告（阈值检查与增量覆盖率的输入） |
| `diff-coverage.txt` | 增量覆盖率报告（仅 `--diff` 时生成） |

阈值检查与增量覆盖率说明：

- `--fail-under N`：解析 `coverage.xml` 根节点 `line-rate` 属性，行覆盖率 < N% 则脚本退出码非 0。
- `--diff BRANCH`：调用 `diff-cover` 对比 `BRANCH...HEAD` 的增量代码覆盖率，低于阈值则退出码非 0；启用 `--diff` 时阈值检查由 `diff-cover` 兜底，不再重复执行。

## 与 numpy pipline 的对应关系

本脚本参考 numpy 的覆盖率流水线实现，对应关系如下：

| numpy pipline | knet coverage.sh | 说明 |
|---------------|------------------|------|
| `gcovr`（生成 Cobertura XML/HTML） | `run_gcovr` | gcovr 适配 C/C++ 的 `.gcno/.gcda` 覆盖率数据 |
| `--root` / `--filter` / `--exclude` | 同名参数 | 只统计被测源码，排除第三方/生成代码 |
| `--gcov-ignore-parse-errors negative_hits.warn_once_per_file` | 同 | 忽略 gcov 解析告警 |
| `--print-summary` | 同 | 打印覆盖率摘要 |
| `find ... -name '*.gcda' -delete` | `run_tests` 运行前清理 | 清理脏数据 |
| `incremental_coverage.sh`（`diff-cover`） | `run_diff` | 增量覆盖率检查 |
| 内嵌 Python 解析 XML `line-rate` | `check_threshold` | 阈值检查 |

## 常用示例

```bash
# 仅生成 HTML 报告
bash test/ut/coverage.sh --no-build --no-run --format html

# 行覆盖率阈值 80%
bash test/ut/coverage.sh --no-build --fail-under 80

# 增量覆盖率，对比 origin/master，阈值 90%
bash test/ut/coverage.sh --no-build --diff origin/master --fail-under 90

# 自定义输出目录与过滤路径
bash test/ut/coverage.sh --no-build --output /tmp/cov --filter /path/to/src
```
