# 示例测试数据

在程序“备份”页中填写：

- 源目录：本目录下的 `source`
- 备份仓库：本目录下的 `backup-output`
- 备份名称：`course-demo`
- 备份模式：完整备份

第一次备份完成后，编辑 `source/示例文档.txt`，把“版本”从 `1` 改成 `2`，再选择增量备份。最后在“还原”页选择最新生成的备份目录，并将其还原到 `restore-output`。

`backup-output` 和 `restore-output` 中生成的内容已被 Git 忽略，不会混入课程项目源码。
