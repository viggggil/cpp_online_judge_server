---
document_id: algo_complexity_analysis
title: "复杂度分析常用方法"
category: algorithm
tags: ["complexity", "time-complexity", "space-complexity", "amortized"]
difficulty: beginner
safe_level: knowledge
source_type: original_summary
external_sources:
  - "https://oi-wiki.org/basic/complexity/"
---

# 复杂度分析常用方法

复杂度用于估计程序运行时间和内存随输入规模增长的趋势。OJ 诊断时，复杂度分析常用来解释 Time Limit Exceeded、Memory Limit Exceeded 和数据范围不匹配。

## 常见时间复杂度

- `O(1)`：常数次操作。
- `O(log n)`：二分、平衡树、倍增等。
- `O(n)`：单次线性扫描。
- `O(n log n)`：排序、分治合并、很多启发式合并。
- `O(n^2)`：双重循环，`n=2e5` 时通常不可接受。
- `O(nm)`：两个规模同时参与，需结合数据范围判断。

## 分析步骤

1. 找到主循环或递归。
2. 判断每个元素会被处理多少次。
3. 数据结构操作要乘上单次复杂度，例如 `set` 操作通常是 `O(log n)`。
4. 多组测试要看所有测试的规模总和。
5. 对“删除一次后不再出现”的模拟，可以用均摊思想分析为线性。

## OJ 常见诊断

- 如果 `n` 达到 `2e5`，通常需要 `O(n)` 或 `O(n log n)`。
- 如果所有测试 `n` 总和受限，算法复杂度应按总和估算。
- 对树上问题，重复扫描每个子树容易退化为 `O(n^2)`。
- 对区间问题，逐区间扫描可能退化为所有区间长度之和。

## 空间复杂度

空间复杂度关注额外数组、图邻接表、哈希表、递归栈等。图的邻接表一般是 `O(n + m)`，树上若为每个节点存一份完整 map，可能达到 `O(n^2)`，需要共享、合并或清空。
