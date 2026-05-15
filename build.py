#!/usr/bin/env python3
# -*- encoding:utf-8 -*-
# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

import os
import sys
import shutil
import logging
import subprocess

KNET_SOURCE_DIR = os.path.abspath(os.path.dirname(__file__))
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

    # 生成libdpdk.so
    os.chdir(f"{KNET_SOURCE_DIR}/opensource/dpdk/output/lib64")
    os.environ['PKG_CONFIG_PATH'] = f"{KNET_SOURCE_DIR}/opensource/dpdk/output/lib64/pkgconfig"

    cmd = ['gcc']
    cmd.extend(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'libdpdk', '--static']).decode('utf-8').strip().split())
    cmd.extend(['-shared', '-o', 'libdpdk.so'])

    try:
        output = subprocess.Popen(cmd)
        output.wait()
    except subprocess.CalledProcessError as e:
        print(f"cmd fail. [{cmd}]")
        print(e.output)
        raise e

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
