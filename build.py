#!/usr/bin/env python3
# -*- encoding:utf-8 -*-
# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

import os
import sys
import glob
import shutil
import logging
import subprocess

KNET_SOURCE_DIR = os.path.abspath(os.path.dirname(__file__))
# 记录进程启动时的工作目录：脚本内部多处 os.chdir（cJSON/DPDK/securec 构建），
# 归一化用户传入的相对路径必须以启动目录为基准，否则会被解析到内部切换后的目录
KNET_LAUNCH_CWD = os.getcwd()
logging.basicConfig(level=logging.INFO)
cJSON_repo_url = "https://gitcode.com/gh_mirrors/cj/cJSON.git"
cJSON_tag = "v1.7.15"
dpdk_repo_url = "https://github.com/DPDK/dpdk.git"  # 当前dpdk没有官方镜像仓，正式构建暂保留，其他依赖全面迁移到gitcode
dpdk_tag = "v21.11.7"
securec_repo_url = "https://gitcode.com/openeuler/libboundscheck.git"
securec_tag = "v1.1.16"

def copy_cjson():
    KNET_CJSON_DIR = f"{KNET_SOURCE_DIR}/opensource/cJSON"
    # copy
    if not os.path.exists(f"{KNET_SOURCE_DIR}/build/opensource/lib/cJSON"):
        os.makedirs(f"{KNET_SOURCE_DIR}/build/opensource/lib/cJSON")
    if not os.path.exists(f"{KNET_SOURCE_DIR}/build/opensource/include/cJSON"):
        os.makedirs(f"{KNET_SOURCE_DIR}/build/opensource/include/cJSON")
    shutil.copy2(f"{KNET_CJSON_DIR}/build/libcjson.so", f"{KNET_SOURCE_DIR}/build/opensource/lib/cJSON")
    shutil.copy2(f"{KNET_CJSON_DIR}/cJSON.h", f"{KNET_SOURCE_DIR}/build/opensource/include/cJSON")

def build_cjson():
    KNET_CJSON_DIR = f"{KNET_SOURCE_DIR}/opensource/cJSON"

    if os.path.exists(f"{KNET_CJSON_DIR}/build/libcjson.a") and os.path.exists(f"{KNET_CJSON_DIR}/cJSON.h"):
        logging.info("cJSON exist")
        copy_cjson()
        return 0
    shutil.rmtree(f"{KNET_CJSON_DIR}/build", ignore_errors=True)
    if not os.path.exists(KNET_CJSON_DIR):
        logging.info("cjson directory not found, cloning repository...")
        
        os.makedirs(f"{KNET_SOURCE_DIR}/opensource", exist_ok=True)
        
        os.chdir(f"{KNET_SOURCE_DIR}/opensource")
        
        # clone cjson仓库
        clone_cmd = ["git", "clone", "-b", cJSON_tag, cJSON_repo_url, "cJSON"]
        output = subprocess.run(clone_cmd, shell=False)
        if output.returncode != 0:
            logging.error(f"git clone failed. [{clone_cmd}]")
            return 1
        
    os.chdir(f"{KNET_CJSON_DIR}")
    # build
    cmd = ["cmake", f"{KNET_CJSON_DIR}", "-B", f"{KNET_CJSON_DIR}/build", "-DBUILD_SHARED_AND_STATIC_LIBS=On", "-DCMAKE_C_FLAGS=-fPIC"]
    output = subprocess.run(cmd, shell=False)
    if output.returncode != 0:
        logging.error(f"exec cmd fail. [{cmd}]")
        return 1

    cmd = ["make", "-C", f"{KNET_CJSON_DIR}/build", "-j"]
    output = subprocess.run(cmd, shell=False)
    if output.returncode != 0:
        logging.error(f"exec cmd fail. [{cmd}]")
        return 1

    copy_cjson()
    return 0

def copy_dpdk():
    KNET_DPDK_DIR = f"{KNET_SOURCE_DIR}/opensource/dpdk"
    # copy
    shutil.rmtree(f"{KNET_SOURCE_DIR}/build/opensource/lib/dpdk", ignore_errors=True)
    shutil.rmtree(f"{KNET_SOURCE_DIR}/build/opensource/include/dpdk", ignore_errors=True)
    shutil.copytree(f"{KNET_DPDK_DIR}/output/lib64", f"{KNET_SOURCE_DIR}/build/opensource/lib/dpdk")
    shutil.copytree(f"{KNET_DPDK_DIR}/output/include", f"{KNET_SOURCE_DIR}/build/opensource/include/dpdk")

