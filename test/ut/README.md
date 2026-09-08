# K-NET 单元测试覆盖率统计（不依赖 lcov）

本目录提供一套纯 Python（仅标准库）+ GCC `gcov` 的代码覆盖率统计方案，
用于在 **linux** 上对 K-NET 单元测试（UT）执行完毕后生成覆盖率数据，
覆盖 **行（line）**、**函数（function）**、**分支（branch）** 三类指标，
并生成可视化报告，便于查看哪些源码已被覆盖、哪些未覆盖。

> 不使用 lcov，仅依赖系统自带的 gcc / gcov 与 python3。

---

## 1. 基本原理

K-NET 的 UT 工程 `CMakeLists.txt` 已使用如下编译选项：

```
-fprofile-arcs -ftest-coverage
```

因此：

1. **编译时**：每个源码对象会生成静态的 `.gcno` 文件；
2. **运行时**：测试可执行文件运行后，在构建目录生成运行数据 `.gcda` 文件；
3. `gen_coverage.py` 以 gcov 自带的 `gcov -b` 逐文件生成 `.gcov` 文本，
   该文本包含行覆盖计数、函数调用次数、分支命中情况；
4. 脚本解析 `.gcov`，汇总出 line / function / branch 覆盖率，
   输出 HTML + JSON 报告。

```
源码(*.c) --编译--> 对象 + .gcno --运行测试--> .gcda
                      \                         /
                       \-- gcov -b -----------/--> .gcov --> parse --> 报告
```

---

## 2. 依赖

- Linux 系统（本方案生成的 `.gcda` 依赖 GCC：`-fprofile-arcs -ftest-coverage`）
- gcc + gcov（GCC 自带，无需单独安装）
- python3（仅用标准库，无第三方依赖）

---

## 3. 使用方法

### 3.1 编译 UT 源码（在 linux 上）

在 K-NET 工程根目录执行：

```bash
# 若尚未编译依赖与 K-NET，先执行工程级构建
# python3 build.py debug

cd test
bash build.sh      # 编译 K-NET 与 UT 测试
```

`build.sh` 会调用 `cmake ..` 并把可执行文件输出到 `test/ut/`，
即编译产物位于 `test/ut/build`，测试可执行文件为 `test/ut/knet_ut_BIN`。

### 3.2 运行测试并生成覆盖率报告

在 `test/ut` 目录下执行：

```bash
cd test/ut
python3 gen_coverage.py
```

脚本会：

1. 运行测试可执行文件（`knet_ut_BIN`），从而生成 `.gcda` 数据；
2. 对 `../src`（KNET 源码根目录）下的每个 `.c` 文件调用 `gcov -b`；
3. 解析并汇总覆盖率；
4. 在 `test/ut/coverage_report/` 生成报告。

控制台会输出汇总：

```
==============================================================
K-NET UT 覆盖率汇总
==============================================================
  行    覆盖:         1234 / 2000     61.70%
  函数  覆盖:          567 / 800      70.88%
  分支  覆盖:          890 / 1500     59.33%
--------------------------------------------------------------
  源文件数   : 120
  报告入口   : .../coverage_report/index.html
  数据文件   : .../coverage_report/coverage.json
  原始 gcov  : .../coverage_report/gcov
==============================================================
```

### 3.3 生成的报告文件

| 文件/目录            | 说明                                                        |
| -------------------- | ----------------------------------------------------------- |
| `index.html`         | 总览页：各源码文件的行/函数/分支覆盖率表格 + 总覆盖率       |
| `src_<路径>.html`    | 每个源文件的覆盖视图：绿=已覆盖，红=未覆盖，含函数覆盖表   |
| `coverage.json`      | 机器可读的汇总数据（可接入 CI 阈值判断）                    |
| `gcov/*.gcov`        | gcov 生成的原始覆盖文本，可人工查看                         |

用浏览器打开 `coverage_report/index.html` 即可查看：

- **总覆盖率**：行 / 函数 / 分支 的 已覆盖/总数/百分比；
- **未覆盖源码定位**：红色行 = `#####` 未执行，绿色行 = 已执行；
- **分支覆盖**：源文件页内每条分支行显示 `branches x/y taken`；
- **函数覆盖**：源文件页底部列出每个函数的调用次数与是否覆盖。

---

## 4. 常用参数

| 参数                    | 默认值                            | 说明                                            |
| ----------------------- | --------------------------------- | ----------------------------------------------- |
| `--binary`              | `test/ut/knet_ut_BIN`             | 测试可执行文件路径                              |
| `--build-dir`           | `test/ut/build`                   | 编译目录（存放 `.gcda` / `.gcno`）              |
| `--source-root`         | `src`                             | 源码根目录（默认统计其下所有 `.c`）             |
| `--outdir`              | `test/ut/coverage_report`         | 报告输出目录                                    |
| `--gcov`                | `gcov`                            | gcov 可执行文件名或路径                         |
| `--no-run`              | 关                                | 只统计已存在的 `.gcda`，不重新运行测试          |
| `--pattern`             | 空                                | 只统计相对源码根路径中含该子串的文件            |

示例：

```bash
# 只统计，不重新运行测试
python3 gen_coverage.py --no-run

# 只统计某一个子目录(如 sal/tcp)
python3 gen_coverage.py --pattern tcp

# 自定义路径
python3 gen_coverage.py --binary /tmp/knet_ut_BIN --build-dir /tmp/build \
    --source-root ../src --outdir /tmp/coverage
```

---

## 5. 覆盖率的判定规则

- **行覆盖**：gcov 计数为数字且 >0 的行视为已覆盖；
  `#####` / `=====` 行视为未覆盖的可执行行。
- **函数覆盖**：`gcov` 标注 `called N`，`N>0` 视为已覆盖。
- **分支覆盖**：`branch taken X%` 且 `X>0` 视为已覆盖；
  `taken 0%` 或 `never executed` 视为未覆盖分支。
- 没有 `.gcda`/`.gcov` 数据的源文件（编译了但从未执行到）：
  近似将其所有可执行行标记为未覆盖，并在 JSON 中给出 `note` 提示。

---

## 6. 常见问题

- **`[error] 构建目录中未找到 .gcda/.gcno`**
  说明尚未编译或尚未运行测试，请先执行 `bash build.sh`。

- **`[error] 目录不存在`**
  确认 `--build-dir` / `--source-root` 路径正确。

- **只有全部未覆盖**
  多为未执行测试或 `.gcda` 未生成。请先运行一次测试，
  或用 `python3 gen_coverage.py`（不带 `--no-run`）。

- **gcov 版本与编译编译器不一致**
  gcov 版本需与编译所用的 gcc 匹配，否则可能无法读取 `.gcda`。
  通过 `--gcov` 指定与编译器匹配的 gcov 路径。

- **报告为中文但浏览器乱码**
  报告使用 UTF-8 编码，请确保浏览器自动识别为 UTF-8。