---
document_id: problem_1002_lomsat_gelral
title: "1002 Lomsat gelral 题解"
category: problem_editorial
problem_id: 1002
problem_title: "Lomsat gelral"
tags: ["tree", "dfs", "dsu-on-tree", "small-to-large", "data-structures"]
difficulty: hard
safe_level: editorial
source_type: original
external_sources:
  - "https://codeforces.com/problemset/problem/600/E"
  - "https://codeforces.com/blog/entry/21827"
  - "https://oi-wiki.org/graph/dsu-on-tree/"
---

# 1002 Lomsat gelral 题解

## 问题转化

对每个节点 `v`，需要统计 `v` 子树内出现次数最多的颜色。如果有多个颜色并列最多，答案是这些颜色编号之和。

暴力做法是对每个节点扫描整棵子树，会达到 `O(n^2)`，无法通过 `n = 100000`。

## DSU on Tree / Sack

这题是树上启发式合并的经典应用。核心思想是：处理一个节点时，保留最大儿子子树的统计表，再把其他轻儿子的信息合并进去。

基本流程：

1. 第一次 DFS 预处理每个节点的子树大小和重儿子。
2. 第二次 DFS：
   - 先处理所有轻儿子，处理完后清空它们对颜色计数的影响。
   - 再处理重儿子，并保留它的颜色计数。
   - 把所有轻儿子的节点重新加入当前计数表。
   - 加入当前节点颜色。
   - 此时计数表正好表示当前节点整棵子树，可以得到答案。
   - 如果当前递归分支不需要保留，再清空当前子树贡献。

维护变量：

- `cnt[color]`：当前保留集合中某颜色出现次数。
- `max_freq`：当前最大出现次数。
- `sum_colors`：所有出现次数等于 `max_freq` 的颜色编号之和。

每次加入一个颜色 `c`：

1. `cnt[c] += 1`
2. 如果 `cnt[c] > max_freq`，更新 `max_freq = cnt[c]`，并令 `sum_colors = c`
3. 如果 `cnt[c] == max_freq`，把 `c` 加入 `sum_colors`

## 复杂度

标准树上启发式合并为 `O(n log n)`，若结合 DFS 序和重儿子优化，实际常数较好。空间复杂度 `O(n)`。

## 常见错误

1. 没有区分轻儿子和重儿子，导致重复清空或重复统计。
2. 更新 `sum_colors` 时忘记在出现更大频率时清零。
3. 颜色编号较大时没有做离散化或没有开足数组。
4. 清空贡献时没有同步恢复 `cnt`，导致兄弟子树互相污染。
5. 递归深度过大，需要注意栈空间。

## 给学生的分级提示

Level 1：每个节点都要回答“子树内哪个颜色出现最多”，朴素重复扫描会超时。

Level 2：可以保留最大子树的统计结果，再把小子树合进去，避免重复工作。

Level 3：预处理重儿子；处理轻儿子时不保留，处理重儿子时保留，然后把轻儿子重新加入统计表。
