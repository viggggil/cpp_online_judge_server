---
document_id: algo_greedy_linked_list_simulation
title: "贪心选择与链表删除模拟"
category: algorithm
tags: ["greedy", "linked-list", "simulation", "set", "data-structures"]
difficulty: medium
safe_level: knowledge
source_type: original_summary
external_sources:
  - "https://oi-wiki.org/basic/greedy/"
---

# 贪心选择与链表删除模拟

有些模拟题每轮都要选择当前最大或最小元素，并删除它附近的一段元素。朴素数组删除会导致 `O(n^2)`，应维护“仍然存在的元素之间的邻接关系”。

## 常见结构

1. `pos[value]`：由权值找到位置。
2. `alive[position]`：位置是否仍存在。
3. `pre[position]` 和 `nxt[position]`：双向链表，维护左右最近未删除位置。

如果权值不是排列，也可以用 `set`、堆加懒删除或平衡树维护当前最大元素。

## 删除操作

删除位置 `x` 时：

- `L = pre[x]`
- `R = nxt[x]`
- 如果 `L` 存在，`nxt[L] = R`
- 如果 `R` 存在，`pre[R] = L`

每个位置最多删除一次，因此删除总成本是线性的。

## 复杂度

如果最大元素能通过排列下标直接逆序枚举，总复杂度 `O(n)`。如果用 `set` 或堆维护最大元素，通常是 `O(n log n)`。

## 常见错误

1. 边遍历边删除导致指针丢失。
2. 删除后仍使用已删除节点作为中心。
3. 忘记处理链表边界。
4. 一轮选择左右元素时没有限制最多 `k` 个。