def build_dpdk():
    KNET_DPDK_DIR = f"{KNET_SOURCE_DIR}/opensource/dpdk"

    if os.path.exists(f"{KNET_DPDK_DIR}/output/lib64") and os.path.exists(f"{KNET_DPDK_DIR}/output/include"):
        logging.info("dpdk exist")
        copy_dpdk()
        return 0
    
    if not os.path.exists(KNET_DPDK_DIR):
        logging.info("dpdk directory not found, cloning repository...")
        
        os.makedirs(f"{KNET_SOURCE_DIR}/opensource", exist_ok=True)
        
        os.chdir(f"{KNET_SOURCE_DIR}/opensource")
        
        # clone dpdk仓库
        clone_cmd = ["git", "clone", "-b", dpdk_tag, dpdk_repo_url, "dpdk"]
        output = subprocess.run(clone_cmd, shell=False)
        if output.returncode != 0:
            logging.error(f"git clone failed. [{clone_cmd}]")
            return 1
       
    os.chdir(f"{KNET_DPDK_DIR}")
    shutil.rmtree(f"{KNET_DPDK_DIR}/build", ignore_errors=True)
    cmd = ["meson", "-Ddisable_drivers=net/cnxk",
                   f"-Dprefix={KNET_DPDK_DIR}/output/",
                   "-Dplatform=generic",
                   "-Denable_kmods=false",
                   "build"]
    output = subprocess.run(cmd, shell=False)
    if output.returncode != 0:
        logging.error(f"exec cmd fail. [{cmd}]")
        return 1
    cmd = ["ninja", "-C", "build"]
    output = subprocess.run(cmd, shell=False)
    if output.returncode != 0:
        logging.error(f"exec cmd fail. [{cmd}]")
        return 1
    # ninja install -C build
    cmd = ["ninja", "install", "-C", "build"]
    output = subprocess.run(cmd, shell=False)
    if output.returncode != 0:
        logging.error(f"exec cmd fail. [{cmd}]")
        return 1

    # 生成libdpdk.so：先用 pkg-config 收集 DPDK 的编译/链接参数，再打包成单个动态库
    os.chdir(f"{KNET_SOURCE_DIR}/opensource/dpdk/output/lib64")
    os.environ['PKG_CONFIG_PATH'] = f"{KNET_SOURCE_DIR}/opensource/dpdk/output/lib64/pkgconfig"

    # 注意：pkg-config 不存在时抛 FileNotFoundError，libdpdk.pc 缺失时抛 CalledProcessError，
    # 必须放在 try 内捕获，否则脚本会以未捕获的异常栈退出，无法给出可读的失败原因
    try:
        cflags_libs = subprocess.check_output(
            ['pkg-config', '--cflags', '--libs', 'libdpdk', '--static']
        ).decode('utf-8').strip().split()
    except (subprocess.CalledProcessError, FileNotFoundError) as e:
        logging.error(f"获取 libdpdk 编译参数失败，请确认已安装 pkg-config 且存在 libdpdk.pc: {e}")
        return 1

    cmd = ['gcc']
    cmd.extend(cflags_libs)
    cmd.extend(['-shared', '-o', 'libdpdk.so'])

    # gcc 未安装时 Popen 抛 FileNotFoundError，需要一并捕获，避免异常栈直接抛出
    try:
        output = subprocess.Popen(cmd)
        output.wait()
    except FileNotFoundError as e:
        logging.error(f"生成libdpdk.so失败，未找到 gcc: {e}")
        return 1

    if output.returncode != 0:
        logging.error(f"生成libdpdk.so失败. [{cmd}]")
        return 1

    copy_dpdk()
    return 0

