---
document_id: problem_1001_reachability_from_capital
title: "1001 Reachability from the Capital 题解"
category: problem_editorial
problem_id: 1001
problem_title: "Reachability from the Capital"
tags: ["graph", "dfs", "scc", "reachability", "greedy"]
difficulty: medium
safe_level: editorial
source_type: original
external_sources:
  - "https://codeforces.com/problemset/problem/999/E"
  - "https://oi-wiki.org/graph/scc/"
  - "https://oi-wiki.org/graph/dfs/"
---

# 1001 Reachability from the Capital 题解

## 问题转化

给定有向图和首都 `s`，允许新增若干条单向边，目标是让所有点都能从 `s` 到达。

先从 `s` 做一次 DFS/BFS，已经可达的点不需要处理。剩余点之间可能互相可达，也可能形成环。对于有向图，环内所有点互相可达，可以看成一个整体，这就是强连通分量。

## 强连通分量做法

1. 用 Tarjan 或 Kosaraju 求出所有 SCC。
2. 把每个 SCC 缩成一个点，得到一张 DAG。
3. 找到首都 `s` 所在的 SCC，从它出发在缩点 DAG 上标记可达 SCC。
4. 在不可达 SCC 中，统计入度为 `0` 的 SCC 个数。这里的入度只考虑来自其他不可达 SCC 的边。

答案就是这些入度为 `0` 的不可达 SCC 数量。

原因是：每个不可达源 SCC 没有办法从其他不可达 SCC 进入，也不能从首都到达，所以至少要给它补一条边。给每个这样的 SCC 从某个已可达点连一条边后，它沿着 DAG 后继能覆盖一批不可达 SCC，因此这些边也足够。

## 另一种贪心 DFS 思路

由于原题数据范围 `n,m <= 5000`，也可以先标记首都可达点，然后对每个不可达点计算它能扩展覆盖多少当前不可达点，按覆盖量从大到小尝试 DFS。这个思路实现短，但 SCC 解释更清晰，复杂度边界也更稳。

## 复杂度

Tarjan/Kosaraju 和缩点统计总复杂度为 `O(n + m)`，空间复杂度为 `O(n + m)`。

## 常见错误

1. 只统计原图中入度为 `0` 的点，而不是统计缩点后的 SCC。
2. 把来自已可达 SCC 的边也算进不可达子图的入度，导致少算答案。
3. 忘记首都本身所在 SCC 及其后继都已经可达。
4. 递归 DFS 在更大数据下可能爆栈，可以改成迭代或提高栈限制。

## 给学生的分级提示

Level 1：先想清楚哪些城市已经能从首都到达，这部分不需要新边。

Level 2：不可达部分里，某些点集没有任何入口，它们必须各自获得一条新边。

Level 3：把有向环缩成一个点后，在不可达的缩点 DAG 中数入度为 `0` 的连通块。
