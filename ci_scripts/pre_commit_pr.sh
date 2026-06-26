#!/bin/bash
set -e # 命令执行失败时异常退出
set -o pipefail

set +x # 执行命令前打印

# 脚本标题
echo "================================================"
echo "          Pre-Commit CI 增量检查"
echo "================================================"

# 配置文件.pre-commit-config.yaml校验
PRE_COMMIT_CONFIG_YAML=".pre-commit-config.yaml"
if [ ! -f ${PRE_COMMIT_CONFIG_YAML} ]; then
	echo "[SUCCESS] 未找到${PRE_COMMIT_CONFIG_YAML}，检查终止"
	exit 0
fi

# 获取变更文件列表
echo -e "\n[INFO] 获取变更文件列表"
# shellcheck disable=SC2207
FILES_ARR=($(git diff --name-only --diff-filter=ACMR ${BEFORE_MERGE} HEAD | sort -u))

# 无变更文件直接退出
if [ ${#FILES_ARR[@]} -eq 0 ]; then
	echo "[INFO] 无变更文件，检查通过"
	exit 0
fi

# 输出变更文件信息
echo -e "\n[INFO] 变更文件数量: ${#FILES_ARR[@]}"
echo "[INFO] 变更文件列表:"
for f in "${FILES_ARR[@]}"; do echo "  $f"; done

# 安装pre-commit工具
echo -e "\n[INFO] 安装 pre-commit"
pip config set global.index-url https://repo.huaweicloud.com/repository/pypi/simple # 配置国内pip源加速
pip config set global.trusted-host repo.huaweicloud.com
pip install pre-commit # 安装pre-commit

export PATH="/home/jenkins/.local/bin:$PATH"  # 流水线环境工具默认安装路径在/home/jenkins/.local/bin

pre-commit --version
pre-commit install --install-hooks # 安装扫描工具

# 执行pre-commit检查
echo -e "\n[INFO] 开始 pre-commit 检查"
echo "[COMMAND] pre-commit run --files ${FILES_ARR[*]}"
set +e
pre-commit run --show-diff-on-failure --files "${FILES_ARR[@]}"
CODE=$?
set -e

# 输出检查结果
echo -e "\n================================================================"
if [ ${CODE} -eq 0 ]; then
	echo "[INFO] pre-commit 检查全部通过"
else
	echo "[ERROR] pre-commit 检查失败"
	echo "[INFO] 请在本地执行以下命令修复后重新提交:"
	echo ""
	echo "1. 安装/初始化环境"
	echo "pip install pre-commit && pre-commit install --install-hooks"
	echo ""
	echo "2. 检查并修复变更文件"
	echo "pre-commit run --files ${FILES_ARR[*]}"
fi
echo "================================================================"

exit ${CODE}