def use_external_dpdk():
    """通过 KNET_DPDK_PATH 环境变量复用用户已编译/安装的外部 DPDK，
    将其库和头文件软链到 knet 构建所需的路径。
    DPDK 版本兼容性由用户自行保证，knet 不做版本校验。"""
    dpdk_path = os.environ.get('KNET_DPDK_PATH')
    if not dpdk_path:
        logging.error("环境变量 KNET_DPDK_PATH 未设置")
        return 1

    # 归一化为绝对路径后再建软链：若 src 保留相对形式，软链目标会按链接所在目录解析，
    # 且会被后续 os.chdir 影响而失效；基准取脚本启动目录（而非当前 CWD），
    # 避免受前面 cJSON/securec 构建过程中 os.chdir 的干扰
    dpdk_path = os.path.normpath(os.path.join(KNET_LAUNCH_CWD, os.path.expanduser(dpdk_path)))

    if not os.path.exists(dpdk_path):
        logging.error(f"KNET_DPDK_PATH 指向的路径不存在: {dpdk_path}")
        return 1

    KNET_DPDK_OUTPUT_DIR = f"{KNET_SOURCE_DIR}/opensource/dpdk/output"

    # 探测 include 目录：在 $KNET_DPDK_PATH/include、$KNET_DPDK_PATH/include/dpdk、$KNET_DPDK_PATH 中找含 rte_eal.h 的目录
    inc_dir = None
    for candidate in [
        os.path.join(dpdk_path, 'include'),
        os.path.join(dpdk_path, 'include', 'dpdk'),
        dpdk_path,
    ]:
        if os.path.exists(os.path.join(candidate, 'rte_eal.h')):
            inc_dir = candidate
            break
    if not inc_dir:
        logging.error(f"在 KNET_DPDK_PATH={dpdk_path} 下未找到含 rte_eal.h 的 include 目录")
        return 1

    # 探测 lib 目录：在 $KNET_DPDK_PATH/lib64、$KNET_DPDK_PATH/lib、$KNET_DPDK_PATH 中找含 librte_*.so* 的目录
    lib_dir = None
    for candidate in [
        os.path.join(dpdk_path, 'lib64'),
        os.path.join(dpdk_path, 'lib'),
        dpdk_path,
    ]:
        if not os.path.isdir(candidate):
            continue
        if glob.glob(os.path.join(candidate, 'librte_*.so*')):
            lib_dir = candidate
            break
    if not lib_dir:
        logging.error(f"在 KNET_DPDK_PATH={dpdk_path} 下未找到含 librte_*.so* 的 lib 目录")
        return 1

    logging.info(f"使用外部DPDK: lib={lib_dir}, include={inc_dir}")

    # KNET_DPDK_PATH 可能指向本仓自身的构建产物目录（例如复用上一次构建的 output）。
    # 该目录同时是下面重建的目标，若当成外部 DPDK 处理会先把软链源自己删掉；
    # 因此先检查其中是否已有可直接复用的库/头文件/pkgconfig：完整则跳过重建，否则明确报错
    output_real = os.path.realpath(KNET_DPDK_OUTPUT_DIR)
    is_self_output = False
    for path in (lib_dir, inc_dir):
        real = os.path.realpath(path)
        if real == output_real or real.startswith(output_real + os.sep):
            is_self_output = True
            break

    reuse_existing = False
    if is_self_output:
        # os.path.exists 会跟随软链，软链农场中的断链在此判定为不可复用
        has_lib = any(os.path.exists(p) for p in glob.glob(os.path.join(lib_dir, 'librte_*.so*')))
        has_header = os.path.exists(os.path.join(inc_dir, 'rte_eal.h'))
        has_pc = os.path.exists(os.path.join(lib_dir, 'pkgconfig', 'libdpdk.pc'))
        if has_lib and has_header and has_pc:
            reuse_existing = True
            logging.info(f"KNET_DPDK_PATH 指向本仓构建产物 {output_real} 且内容完整，直接复用，跳过软链重建")
        else:
            logging.error(f"KNET_DPDK_PATH 指向 knet 自身的构建产物目录 {output_real}，"
                          "但其中缺少可复用的 librte_*.so*/rte_eal.h/libdpdk.pc，"
                          "请改为指向真正的 DPDK 安装目录")
            return 1

    if not reuse_existing:
        # 清理旧 output 目录
        shutil.rmtree(f"{KNET_DPDK_OUTPUT_DIR}/lib64", ignore_errors=True)
        shutil.rmtree(f"{KNET_DPDK_OUTPUT_DIR}/include", ignore_errors=True)
        os.makedirs(f"{KNET_DPDK_OUTPUT_DIR}/lib64", exist_ok=True)
        os.makedirs(f"{KNET_DPDK_OUTPUT_DIR}/include", exist_ok=True)

        # 软链 DPDK 库文件（librte_*.so*）到 output/lib64
        # 不链 libdpdk*.so*：本函数末尾会用 gcc 重新生成该合并库，外部同名文件没有复用价值
        for item in os.listdir(lib_dir):
            if item.startswith('librte_'):
                src = os.path.join(lib_dir, item)
                dst = os.path.join(f"{KNET_DPDK_OUTPUT_DIR}/lib64", item)
                if not os.path.exists(dst):
                    os.symlink(src, dst)

        # 仅链接 DPDK 自身的头文件到 output/include，避免带入非 DPDK 头文件导致冲突
        # 1) 根目录下的 rte_*.h
        for src in glob.glob(os.path.join(inc_dir, 'rte_*.h')):
            dst = os.path.join(f"{KNET_DPDK_OUTPUT_DIR}/include", os.path.basename(src))
            os.symlink(src, dst)
        # 2) 含 rte_*.h 的子目录（如 generic/）整体软链
        for item in os.listdir(inc_dir):
            sub = os.path.join(inc_dir, item)
            if not os.path.isdir(sub):
                continue
            if not glob.glob(os.path.join(sub, 'rte_*.h')):
                continue
            dst = os.path.join(f"{KNET_DPDK_OUTPUT_DIR}/include", item)
            os.symlink(sub, dst)

        # 软链 pkgconfig（如有）
        pkgconfig_dst = os.path.join(f"{KNET_DPDK_OUTPUT_DIR}/lib64", 'pkgconfig')
        os.makedirs(pkgconfig_dst, exist_ok=True)
        sys_pkgconfig = os.path.join(lib_dir, 'pkgconfig')
        if os.path.exists(sys_pkgconfig):
            for item in os.listdir(sys_pkgconfig):
                if item.startswith('libdpdk') or item.startswith('rte_'):
                    src = os.path.join(sys_pkgconfig, item)
                    dst = os.path.join(pkgconfig_dst, item)
                    if not os.path.exists(dst):
                        os.symlink(src, dst)

    # 生成 libdpdk.so（与 build_dpdk 逻辑一致）
    # 注意：pkgconfig 的软链是"如有"则链，但本步骤仍强依赖 libdpdk.pc 来获取编译/链接参数，
    # 若外部 DPDK 未提供 .pc（手工/裁剪安装）或构建机未安装 pkg-config，则在此明确报错退出
    os.environ['PKG_CONFIG_PATH'] = f"{KNET_DPDK_OUTPUT_DIR}/lib64/pkgconfig"
    os.chdir(f"{KNET_DPDK_OUTPUT_DIR}/lib64")

    # pkg-config 不存在时抛 FileNotFoundError，.pc 缺失时抛 CalledProcessError，需一并捕获
    try:
        cflags_libs = subprocess.check_output(
            ['pkg-config', '--cflags', '--libs', 'libdpdk', '--static']
        ).decode('utf-8').strip().split()
    except (subprocess.CalledProcessError, FileNotFoundError) as e:
        logging.error(f"获取 libdpdk 编译参数失败，请确认已安装 pkg-config 且 KNET_DPDK_PATH 下存在 libdpdk.pc: {e}")
        return 1

    cmd = ['gcc']
    cmd.extend(cflags_libs)
    cmd.extend(['-shared', '-o', 'libdpdk.so'])

    # gcc 未安装时 Popen 抛 FileNotFoundError，不能只捕获 CalledProcessError
    try:
        proc = subprocess.Popen(cmd)
        proc.wait()
    except FileNotFoundError as e:
        logging.error(f"生成libdpdk.so失败，未找到 gcc: {e}")
        return 1

    if proc.returncode != 0:
        logging.error(f"生成libdpdk.so失败. [{cmd}]")
        return 1

    logging.info("外部DPDK链接完成")
    copy_dpdk()
    return 0

