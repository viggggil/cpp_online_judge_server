---
document_id: algo_sliding_window
title: "双指针与滑动窗口"
category: algorithm
tags: ["two-pointers", "sliding-window", "string", "array"]
difficulty: beginner
safe_level: knowledge
source_type: original_summary
external_sources:
  - "https://oi-wiki.org/misc/two-pointer/"
---

# 双指针与滑动窗口

双指针常用于维护一个区间 `[l, r]`。当右端点向右扩展时，根据约束移动左端点，使区间重新合法。

## 适用场景

- 最长不重复子串。
- 和、数量或种类满足某种单调条件的区间。
- 两个有序数组的合并。
- 排序数组中的配对问题。

## 最长不重复子串

维护每个字符上一次出现的位置。扫描到位置 `r` 的字符 `c` 时，如果 `c` 上次出现位置 `last[c] >= l`，说明当前窗口中已经有 `c`，需要把 `l` 移到 `last[c] + 1`。然后更新答案 `r - l + 1`。

## 复杂度

每个指针最多向右移动 `n` 次，因此时间复杂度 `O(n)`。如果字符集固定，空间复杂度 `O(1)`；否则为 `O(字符种类数)`。

## 常见错误

1. 左指针回退，破坏线性复杂度。
2. 遇到重复字符时把左指针移动到过早位置。
3. 更新答案的时机错误。
4. 对空串或长度为 1 的字符串边界处理不当。
5. ASCII、Unicode、字节长度混淆。
