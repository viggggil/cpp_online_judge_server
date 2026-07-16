---
document_id: algo_scc_reachability
title: "强连通分量、缩点与可达性"
category: algorithm
tags: ["graph", "scc", "tarjan", "kosaraju", "reachability", "dag"]
difficulty: medium
safe_level: knowledge
source_type: original_summary
external_sources:
  - "https://oi-wiki.org/graph/scc/"
  - "https://oi-wiki.org/graph/concept/"
---

# 强连通分量、缩点与可达性

在有向图中，如果一组点两两互相可达，这组点可以看成一个强连通块。强连通分量缩点后得到的图一定是 DAG。

## 常见用途

- 判断有向图中的环结构。
- 把互相可达的一组点压缩成一个状态。
- 在缩点 DAG 上统计入度、出度、可达性。
- 解决“最少加边使某些点可达”的问题。

## Tarjan 思路

Tarjan 使用 DFS 序和 lowlink 维护当前搜索栈。当发现某个点 `u` 满足 `dfn[u] == low[u]` 时，说明 `u` 是一个 SCC 的根，可以从栈中弹出该 SCC。

## 缩点后统计

缩点时，对原图每条边 `u -> v`，如果 `scc[u] != scc[v]`，就在 DAG 中加入 `scc[u] -> scc[v]`。

如果要让某个源点能到达所有点，可以先标记源点所在 SCC 能到达的部分，再在剩余 SCC 中统计入度为 0 的块。

## 复杂度

求 SCC 和缩点都可以做到 `O(n + m)`。

## 常见错误

1. 没有清除多组测试的全局数组。
2. lowlink 更新时混淆树边、返祖边和已出栈节点。
3. 缩点后重复边虽然通常不影响入度是否为 0，但可能影响计数或后续 DP。
4. 在原图上直接统计入度，忽略了强连通块内部互相可达。