def copy_securec():
    KNET_SECUREC_DIR = f"{KNET_SOURCE_DIR}/opensource/secure_c/"
    # copy
    shutil.rmtree(f"{KNET_SOURCE_DIR}/build/opensource/lib/securec", ignore_errors=True)
    shutil.rmtree(f"{KNET_SOURCE_DIR}/build/opensource/include/securec", ignore_errors=True)
    shutil.copytree(f"{KNET_SECUREC_DIR}/lib", f"{KNET_SOURCE_DIR}/build/opensource/lib/securec")
    shutil.copytree(f"{KNET_SECUREC_DIR}/include", f"{KNET_SOURCE_DIR}/build/opensource/include/securec")

def build_securec():
    KNET_SECUREC_DIR = f"{KNET_SOURCE_DIR}/opensource/secure_c"
    if os.path.exists(f"{KNET_SECUREC_DIR}/include") and os.path.exists(f"{KNET_SECUREC_DIR}/lib"):
        logging.info("securec exist")
        copy_securec()
        return 0

    if not os.path.exists(KNET_SECUREC_DIR):
        logging.info("securec directory not found, cloning repository...")
        
        os.makedirs(f"{KNET_SOURCE_DIR}/opensource", exist_ok=True)
        
        os.chdir(f"{KNET_SOURCE_DIR}/opensource")
        
        # clone secure仓库
        clone_cmd = ["git", "clone", "-b", securec_tag, securec_repo_url, "secure_c"]
        output = subprocess.run(clone_cmd, shell=False)
        if output.returncode != 0:
            logging.error(f"git clone failed. [{clone_cmd}]")
            return 1
        
    # opensource secure_c 构建
    os.chdir(f"{KNET_SECUREC_DIR}")
    cmd = ["make"]
    output = subprocess.run(cmd, shell=False)
    if output.returncode != 0:
        logging.error(f"exec cmd fail. [{cmd}]")

    # secure_c 构建
    os.chdir(f"{KNET_SECUREC_DIR}/src")
    cmd = ["make", "lib"]
    output = subprocess.run(cmd, shell=False)
    if output.returncode != 0:
        logging.error(f"exec cmd fail. [{cmd}]")

    if os.path.exists(f"{KNET_SECUREC_DIR}/include") and os.path.exists(f"{KNET_SECUREC_DIR}/lib"):
        copy_securec()
        return 0
    return 1

