#!/bin/bash
# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 
# K-NET is licensed under the Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#      http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.

set -e

readonly PROJECT_ROOT="$(dirname $(readlink -f $0))"

readonly KNET_DIR=${PROJECT_ROOT}/..

cd ../
python build.py debug
cd -
rm -rf ./ut/build
mkdir -p ./ut/build
pushd ./ut/build
    cmake ..
    make -j8
popd
