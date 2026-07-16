---
document_id: problem_1003_two_teams
title: "1003 Two Teams 题解"
category: problem_editorial
problem_id: 1003
problem_title: "Two Teams"
tags: ["greedy", "linked-list", "set", "data-structures", "simulation"]
difficulty: medium
safe_level: editorial
source_type: original
external_sources:
  - "https://codeforces.com/problemset/problem/1154/E"
  - "https://oi-wiki.org/basic/greedy/"
---

# 1003 Two Teams 题解

## 核心观察

每一轮一定会选当前剩余学生中能力值最大的学生作为中心，然后向左右各扩展最多 `k` 个仍未被选走的学生。能力值是 `1..n` 的排列，因此可以直接从能力值 `n` 到 `1` 逆序找中心。

难点不在“选谁做中心”，而在快速找到某个位置左右最近的未删除位置。

## 数据结构

可以维护一条“仍未被选走”的双向链表：

- `pre[i]` 表示当前位置左边最近的未删除位置。
- `nxt[i]` 表示当前位置右边最近的未删除位置。
- 删除位置 `x` 时，把 `pre[x]` 和 `nxt[x]` 连接起来。

同时准备 `pos[value]`，表示能力值为 `value` 的学生原始位置。按 `value = n..1` 扫描，如果 `pos[value]` 已经被选走，就跳过；否则它就是本轮中心。

## 模拟流程

1. 当前队伍编号从 `1` 开始，每完成一轮在 `1` 和 `2` 之间切换。
2. 从大到小枚举能力值，找到还未被删除的中心位置 `p`。
3. 先选中 `p`。
4. 从 `p` 向左沿 `pre` 走最多 `k` 次，记录要删除的位置。
5. 从 `p` 向右沿 `nxt` 走最多 `k` 次，记录要删除的位置。
6. 给这些位置赋当前队伍编号，并从链表中删除。

## 复杂度

每个学生只会被删除一次，每次删除是 `O(1)`，整体复杂度 `O(n)`。如果使用 `set` 找左右邻居，也可以做到 `O(n log n)`。

## 常见错误

1. 删除中心后再找左右邻居，导致左右扩展断掉。应先收集本轮要选的位置，再统一删除，或小心保存左右指针。
2. 忘记跳过已经被选走的最大能力值位置。
3. 队伍编号切换时机错误，应在完成一整轮选择后切换。
4. `k` 可能大于当前一侧剩余人数，需要走到边界就停止。

## 给学生的分级提示

Level 1：每轮中心一定是当前剩余能力值最大的人。

Level 2：关键是维护“删除后左右最近的剩余位置”。

Level 3：用数组模拟双向链表，删除一个位置时让它的前驱和后继直接相连。