def build_knet(debug, test, sdv, fuzz, use_opensource=False):
    os.chdir(f"{KNET_SOURCE_DIR}")
    # build

    # 获取原始 LD_LIBRARY_PATH
    original_ld_library_path = os.environ.get('LD_LIBRARY_PATH', '')

    # 设置 LD_LIBRARY_PATH
    os.environ["LD_LIBRARY_PATH"] = f"{KNET_SOURCE_DIR}/opensource/dpdk/output/lib64:" \
        f"{os.environ.get('LD_LIBRARY_PATH', '')}"
    os.environ["LD_LIBRARY_PATH"] = f"{KNET_SOURCE_DIR}/opensource/cJSON/build:" \
        f"{os.environ.get('LD_LIBRARY_PATH', '')}"
    os.environ["LD_LIBRARY_PATH"] = f"{KNET_SOURCE_DIR}/opensource/secure_c/lib:" \
        f"{os.environ.get('LD_LIBRARY_PATH', '')}"

    cmd = ["cmake", f"{KNET_SOURCE_DIR}", "-B", f"{KNET_SOURCE_DIR}/build", "-DKNET_BUILD_TYPE="]
    if debug:
        cmd[-1] += "Debug"
    elif test:
        cmd[-1] += "Test"
    elif sdv:
        cmd[-1] += "SDV"
    elif fuzz:
        cmd[-1] += "Fuzz"
    else:
        cmd[-1] += "Release"
    if use_opensource:
        cmd.append("-DUSE_OPENSOURCE=ON")
    output = subprocess.run(cmd, shell=False)
    if output.returncode != 0:
        logging.error(f"exec cmd fail. [{cmd}]")
        return 1

    cmd = ["make", "-C", f"{KNET_SOURCE_DIR}/build", "-j"]
    output = subprocess.run(cmd, shell=False)
    if output.returncode != 0:
        logging.error(f"exec cmd fail. [{cmd}]")
        return 1

    # 将 LD_LIBRARY_PATH 还原为原始值
    os.environ["LD_LIBRARY_PATH"] = original_ld_library_path

    return 0

def build_test():
    KNET_TEST_DIR = f"{KNET_SOURCE_DIR}/test"
    os.chdir(f"{KNET_TEST_DIR}")
    if not os.path.exists(f"{KNET_TEST_DIR}/build"):
        os.makedirs(f"{KNET_TEST_DIR}/build")
    # build
    cmd = ["cmake", f"{KNET_TEST_DIR}", "-B", f"{KNET_TEST_DIR}/build"]
    output = subprocess.run(cmd, shell=False)
    if output.returncode != 0:
        logging.error(f"exec cmd fail. [{cmd}]")
        return 1

    cmd = ["make", "-C", f"{KNET_TEST_DIR}/build", "-j"]
    output = subprocess.run(cmd, shell=False)
    if output.returncode != 0:
        logging.error(f"exec cmd fail. [{cmd}]")
        return 1

    return 0

