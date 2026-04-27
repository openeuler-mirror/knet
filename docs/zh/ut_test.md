# 执行UT用例

可以按照如下方式，手动编译和执行UT用例。

```shell
python build.py
cd test/sdv_new/scripts
# 执行UT构建脚本并下载依赖
sh unit_test.sh t
# 设置系统资源上限
ulimit -n 65535
# 运行UT测试用例
sh unit_test.sh r
# 生成测试报告
sh unit_test.sh g
# UT覆盖率测试结果存放在build目录中，可解压查看
cd ../build ; tar -zxf lcov_result.tgz
start ./lcov_result/index.html
```
