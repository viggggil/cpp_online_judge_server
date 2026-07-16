---
document_id: algo_bracket_normalization_hashing
title: "括号序列规约、栈与哈希"
category: algorithm
tags: ["brackets", "stack", "hashing", "strings", "normalization"]
difficulty: medium
safe_level: knowledge
source_type: original_summary
external_sources:
  - "https://oi-wiki.org/ds/stack/"
  - "https://oi-wiki.org/string/hash/"
---

# 括号序列规约、栈与哈希

括号序列问题常用栈判断合法性。对于多种括号，除了左右数量，还必须检查类型和嵌套顺序。

## 栈判断合法性

扫描字符串：

1. 左括号入栈。
2. 右括号要求栈顶是同类型左括号。
3. 如果不匹配或栈为空，则序列不合法。
4. 扫描结束后栈为空才合法。

## 规约剩余串

对于需要拼接配对的问题，可以消去内部已经匹配的括号，只保留无法在当前串内部匹配的部分。两个串能否拼成合法序列，取决于第一个串的剩余部分和第二个串的剩余部分是否按方向、类型和顺序互补。

## 哈希表示

如果剩余串很长，直接存字符串可能较慢。可以使用字符串哈希表示 key，并为互补串也计算 key。为了降低冲突风险，建议使用双哈希或同时保存长度。

## 常见错误

1. 只统计左右括号数量。
2. 忽略不同括号类型。
3. 忽略拼接方向，`A+B` 与 `B+A` 条件不同。
4. 哈希没有保存长度，导致不同长度串出现同 key 风险更高。
5. 合法空串需要两两配对，不能单独贡献答案。
