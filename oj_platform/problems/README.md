# problems

该目录用于存放题库源文件。

目录约定为：**每道题一个目录**，目录名即题号。

示例：

```text
problems/<problem_id>/
├─ meta.json
├─ statement_zh.md
├─ checker.cpp
└─ tests/
   ├─ 1.in
   └─ 1.out
```

文件职责：

- `meta.json`：题目元数据（题号、标题、时空限制、标签）
- `statement_zh.md`：中文题面
- `tests/*.in`：测试输入
- `tests/*.out`：标准输出
- `checker.cpp`：特殊判题器预留
