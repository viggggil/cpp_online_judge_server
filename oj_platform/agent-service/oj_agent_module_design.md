# Agent Service 模块设计文档

> 文档状态：当前实现说明  
> 模块目录：`oj_platform/agent-service`  
> 服务定位：在线判题平台的 AI 编程学习助手后端  
> 更新时间：2026-07-21

## 1. 模块定位

`agent-service` 是一个 Python FastAPI 服务，负责为 OJ 平台提供 AI 编程学习助手能力。它不直接面对浏览器，不直接访问 MySQL，也不直接信任前端输入的用户身份。所有用户请求先进入 C++ `oj_server`，由 `oj_server` 完成登录态校验、会话归属校验、上下文恢复和数据库持久化，再通过内部 API 调用 `agent-service`。

当前 Agent 主链路是通用多轮聊天，不再以固定“提交诊断结构化输出”为业务中心。用户可以开启新对话，也可以继续历史会话；可以问题目思路、算法概念、提交错误、复杂度、上一轮解释等问题。Agent 会根据本轮问题、最近历史和当前上下文自行规划是否需要查询题目、提交或本地知识库。

核心职责：

- 基于 LLM Planner 规划本轮回答需要的工具调用。
- 使用 LangChain `StructuredTool` 封装受控工具。
- 通过 `oj_server /api/ai/*` 内部接口查询题目、提交和会话数据。
- 使用 Chroma + fastembed 检索本地 Markdown 知识库。
- 构造回答 prompt，并通过 OpenRouter 调用模型。
- 以 SSE 事件形式返回状态、工具调用、工具结果、RAG 来源、模型增量和最终回答。
- 对模型调用超时、流式中断和 planner 失败做降级兜底。

## 2. 总体架构

```text
Browser
  |
  | JWT
  v
oj_server (C++ Crow)
  |
  | internal token + request id
  v
agent-service (FastAPI)
  |
  +--> PlannerService
  |      +--> OpenRouter structured JSON call
  |
  +--> ToolExecutor
  |      +--> OjTools -> oj_server /api/ai/*
  |      +--> retrieve_knowledge -> Chroma / fastembed
  |
  +--> PromptService
  |      +--> LangChain ChatPromptTemplate
  |
  +--> OpenRouterClient
         +--> custom httpx client
         +--> stream_text / complete_text / invoke_structured
```

跨服务边界：

| 模块 | 职责 |
| --- | --- |
| 前端 `/agent` | 展示会话、提交用户问题、轮询 job 事件、渲染 Markdown |
| `oj_server` | 用户认证、会话表/消息表持久化、上下文恢复、内部数据 API |
| `agent-service` | Planner、工具执行、RAG、prompt、LLM 调用、安全后处理 |
| OpenRouter | 大模型推理 |
| Chroma | 本地知识库向量检索 |
| MySQL | 由 `oj_server` 管理，保存题目、提交、对话和消息 |

安全边界：

- 浏览器不直接调用 `agent-service`。
- `agent-service` 不直接连接 MySQL。
- `agent-service` 调 `oj_server` 必须带 `X-Internal-Token`。
- 查询提交时必须带可信 `user_id`，由 `oj_server` 校验资源归属。
- LLM 只能选择预定义工具，不能任意访问 URL 或数据库。

## 3. 技术选型

| 能力 | 当前选型 | 说明 |
| --- | --- | --- |
| Web 框架 | FastAPI + Uvicorn | 提供内部 API 和 SSE 流式响应 |
| Schema | Pydantic v2 | 请求、响应、工具记录和 planner 计划校验 |
| Prompt | LangChain `ChatPromptTemplate` | 用于 planner prompt 和 answer prompt |
| Tool 抽象 | LangChain `StructuredTool` | OJ 工具和 RAG 工具统一用结构化参数 |
| LLM Client | 自写 OpenRouter `httpx` client | `langchain-openrouter` 在当前环境测试不稳定，暂不作为主链路 |
| RAG 向量库 | Chroma PersistentClient | 数据目录默认 `/app/data/chroma` |
| Embedding | fastembed | 默认模型 `BAAI/bge-small-zh-v1.5` |
| 知识库 | Markdown 文件 | 位于 `knowledge/algorithms` 和 `knowledge/problem_hints` |
| 测试 | pytest + pytest-asyncio + ruff | 单测覆盖 planner/tool/workflow 基础行为 |

