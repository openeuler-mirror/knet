# 动态库缺失故障

## 内存边界检查动态库缺失故障

### 现象描述

K-NET加速应用失败，出现以下报错：

```
***: error while loading shared libraries: libboundscheck.so: cannot open shared object file: No such file or directory
```

### 原因

缺少libboundscheck.so动态库

### 处理步骤

回到执行build.py编译脚本的K-NET项目路径下，执行以下命令拷贝libboundscheck.so动态库：
```
cp ./build/opensource/lib/securec/libboundscheck.so /usr/lib64/
```