# C：筛选模块实现目录

该目录由 C 负责。`DefaultFilterManager.cpp` 实现扩展名白名单/黑名单、排除路径、文件大小和隐藏属性规则。扩展名及 Windows 路径匹配忽略 ASCII 大小写，目录不受文件扩展名和大小规则影响。

```text
src/filter/
└─ DefaultFilterManager.cpp
```

筛选模块通过 `FilterManager` 公共契约供核心遍历调用，具体规则测试位于 `tests/filter/`。