## 4. 目录结构

```text
agent-service/
├─ app/
│  ├─ api/
│  │  ├─ chat.py                 # /api/v1/chat/stream
│  │  ├─ dependencies.py         # 内部 token / request id 校验
│  │  ├─ health.py               # /health /ready
│  │  └─ diagnoses.py            # 历史兼容接口，不作为当前主链路
│  ├─ clients/
│  │  ├─ oj_client.py            # 调 oj_server 内部 API
│  │  └─ openrouter_client.py    # 自写 OpenRouter client
│  ├─ core/
│  │  └─ config.py               # 环境变量配置
│  ├─ llm/
│  │  └─ messages.py             # LangChain message -> OpenRouter message
│  ├─ rag/
│  │  ├─ ingest.py               # Markdown 知识库入库
│  │  └─ retriever.py            # Chroma 检索
│  ├─ schemas/
│  │  ├─ chat.py                 # Agent Chat schema
│  │  ├─ diagnosis.py            # 历史兼容 schema
│  │  ├─ oj.py                   # OJ 数据 schema
│  │  ├─ rag.py                  # SourceReference
│  │  └─ streaming.py            # SSE helper
│  ├─ services/
│  │  ├─ planner_service.py      # LLM Planner 和参数归一化
│  │  ├─ prompt_service.py       # Planner/Answer prompt
│  │  └─ safety_service.py       # 回答安全后处理
│  ├─ tools/
│  │  ├─ executor.py             # 工具执行、参数校验、去重、错误记录
│  │  ├─ oj_tools.py             # OJ 数据工具
│  │  └─ rag_tools.py            # retrieve_knowledge
│  └─ workflows/
│     ├─ chat_workflow.py        # 当前 Agent Chat 主流程
│     └─ diagnosis_workflow.py   # 历史兼容流程
├─ knowledge/                    # Markdown 知识库
├─ scripts/
│  ├─ ingest_knowledge.py        # 构建/刷新 Chroma 索引
│  ├─ check_openrouter.py        # OpenRouter 连通性检查
│  └─ check_structured_diagnosis.py
├─ tests/
│  └─ test_chat_workflow_units.py
├─ pyproject.toml
└─ oj_agent_module_design.md
```

## 5. Agent Chat 工作流

主入口是：

```http
POST /api/v1/chat/stream
```

同一个接口同时支持新对话和继续对话，区别由请求中的 `conversation.conversation_id`、`conversation.history` 和 `initial_context` 决定。

### 5.1 阶段流转

```text
AgentChatRequest
  |
  v
status: planning
  |
  v
PlannerService.plan()
  |
  +--> OpenRouter structured JSON
  +--> PlannerPlan(tool_calls, intent, answer_strategy, rewritten_question)
  |
  v
ToolExecutor.execute()
  |
  +--> OjTools
  +--> RAG tool
  |
  v
PromptService.build_answer_messages()
  |
  v
OpenRouterClient.stream_text()
  |
  +--> delta events
  +--> idle/stream error -> complete_text fallback
  |
  v
SafetyService.validate_answer()
  |
  v
done: AgentChatResponse
```

### 5.2 Planner 行为

Planner 的任务不是回答用户，而是输出工具计划。结构如下：

```python
class PlannerPlan(BaseModel):
    tool_calls: list[PlannerToolCall] = []
    answer_strategy: str = ""
    intent: str = ""
    rewritten_question: str = ""
```

其中：

- `rewritten_question` 是基于多轮历史和初始上下文重述后的本轮问题。
- `rewritten_question` 用于消解“这个题”“这份提交”“上一轮”等指代。
- 如果要调用 RAG，`retrieve_knowledge.query` 应使用 `rewritten_question` 或基于它压缩出的明确检索语句。
- 后端不会再用正则强行改写 RAG query，只在 query 为空时用 `rewritten_question` 补齐必填参数。

Planner 失败时：

- 超过 `PLANNER_TIMEOUT_SECONDS` 会进入 fallback。
- OpenRouter 结构化调用异常会进入 fallback。
- fallback 只根据明确上下文和用户消息中可提取的题号/提交号调用基础工具。

### 5.3 工具执行

工具执行由 `ToolExecutor` 统一处理：

