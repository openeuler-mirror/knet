# knet UT 覆盖率获取说明

## 功能简介

`test/ut/get_cover.sh` 用于在 UT 运行后生成 knet 单元测试的代码覆盖率报告。脚本串联了完整的覆盖率流水线：

**gcovr 生成全量报告 → 模块聚合报告 → 增量覆盖率（自动）→ 阈值检查**

> 前置条件：`./knet_ut_BIN` 已运行且 `.gcda` 文件存在。本脚本不负责构建与运行 UT，需用户自行执行 `test/build.sh` 与 `./knet_ut_BIN`。

覆盖率插桩由 `test/ut/CMakeLists.txt` 完成（`-fprofile-arcs -ftest-coverage`，链接 `gcov` 库），构建产物 `knet_ut_BIN` 输出到 `test/ut/` 根目录，`.gcno/.gcda` 生成在 `test/ut/build/CMakeFiles/knet_ut_BIN.dir/` 下。脚本通过 `gcovr` 将 `.gcda/.gcno` 转换为 Cobertura 格式的 `coverage.xml`/`coverage.html`，并基于内嵌 Python 解析 XML 生成模块聚合报告 `coverage_modules.html` 与 folly 风格的增量覆盖率报告 `diff-coverage.txt`（不依赖 `diff-cover`）。

## 依赖

### 系统依赖

| 命令 | 用途 | 说明 |
|------|------|------|
| `bash` | 脚本运行 | 推荐 4.x 及以上 |
| `gcov` | gcno/gcda 解析 | gcc 配套工具（插桩由 CMakeLists.txt 调用 `gcc`/`g++` 完成） |
| `python3` | 模块聚合报告 / 增量覆盖率 / gcovr 依赖 | 3.6+ |
| `git` | 增量覆盖率（解析 `git diff`） | 仅增量阶段需要 |

### Python 依赖

```bash
pip install gcovr          # 覆盖率报告生成（脚本缺失时会自动尝试 pip 安装）
```

> 说明：增量覆盖率不依赖 `diff-cover`，脚本内嵌 Python 解析 `git diff` 与 `coverage.xml` 完成计算。

## 快速开始

```bash
# 1. 首次构建（clone dpdk + build.py debug + cmake + make，生成 knet_ut_BIN）
cd test && bash build.sh

# 2. 运行 UT 生成 .gcda
./test/ut/knet_ut_BIN

# 3. 生成覆盖率报告（全量 + 自动增量）
bash test/ut/get_cover.sh
```

报告默认输出到 `test/ut/build/coverage/`，包含 `coverage.txt`、`coverage.html`、`coverage.xml`、`coverage_modules.html`；增量阶段还会生成 `diff-coverage.txt`。

## 命令行选项

| 选项 | 说明 | 默认值 |
|------|------|--------|
| `--format FORMAT` | 报告格式：`text` / `html` / `xml` / `all` | `all` |
| `--build-dir DIR` | 包含 `.gcda` 的构建目录 | 自动探测 |
| `--output DIR` | 报告输出目录 | `test/ut/build/coverage` |
| `--filter PATH` | gcovr `--filter` 被统计源码路径 | `<root>/src/knet` |
| `--diff BRANCH` | 增量覆盖率，对比 `BRANCH...HEAD` | 自动探测基线 |
| `--fail-under N` | 增量行覆盖率最低阈值，低于则退出码非 0 | `80` |
| `--full-fail-under N` | 全量行覆盖率最低阈值，低于则退出码非 0 | `70` |
| `-h, --help` | 打印帮助并退出 | — |

> 说明：
> - **构建目录自动探测**：依次检查 `KNET_BUILD_DIR`、`/root/knet_wsl/test/ut/build`、`<script>/build`，选取第一个含 `.gcda` 的目录；都未找到则报错退出。
> - **增量基线自动探测**：未显式指定 `--diff` 时，依次尝试 `origin/master`、`origin/main`、`master`、`main`、`HEAD~1`，选取第一个可解析的引用；都不可用则跳过增量阶段。
> - 未显式指定 `--diff` 时，脚本同时生成全量与增量报告；显式指定 `--diff` 后只针对该基线生成增量报告。
> - `gcovr` 缺失时脚本会尝试 `pip install gcovr`，安装失败则跳过报告生成并以退出码 0 退出，避免影响构建流水线。

## 环境变量

环境变量优先级低于命令行参数（即命令行参数会覆盖同名环境变量）。

| 变量 | 说明 | 对应选项 | 默认值 |
|------|------|----------|--------|
| `KNET_BUILD_DIR` | 覆盖默认构建目录 | `--build-dir` | 自动探测 |
| `KNET_COVERAGE_OUTPUT` | 覆盖默认输出目录 | `--output` | `<script>/build/coverage` |
| `KNET_COVERAGE_DIFF_BRANCH` | 覆盖默认增量分支 | `--diff` | 自动探测 |
| `KNET_COVERAGE_FAIL_UNDER` | 覆盖默认增量阈值 | `--fail-under` | `80` |
| `KNET_COVERAGE_FULL_FAIL_UNDER` | 覆盖默认全量阈值 | `--full-fail-under` | `70` |

## 产物说明

报告默认输出在 `test/ut/build/coverage/` 下：

| 文件 | 说明 |
|------|------|
| `coverage.txt` | 文本版覆盖率摘要 |
| `coverage.html` | HTML 覆盖率报告（含逐行详情 `--html-details`） |
| `coverage.xml` | Cobertura XML 报告（模块聚合与增量覆盖率的输入） |
| `coverage_modules.html` | 模块聚合 HTML 报告（按 `src/knet` 子目录聚合，可点击展开查看文件级覆盖） |
| `diff-coverage.txt` | 增量覆盖率报告（自动或 `--diff` 时生成） |
| `gcovr.stderr` | gcovr 运行的标准错误输出 |

阈值检查说明：

- `--full-fail-under N`：基于模块聚合报告的全量行覆盖率，低于 N% 则退出码非 0（默认 70%）。
- `--fail-under N`：基于 folly 风格增量覆盖率（`git diff` 新增行 ∩ `coverage.xml` 行命中），低于 N% 则退出码非 0（默认 80%，仅增量阶段检查）。
- 退出码：gcovr 失败、全量阈值未达标、增量阈值未达标，任一发生则退出码非 0。

## 常用示例

```bash
# 1. 默认全量+增量报告（自动探测构建目录与基线分支）
bash test/ut/get_cover.sh

# 2. 仅生成 HTML 报告（仍会生成 XML 供模块聚合与增量使用）
bash test/ut/get_cover.sh --format html

# 3. 指定构建目录（如 WSL 内 ext4 路径）
bash test/ut/get_cover.sh --build-dir /root/knet_wsl/test/ut/build

# 4. 增量覆盖率对比指定分支
bash test/ut/get_cover.sh --diff origin/master

# 5. 调整阈值（全量 75%，增量 85%）
bash test/ut/get_cover.sh --full-fail-under 75 --fail-under 85

# 6. 自定义输出目录与过滤路径
bash test/ut/get_cover.sh --output /tmp/cov --filter /path/to/src
```
