---
document_id: problem_1000_a_plus_b
title: "1000 A+B Problem 题解"
category: problem_editorial
problem_id: 1000
problem_title: "a+b problem"
tags: ["implementation", "math", "io", "beginner"]
difficulty: beginner
safe_level: editorial
source_type: original
---

# 1000 A+B Problem 题解

## 核心思路

这是一道输入输出与整数加法入门题。读入两个整数 `a` 和 `b`，输出 `a + b`。

## 常见错误

1. 把加法写成减法、乘法或其他运算。
2. 忘记读入两个数，只读入一个数。
3. 输出了多余文字，导致 OJ 严格比较失败。
4. 使用了过小的数据类型。虽然当前数据范围很小，但养成使用 `long long` 处理整数和的习惯更稳。

## 复杂度

时间复杂度为 `O(1)`，空间复杂度为 `O(1)`。

## 给学生的分级提示

Level 1：检查题目要求的是哪一种运算。

Level 2：确认代码中输出表达式确实是两个输入整数的和。

Level 3：如果读入变量名是 `a` 和 `b`，输出表达式应该围绕 `a + b` 构造。