- 去除 `user_id`、`owner_user_id` 等身份字段，避免 LLM 注入身份。
- 根据工具 `args_schema` 检查必填参数。
- 对同名同参工具调用去重。
- 未知工具标记为 `skipped`。
- 参数错误、HTTP 错误、RAG 错误等记录为 `status="error"`，但不会让整个 workflow 立即崩溃。

当前稳定工具：

| 工具 | 参数 | 来源 | 说明 |
| --- | --- | --- | --- |
| `get_problem` | `problem_id` | `oj_server` | 查询公开题面、标签、时限和内存 |
| `get_submission` | `submission_id` | `oj_server` | 查询当前用户自己的提交、源码和判题摘要 |
| `get_conversation_history` | `conversation_id`, `limit` | `oj_server` | 查询当前用户指定会话最近消息 |
| `retrieve_knowledge` | `query`, `problem_id?` | Chroma | 检索本地 Markdown 知识库 |

目前代码中还有 `search_problem` 和 `list_problem_submissions` 的草稿工具，但对应 OJ 能力尚未完整接入，不应视为稳定对外设计。

## 6. API 设计

### 6.1 健康检查

```http
GET /health
```

响应：

```json
{
  "status": "ok",
  "service": "oj-programming-tutor",
  "environment": "production",
  "openrouter_configured": true,
  "chat_model_configured": true,
  "oj_server_configured": true
}
```

```http
GET /ready
```

响应：

```json
{
  "status": "ready",
  "checks": {
    "config": "ok",
    "vector_store": "ok",
    "embedding": "configured",
    "oj_client": "configured",
    "llm_client": "configured"
  }
}
```

`/ready` 会检查配置、Chroma、embedding cache、OJ client 和 LLM client 配置。它不主动调用 OpenRouter。

### 6.2 Agent Chat 流式接口

```http
POST /api/v1/chat/stream
X-Internal-Token: <internal-token>
X-Request-Id: req_...
Content-Type: application/json
Accept: text/event-stream
```

请求 schema：

```json
{
  "user": {
    "user_id": 1001
  },
  "conversation": {
    "conversation_id": "conv_...",
    "history": [
      {
        "round_no": 1,
        "user_content": "帮我看看 1001 这道题",
        "assistant_content": "这是一道图连通性题..."
      }
    ]
  },
  "initial_context": {
    "problem_id": 1001,
    "submission_id": null,
    "extra": {}
  },
  "hint_level": 2,
  "message": "这个题为什么不能用二分？"
}
```

字段说明：

| 字段 | 说明 |
| --- | --- |
| `user.user_id` | 可信用户 ID，由 `oj_server` 从 JWT 解析后写入 |
| `conversation.conversation_id` | 当前会话 ID；新对话为 `null` |
| `conversation.history` | 最近若干轮历史，由 `oj_server` 从数据库恢复 |
| `initial_context.problem_id` | 当前会话绑定题目，可空 |
| `initial_context.submission_id` | 当前会话绑定提交，可空 |
| `hint_level` | 提示等级，当前为 1-3 |
| `message` | 用户本轮原始问题 |

响应类型：

```http
Content-Type: text/event-stream
```

SSE 事件：

| 事件 | 数据 | 说明 |
| --- | --- | --- |
| `status` | `{stage, message}` | 阶段状态，如 planning、generating、fallback |
| `tool_call` | `{name, arguments, reason}` | 即将调用工具 |
| `tool_result` | `ToolCallRecord` | 工具调用结果摘要 |
| `sources` | `{message, sources}` | RAG 来源 |
| `delta` | `{content}` | 模型流式输出片段 |
| `done` | `AgentChatResponse` | 最终回答和元数据 |
| `error` | `{message}` | workflow 未能恢复的错误 |

`done` 示例：