def build_rpm():
    os.chdir(f"{KNET_SOURCE_DIR}")
    KNET_RPM_DIR = f"{KNET_SOURCE_DIR}/build/rpmbuild"
    shutil.rmtree(f"{KNET_RPM_DIR}", ignore_errors=True)

    os.makedirs(f"{KNET_RPM_DIR}")
    os.makedirs(f"{KNET_RPM_DIR}/SPECS")
    os.makedirs(f"{KNET_RPM_DIR}/BUILD/usr")
    os.makedirs(f"{KNET_RPM_DIR}/BUILD/usr/bin")
    os.makedirs(f"{KNET_RPM_DIR}/BUILD/usr/include")
    os.makedirs(f"{KNET_RPM_DIR}/BUILD/usr/include/knet")
    os.makedirs(f"{KNET_RPM_DIR}/SRPMS")
    os.makedirs(f"{KNET_RPM_DIR}/RPMS")

    # symlinks：是否复制软连接，True复制软连接，False不复制，软连接会被当成文件复制过来，默认False
    shutil.copytree(f"{KNET_SOURCE_DIR}/build/lib", f"{KNET_RPM_DIR}/BUILD/usr/lib64", symlinks=True)
    shutil.copytree(f"{KNET_SOURCE_DIR}/conf", f"{KNET_RPM_DIR}/SOURCES")
    shutil.copy2(f"{KNET_SOURCE_DIR}/package/knet.spec", f"{KNET_RPM_DIR}/SPECS")
    shutil.copy2(f"{KNET_SOURCE_DIR}/build/src/knet/knet_mp_daemon", f"{KNET_RPM_DIR}/BUILD/usr/bin")
    shutil.copy2(f"{KNET_SOURCE_DIR}/build/include/extern_api/h/knet_socket_api.h",
        f"{KNET_RPM_DIR}/BUILD/usr/include/knet")
    shutil.copytree(f"{KNET_SOURCE_DIR}/tools", f"{KNET_RPM_DIR}/SOURCES/tools")

    cmd = ["rpmbuild", f"-ba", f"{KNET_RPM_DIR}/SPECS/knet.spec"]
    output = subprocess.run(cmd, shell=False)
    if output.returncode != 0:
        logging.error(f"exec cmd fail. [{cmd}]")
        return 1
    return 0

def clean_build(clean):
    if clean == False:
        return 0

    KNET_BUILD_DIR = f"{KNET_SOURCE_DIR}/build"
    if os.path.exists(KNET_BUILD_DIR):
        shutil.rmtree(KNET_BUILD_DIR)
        logging.info(f"Deleted {KNET_BUILD_DIR} directory.")
    return 1

if __name__ == "__main__":
    shutil.rmtree(f"{KNET_SOURCE_DIR}/build", ignore_errors=True)

    arg_nums = len(sys.argv)
    debugMode = False
    testMode = False
    sdvMode = False
    cleanMode = False
    fuzzMode = False
    if arg_nums > 1:
        if sys.argv[1] == 'debug':
            debugMode = True
        elif sys.argv[1] == 'test':
            testMode = True
        elif sys.argv[1] == 'sdv':
            sdvMode = True
        elif sys.argv[1] == 'clean':
            cleanMode = True
        elif sys.argv[1] == 'fuzz':
            fuzzMode = True

    ret = clean_build(cleanMode)
    if ret != 0:
        sys.exit(0)

    # 若没有yum参数，才执行cjson、dpdk、securec的构建，否则跳过直接使用yum依赖
    use_opensource = "yum" not in sys.argv
    if use_opensource:
        ret = build_cjson()
        if ret != 0:
            sys.exit(1)

        # DPDK 来源：
        #   1. 设置了 KNET_DPDK_PATH 环境变量：软链复用外部 DPDK，跳过编译
        #   2. 未设置 KNET_DPDK_PATH：从源码 clone + meson/ninja 编译
        dpdk_path_env = os.environ.get('KNET_DPDK_PATH')
        if dpdk_path_env:
            logging.info(f"检测到环境变量 KNET_DPDK_PATH，使用外部DPDK: {dpdk_path_env}")
            logging.warning("DPDK 版本兼容性由用户自行保证，knet 不做版本校验")
            ret = use_external_dpdk()
        else:
            ret = build_dpdk()
        if ret != 0:
            sys.exit(1)

        ret = build_securec()
        if ret != 0:
            sys.exit(1)

    ret = build_knet(debugMode, testMode, sdvMode, fuzzMode, use_opensource)
    if ret != 0:
        sys.exit(1)

    if testMode or sdvMode:
        ret = build_test()
        if ret != 0:
            sys.exit(1)

    # 检查命令行参数中是否包含"rpm"，若有则执行rpm打包
    if "rpm" in sys.argv:
        if build_rpm() != 0:
            sys.exit(1)
