#!/usr/bin/env python
# coding=utf-8
# parse dependency
# Copyright (C) Huawei Technologies Co., Ltd. 2025. All rights reserved.

import sys
import os
import json

def GetSpec(spec_file):
    with open(spec_file, 'r') as specfile:
        print(f"spec_file:{spec_file}")
        deps = json.loads(specfile.read()).get("depends").replace(' ', '').split(',')
    return deps

def SetSpec(module, deps, cmake_file):
    with open(cmake_file, 'w') as out:
        out.write('#auto generated file. DO NOT EDIT\n')
        out.write('set(%s_depend_module "")\n' % module)
        for dep in deps:
            if dep == '':
                continue
            out.write('list(APPEND %s_depend_module %s)\n' % (module, dep))
    return

spec_file   = sys.argv[1]
cmake_file  = sys.argv[2]
module = spec_file.split('/')[-1].split('.')[0]

deps = GetSpec(spec_file)
print(deps)

SetSpec(module, deps, cmake_file)