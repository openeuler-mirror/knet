#!/usr/bin/env python
# coding=utf-8
# parse dependency
# Copyright (C) Huawei Technologies Co., Ltd. 2025. All rights reserved.

import sys
import os
import json


def get_spec(spec_file_path):
    with open(spec_file_path, 'r') as specfile:
        depends = json.loads(specfile.read()).get("depends").replace(' ', '').split(',')
    return depends


def set_spec(module_name, depends, cmake_file_path):
    with open(cmake_file_path, 'w') as out:
        out.write('#auto generated file. DO NOT EDIT\n')
        out.write('set(%s_depend_module "")\n' % module_name)
        for depend in depends:
            if depend == '':
                continue
            out.write('list(APPEND %s_depend_module %s)\n' % (module_name, depend))
    return


spec_file = sys.argv[1]
cmake_file = sys.argv[2]
module = spec_file.split('/')[-1].split('.')[0]

deps = get_spec(spec_file)

set_spec(module, deps, cmake_file)