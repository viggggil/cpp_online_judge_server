# problems

该目录用于存放题库数据。

当前约定：**每道题一个目录**，目录名即题号。

例如：

```text
problems/1000/
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

当前题目以训练和演示为主，后续可以继续补充：

- 更多算法题
- 特殊判题题目
- 文件输入输出题目
- 数据范围更大的压力测试题目