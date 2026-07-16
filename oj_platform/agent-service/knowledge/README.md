# Agent Service Markdown Knowledge Base

This directory stores the source Markdown documents for the Agent Service RAG
index. The files are the canonical local knowledge base; Chroma should be
rebuilt from this directory when documents change.

## Current Coverage

Problem editorials:

| Problem ID | Title | Document |
|---:|---|---|
| 1000 | a+b problem | `problem_hints/problem-1000-a-plus-b.md` |
| 1001 | Reachability from the Capital | `problem_hints/problem-1001-reachability-from-capital.md` |
| 1002 | Lomsat gelral | `problem_hints/problem-1002-lomsat-gelral.md` |
| 1003 | Two Teams | `problem_hints/problem-1003-two-teams.md` |
| 1007 | Brackets | `problem_hints/problem-1007-brackets.md` |

Algorithm notes:

| Topic | Document |
|---|---|
| Complexity analysis | `algorithms/complexity-analysis.md` |
| Hash table / two sum | `algorithms/hash-table-two-sum.md` |
| Sliding window / two pointers | `algorithms/sliding-window.md` |
| Merge sorted arrays | `algorithms/merge-sorted-arrays.md` |
| SCC and reachability | `algorithms/scc-reachability.md` |
| DSU on tree | `algorithms/dsu-on-tree.md` |
| Greedy linked-list simulation | `algorithms/greedy-linked-list-simulation.md` |
| Bracket normalization and hashing | `algorithms/bracket-normalization-hashing.md` |

## Document Rules

Each document must start with YAML frontmatter:

```yaml
---
document_id: stable_unique_id
title: "Human readable title"
category: algorithm | problem_editorial | cpp_error | complexity
problem_id: 1000
tags: ["graph", "dfs"]
difficulty: beginner | medium | hard
safe_level: knowledge | editorial
source_type: original | original_summary
external_sources:
  - "https://example.com/reference"
---
```

For non-problem documents, omit `problem_id`.

## Source Policy

- Problem editorials in this directory are original explanations written for
  this project.
- External pages such as OI Wiki or Codeforces are used only as references for
  concepts, problem identity, and terminology.
- Do not copy external editorials or large passages into this repository.
- Keep external URLs in `external_sources` so generated answers can expose
  retrieval provenance later.

## Database Relationship

MySQL stores problem metadata, tags, submissions, conversations, and messages.
There is currently no dedicated `problem_editorial` table. For the RAG MVP,
these Markdown files are the knowledge database and should be indexed into
Chroma by the Agent Service.

## Build The Chroma Index

The current local embedding model is `BAAI/bge-small-zh-v1.5`, executed through
`fastembed`.

```bash
cd oj_platform/agent-service
uv run python scripts/ingest_knowledge.py
```

If `huggingface.co` is unreachable in the local network, set:

```bash
export HF_ENDPOINT=https://hf-mirror.com
```

The command rebuilds the `oj_agent_knowledge` Chroma collection from this
directory. Generated files are stored under:

```text
data/chroma/
data/fastembed/
```

Both directories are runtime artifacts and are ignored by Git.