```json
{
  "request_id": "req_...",
  "user_id": 1001,
  "conversation_id": "conv_...",
  "problem_id": 1001,
  "submission_id": null,
  "answer": "Markdown 源文本回答",
  "intent": "explain_algorithm",
  "knowledge_points": [],
  "sources": [
    {
      "document_id": "problem_1001_reachability_from_capital",
      "source": "knowledge/problem_hints/problem-1001-reachability-from-capital.md",
      "title": "1001 Reachability from the Capital 题解",
      "knowledge_point": "problem_editorial",
      "chunk_index": 0,
      "score": 0.48
    }
  ],
  "tool_calls": [
    {
      "name": "get_problem",
      "arguments": {
        "problem_id": 1001
      },
      "status": "ok",
      "summary": "题目 1001: Reachability from the Capital"
    }
  ],
  "metadata": {
    "answer_strategy": "结合题面和题解说明为什么不能用二分。",
    "rewritten_question": "题目 1001 为什么不能用二分？",
    "used_non_stream_fallback": false,
    "safety_flags": []
  },
  "model": "deepseek/deepseek-v4-flash",
  "provider": "OpenRouter",
  "generated_at": 1783920600
}
```

## 7. oj_server 对接

前端不直接调用 agent-service。`oj_server` 暴露给前端的当前主接口包括：

```http
POST /api/assistant/chat/stream
POST /api/assistant/conversations/{conversation_id}/chat/stream
GET  /api/assistant/chat/jobs/{job_id}/events?after=0
GET  /api/assistant/conversations?limit=50
GET  /api/assistant/conversations/{conversation_id}
```

`oj_server` 的职责：

- 校验用户 JWT。
- 新对话时创建后台 chat job。
- 继续对话时校验会话归属。
- 恢复会话当前 `problem_id` / `submission_id` / 最近历史。
- 调用 `agent-service /api/v1/chat/stream`。
- 将 agent-service SSE 事件写入内存 job 事件队列。
- 收到 `done` 后写入 `ai_conversation` / `ai_message`。
- append message 后同步更新 conversation 主表默认题目/提交上下文；空值不会覆盖旧上下文。

agent-service 使用的 `oj_server` 内部数据 API：

```http
GET /api/ai/problems/{problem_id}
GET /api/ai/submissions/{submission_id}
GET /api/ai/conversations/{conversation_id}
```

这些接口需要：

```http
X-Internal-Token: <internal-token>
X-Request-Id: req_...
X-User-Id: <user-id>       # 查询用户私有提交或会话时需要
```

## 8. RAG 设计

知识库目录：

```text
knowledge/
├─ algorithms/
└─ problem_hints/
```

入库脚本：

```bash
cd oj_platform/agent-service
.venv/bin/python scripts/ingest_knowledge.py
```

默认配置：

| 配置 | 默认值 |
| --- | --- |
| `EMBEDDING_MODEL` | `BAAI/bge-small-zh-v1.5` |
| `EMBEDDING_CACHE_DIR` | `/app/data/fastembed` |
| `CHROMA_PERSIST_DIR` | `/app/data/chroma` |
| `CHROMA_COLLECTION` | `oj_agent_knowledge` |

检索工具：

```python
retrieve_knowledge(query: str, problem_id: int | None = None)
```

设计约束：

- 如果有 `problem_id`，retriever 会优先使用题目相关过滤或题目专属文档。
- RAG 资料是参考数据，不是用户指令。
- answer prompt 会提醒模型：资料与当前问题不相关时不要强行引用。
- 最终 `sources` 只包含去重后的来源引用。

## 9. OpenRouter Client 设计

当前不使用 `langchain-openrouter` 作为主链路。原因是当前环境测试中：

- 本地和容器内 `ChatOpenRouter.ainvoke()` 多次超时。
- `ChatOpenRouter.astream()` 多次无 chunk 超时。
- 同一容器内自写 `httpx` 非流式请求可在数秒内成功。
- `langchain-openrouter` 还会引入 `openrouter` SDK 和额外依赖版本约束，生产风险更高。

因此当前保留自写 `OpenRouterClient`：

```python
stream_text(messages)       # 流式回答
complete_text(messages)     # 非流式补全
invoke_structured(messages) # JSON schema 结构化 planner
```

稳定性策略：

- Planner 有 `PLANNER_TIMEOUT_SECONDS` 超时保护，失败后进入 fallback plan。
- Answer 首选流式输出。
- 流式 idle 超过 `ANSWER_STREAM_IDLE_TIMEOUT_SECONDS` 后，切换非流式补全。
- 总生成超过 `ANSWER_STREAM_TOTAL_TIMEOUT_SECONDS` 后，切换非流式补全。
- 非流式补全超时由 `ANSWER_FALLBACK_TIMEOUT_SECONDS` 控制。
- 请求 payload 支持 `provider.sort`，默认 `throughput`。
- 流式 SSE 中出现 OpenRouter error payload 时立即转为异常处理。

