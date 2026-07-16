---
document_id: algo_dsu_on_tree
title: "树上启发式合并 DSU on Tree"
category: algorithm
tags: ["tree", "dsu-on-tree", "small-to-large", "dfs", "subtree-query"]
difficulty: hard
safe_level: knowledge
source_type: original_summary
external_sources:
  - "https://oi-wiki.org/graph/dsu-on-tree/"
---

# 树上启发式合并 DSU on Tree

树上启发式合并适合处理静态树上的子树统计问题，例如每个子树中颜色种类数、颜色出现次数、众数颜色和等。

## 核心思想

对于每个节点，保留它最大儿子的统计结构，把其他小子树的信息合并进去。这样每个点被重复加入的次数与轻边数量有关，总体比每个节点暴力扫整棵子树快得多。

## 标准流程

1. DFS 预处理 `size[u]` 和 `heavy[u]`。
2. 处理轻儿子，处理后清空贡献。
3. 处理重儿子，保留贡献。
4. 把轻儿子的所有节点重新加入统计结构。
5. 加入当前节点。
6. 记录当前节点答案。
7. 如果父层不需要保留，则清空当前子树贡献。

## 维护统计

根据题目需要设计统计结构：

- `cnt[color]`：颜色出现次数。
- `max_freq`：最大频率。
- `sum`：满足最大频率的颜色编号和。
- `distinct`：不同颜色数量。

每次加入或删除节点时同步更新这些变量。

## 复杂度

常见实现为 `O(n log n)`；结合 DFS 序和重儿子可以获得较好的常数。空间通常为 `O(n)`。

## 常见错误

1. 清空时没有重置维护变量。
2. 轻儿子贡献重复加入。
3. 重儿子处理后错误清空，导致失去优化。
4. 递归深度过大。
5. 颜色值域很大时忘记离散化。
