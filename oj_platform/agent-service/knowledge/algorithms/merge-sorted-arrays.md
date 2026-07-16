---
document_id: algo_merge_sorted_arrays
title: "合并两个有序数组"
category: algorithm
tags: ["two-pointers", "merge", "array", "sorting"]
difficulty: beginner
safe_level: knowledge
source_type: original_summary
external_sources:
  - "https://oi-wiki.org/misc/two-pointer/"
---

# 合并两个有序数组

给定两个升序数组，可以用两个指针分别指向当前未合并的最小元素。

## 做法

1. `i = 0, j = 0`。
2. 当两个数组都未结束时，比较 `a[i]` 和 `b[j]`，把较小者加入结果并移动对应指针。
3. 如果相等，可以先加入任意一边，再加入另一边，最终结果仍然有序。
4. 一个数组结束后，把另一个数组剩余元素全部追加。

## 复杂度

每个元素恰好进入答案一次，时间复杂度 `O(n + m)`，额外结果数组空间 `O(n + m)`。

## 常见错误

1. 忘记处理其中一个数组为空。
2. 主循环结束后忘记追加剩余元素。
3. 输出格式多空格一般可接受，但有些自定义 checker 可能严格，建议统一控制。
4. 把输入数组误认为严格递增，实际上题目只说升序，允许相等。