## 10. 配置

核心配置定义在 `app/core/config.py`，Docker Compose 通过环境变量注入。

| 环境变量 | 说明 | 默认值 |
| --- | --- | --- |
| `INTERNAL_API_TOKEN` | agent-service 内部接口鉴权 token | 空 |
| `OJ_SERVER_BASE_URL` | oj_server 内部地址 | `http://127.0.0.1:8080` |
| `OJ_INTERNAL_API_TOKEN` | 调 oj_server 内部接口的 token | `INTERNAL_API_TOKEN` |
| `OJ_CONNECT_TIMEOUT_SECONDS` | OJ client 连接超时 | `5` |
| `OJ_READ_TIMEOUT_SECONDS` | OJ client 读超时 | `15` |
| `OPENROUTER_API_KEY` | OpenRouter API key | 空 |
| `CHAT_MODEL` | 默认聊天模型 | `deepseek/deepseek-v4-flash` |
| `OPENROUTER_BASE_URL` | OpenRouter base URL | `https://openrouter.ai/api/v1` |
| `OPENROUTER_READ_TIMEOUT_SECONDS` | OpenRouter httpx 超时 | `60` |
| `OPENROUTER_PROVIDER_SORT` | OpenRouter provider 排序 | `throughput` |
| `PLANNER_TIMEOUT_SECONDS` | planner 阶段超时 | `20` |
| `ANSWER_STREAM_IDLE_TIMEOUT_SECONDS` | 流式无 token idle 超时 | `35` |
| `ANSWER_STREAM_TOTAL_TIMEOUT_SECONDS` | 回答总时长超时 | `150` |
| `ANSWER_FALLBACK_TIMEOUT_SECONDS` | 非流式补全超时 | `45` |

Docker Compose 中 agent-service 的生产默认值：

```yaml
OPENROUTER_READ_TIMEOUT_SECONDS: ${OPENROUTER_READ_TIMEOUT_SECONDS:-180}
OPENROUTER_PROVIDER_SORT: ${OPENROUTER_PROVIDER_SORT:-throughput}
ANSWER_FALLBACK_TIMEOUT_SECONDS: ${ANSWER_FALLBACK_TIMEOUT_SECONDS:-45}
OJ_SERVER_BASE_URL: http://oj_server:18080
```

## 11. 测试与验证

单测：

```bash
cd oj_platform/agent-service
.venv/bin/python -m pytest
```

Lint：

```bash
cd oj_platform/agent-service
.venv/bin/python -m ruff check app tests
```

知识库入库：

```bash
cd oj_platform/agent-service
.venv/bin/python scripts/ingest_knowledge.py
```

容器重建：

```bash
docker compose -f oj_platform/docker-compose.yml up -d --build agent-service
```

健康检查：

```bash
curl http://127.0.0.1:8001/health
curl http://127.0.0.1:8001/ready
```

## 12. 当前限制

- `search_problem` 和 `list_problem_submissions` 尚未作为稳定工具接入。
- Tool error 目前主要是字符串摘要，尚未结构化为 `error_code`。
- planner 和 answer 目前共用 `CHAT_MODEL`，还未拆为独立模型配置。
- Agent run trace 尚未持久化，排障主要依赖 Docker logs 和 `tool_result`。
- RAG 当前是 embedding 检索，没有 rerank。
- 历史兼容的 diagnosis API 代码仍存在，但不属于当前 Agent Chat 设计主链路。

## 13. 后续演进方向

优先级建议：

1. 隐藏或实现 `search_problem`、`list_problem_submissions`。
2. Tool result 增加结构化 `error_code`。
3. 增加 Agent run trace 表，保存 planner plan、tool calls、tool results、fallback 和耗时。
4. 拆分 `PLANNER_MODEL`、`ANSWER_MODEL`、`FALLBACK_MODEL`。
5. 增加会话上下文栏，支持用户查看/清除当前题目和提交绑定。
6. 对 RAG 增加多 query 检索和 rerank。
7. 建立固定评测集，回放多轮对话、缺参数、RAG、提交诊断和工具失败场景。
