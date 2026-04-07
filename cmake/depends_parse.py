#!/usr/bin/env python
# coding=utf-8
# parse dependency with transitive dependency resolution
# Copyright (C) Huawei Technologies Co., Ltd. 2025. All rights reserved.

import sys
import os
import json

MODULE_DEPS_MAP = {}

def load_all_specs(spec_dir):
    global MODULE_DEPS_MAP
    
    for root, dirs, files in os.walk(spec_dir):
        for f in files:
            if not f.endswith('.spec'):
                continue
                
            spec_path = os.path.join(root, f)
            module_name = f.split('.')[0]
            
            if module_name in MODULE_DEPS_MAP:
                continue
                
            _process_spec_file(spec_path, module_name)


def _process_spec_file(spec_path, module_name):
    global MODULE_DEPS_MAP
    
    try:
        with open(spec_path, 'r', encoding='utf-8') as sf:
            data = json.loads(sf.read())
            depends_str = data.get("depends", "")
            deps = [d.strip() for d in depends_str.split(',') if d.strip()] if depends_str else []
            MODULE_DEPS_MAP[module_name] = deps
    except (json.JSONDecodeError, IOError):
        MODULE_DEPS_MAP[module_name] = []


def get_transitive_deps(module_name, visited=None):
    if visited is None:
        visited = set()
    
    if module_name in visited:
        return set()
    
    visited.add(module_name)
    
    direct_deps = MODULE_DEPS_MAP.get(module_name, [])
    all_deps = set()
    
    for dep in direct_deps:
        if dep == '' or dep == module_name:
            continue
        all_deps.add(dep)
        all_deps.update(get_transitive_deps(dep, visited))
    
    return all_deps


def find_redundant_deps(module_name):
    direct_deps = MODULE_DEPS_MAP.get(module_name, [])
    if not direct_deps:
        return []
    
    redundant = []
    
    for dep in direct_deps:
        if dep == '' or dep == module_name:
            continue
        
        transitive_from_others = set()
        for other_dep in direct_deps:
            if other_dep != dep and other_dep != '':
                transitive_from_others.update(get_transitive_deps(other_dep))
        
        if dep in transitive_from_others:
            redundant.append(dep)
    
    return redundant


def get_minimal_deps(module_name):
    direct_deps = MODULE_DEPS_MAP.get(module_name, [])
    redundant = find_redundant_deps(module_name)
    minimal = [d for d in direct_deps if d not in redundant and d != '']
    return minimal, redundant


def set_spec(module_name, deps, cmake_file_path):
    with open(cmake_file_path, 'w', encoding='utf-8') as out:
        out.write('#auto generated file. DO NOT EDIT\n')
        out.write('set(%s_depend_module "")\n' % module_name)
        for depend in sorted(deps):
            if depend == '' or depend == module_name:
                continue
            out.write('list(APPEND %s_depend_module %s)\n' % (module_name, depend))


def find_project_root(spec_file):
    dir_path = os.path.dirname(os.path.abspath(spec_file))
    
    while dir_path != os.path.dirname(dir_path):
        cmake_path = os.path.join(dir_path, 'CMakeLists.txt')
        if not os.path.exists(cmake_path):
            dir_path = os.path.dirname(dir_path)
            continue
            
        if _is_project_root(cmake_path):
            return dir_path
            
        dir_path = os.path.dirname(dir_path)
    
    return os.path.dirname(os.path.abspath(spec_file))


def _is_project_root(cmake_path):
    try:
        with open(cmake_path, 'r') as f:
            cmake_content = f.read()
        return 'project(K-NET' in cmake_content or 'project(DP' in cmake_content
    except (IOError, FileNotFoundError):
        return False


def main():
    if len(sys.argv) < 3:
        print("Usage: depends_parse.py <spec_file> <cmake_output_file>")
        sys.exit(1)
    
    spec_file = sys.argv[1]
    cmake_file = sys.argv[2]
    
    module = os.path.basename(spec_file).split('.')[0]
    project_root = find_project_root(spec_file)
    
    load_all_specs(project_root)
    
    direct_deps = MODULE_DEPS_MAP.get(module, [])
    
    all_deps = get_transitive_deps(module)
    all_deps.discard(module)
    
    minimal_deps, redundant_deps = get_minimal_deps(module)
    
    print(f"Module: {module}")
    print(f"Direct dependencies: {direct_deps}")
    if redundant_deps:
        print(f"Redundant dependencies (can be removed): {redundant_deps}")
    print(f"Minimal dependencies: {minimal_deps}")
    print(f"All dependencies (including transitive): {sorted(all_deps)}")
    
    set_spec(module, all_deps, cmake_file)
    print(f"Generated cmake file: {cmake_file}")


if __name__ == "__main__":
    main()
