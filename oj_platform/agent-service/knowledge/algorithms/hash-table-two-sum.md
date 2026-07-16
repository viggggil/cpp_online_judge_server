---
document_id: algo_hash_table_two_sum
title: "哈希表与两数之和"
category: algorithm
tags: ["hash-table", "array", "two-sum", "unordered_map"]
difficulty: beginner
safe_level: knowledge
source_type: original_summary
external_sources:
  - "https://oi-wiki.org/ds/hash/"
---

# 哈希表与两数之和

两数之和类问题通常要求在数组中找两个不同下标，使它们满足某个关系，例如 `a[i] + a[j] = target`。

## 基本思路

从左到右扫描数组。对于当前值 `x`，需要找之前是否出现过 `target - x`。如果出现过，就找到了答案；否则把 `x` 和当前下标加入哈希表。

## 为什么要先查再插入

题目要求两个下标不同。如果先把当前元素插入，再查询补数，可能在 `x * 2 = target` 时错误地使用同一个下标。

## 数据结构

C++ 中常用：

- `unordered_map<long long, int>`：平均 `O(1)` 查询和插入。
- `map<long long, int>`：`O(log n)`，但最坏复杂度更稳定。

如果值域较小，也可以用数组计数或位置表。

## 复杂度

使用哈希表时，平均时间复杂度 `O(n)`，空间复杂度 `O(n)`。

## 常见错误

1. 输出值而不是下标。
2. 输出下标时没有按从小到大排序。
3. 使用 `int` 计算补数时溢出。
4. 同一个下标被使用两次。
5. `unordered_map` 中重复值覆盖旧下标时没有考虑题目是否允许重复值。
