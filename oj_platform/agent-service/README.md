# Agent Service

`agent-service` 是 OJ 平台的 AI 编程学习助手后端服务。它是一个内部 FastAPI 服务，不直接暴露给浏览器；前端请求先进入 C++ `oj_server`，由 `oj_server` 完成登录态校验、会话归属校验、上下文恢复和数据库持久化，再通过内部接口调用本服务。

当前主链路已经接入 LangGraph。LangGraph 在这里作为运行时工作流引擎，用 `StateGraph` 编排一次 Agent Chat 请求中的各个节点，并通过自定义流事件把执行过程转成 SSE 返回。

## 已完成能力

- 内部流式聊天接口：`POST /api/v1/chat/stream`
- 健康检查接口：`GET /health`、`GET /ready`
- LangGraph `StateGraph` 工作流编排
- Planner 结构化规划工具调用
- Planner Markdown 计划预览事件：`plan_delta`、`plan_done`
- OJ 受控工具：题目查询、提交查询、会话历史查询
- RAG 知识库检索工具：`retrieve_knowledge`
- OpenRouter 自写 `httpx` 客户端
- 模型流式输出、流式异常 fallback、非流式补全 fallback
- SSE 事件输出：`status`、`plan_delta`、`plan_done`、`tool_call`、`tool_result`、`sources`、`delta`、`done`、`error`
- 回答安全后处理，按提示等级限制完整答案输出
- pytest 单测和 ruff 检查

## 工作流

```text
AgentChatRequest
  |
  v
ChatWorkflow.run_stage_stream()
  |
  v
LangGraph StateGraph
  |
  v
prepare_node
  |
  v
plan_node
  |
  v
emit_plan_preview_node
  |
  +--> 有工具计划: execute_tools_node
  |       |
  |       +--> ToolExecutor
  |       +--> OjTools / retrieve_knowledge
  |
  +--> 无工具计划: 跳过工具
  |
  v
collect_sources_node
  |
  v
build_answer_messages_node
  |
  v
generate_answer_node
  |
  v
finalize_node
  |
  v
done: AgentChatResponse
```

LangGraph 的节点状态定义在 `app/graphs/chat_state.py`。每个节点读取当前 state，只返回自己新增或更新的字段；LangGraph 会把返回值合并进 state，再传给后续节点。

## 目录结构

```text
agent-service/
├─ app/
│  ├─ api/
│  │  ├─ chat.py                 # /api/v1/chat/stream
│  │  ├─ dependencies.py         # 内部 token / request id 校验
│  │  └─ health.py               # /health /ready
│  ├─ clients/
│  │  ├─ oj_client.py            # 调 oj_server 内部 API
│  │  └─ openrouter_client.py    # OpenRouter HTTP client
│  ├─ core/
│  │  └─ config.py               # 环境变量配置
│  ├─ graphs/
│  │  ├─ chat_graph.py           # LangGraph StateGraph 定义
│  │  ├─ chat_state.py           # 图状态定义
│  │  ├─ nodes.py                # 工作流节点
│  │  └─ utils.py                # fallback、source 去重、上下文解析、计划渲染
│  ├─ llm/
│  │  └─ messages.py             # LangChain message 转 OpenRouter message
│  ├─ rag/
│  │  ├─ ingest.py               # Markdown 知识库入库
│  │  └─ retriever.py            # Chroma 检索
│  ├─ schemas/
│  │  ├─ chat.py                 # Chat 请求/响应 schema
│  │  ├─ oj.py                   # OJ 数据 schema
│  │  ├─ rag.py                  # RAG 来源 schema
│  │  └─ streaming.py            # SSE helper
│  ├─ services/
│  │  ├─ planner_service.py      # Planner 调用和参数归一化
│  │  ├─ prompt_service.py       # Planner/Answer prompt
│  │  └─ safety_service.py       # 回答安全后处理
│  ├─ tools/
│  │  ├─ executor.py             # 工具执行器
│  │  ├─ oj_tools.py             # OJ 工具
│  │  └─ rag_tools.py            # RAG 工具
│  └─ workflows/
│     └─ chat_workflow.py        # LangGraph custom stream 到 SSE 的适配层
├─ knowledge/                    # Markdown 知识库
├─ scripts/
│  ├─ ingest_knowledge.py        # 构建/刷新 Chroma 索引
│  └─ check_openrouter.py        # OpenRouter 连通性检查
├─ tests/
├─ pyproject.toml
└─ oj_agent_module_design.md
```

## 接口

### `POST /api/v1/chat/stream`

内部聊天流式接口。请求必须带内部鉴权头：

```http
X-Internal-Token: <internal-token>
X-Request-Id: req_...
Content-Type: application/json
Accept: text/event-stream
```

请求体核心字段：

```json
{
  "user": {
    "user_id": 1001
  },
  "conversation": {
    "conversation_id": "conv_...",
    "history": []
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

返回类型是 `text/event-stream`。主要事件：

| 事件 | 说明 |
| --- | --- |
| `status` | 当前阶段状态 |
| `plan_delta` | Planner 计划预览 Markdown 片段 |
| `plan_done` | Planner 计划预览结束 |
| `tool_call` | 即将调用工具 |
| `tool_result` | 工具调用结果摘要 |
| `sources` | RAG 来源 |
| `delta` | 模型回答增量 |
| `done` | 最终 `AgentChatResponse` |
| `error` | 未能恢复的错误 |

### `GET /health`

返回服务基础状态和关键配置是否存在。

### `GET /ready`

检查配置、Chroma、embedding cache、OJ client 和 LLM client 配置。该接口不主动请求 OpenRouter。

## 配置

配置定义在 `app/core/config.py`，可通过 `.env` 或 Docker Compose 环境变量注入。

| 环境变量 | 说明 | 默认值 |
| --- | --- | --- |
| `INTERNAL_API_TOKEN` | agent-service 内部接口鉴权 token | 空 |
| `OJ_SERVER_BASE_URL` | oj_server 内部地址 | `http://127.0.0.1:8080` |
| `OJ_INTERNAL_API_TOKEN` | 调 oj_server 内部接口的 token | `INTERNAL_API_TOKEN` |
| `OJ_CONNECT_TIMEOUT_SECONDS` | OJ client 连接超时 | `5` |
| `OJ_READ_TIMEOUT_SECONDS` | OJ client 读超时 | `15` |
| `OPENROUTER_API_KEY` | OpenRouter API key | 空 |
| `CHAT_MODEL` | 回答模型 | `deepseek/deepseek-v4-flash` |
| `PLANNER_MODEL` | Planner 模型 | `deepseek/deepseek-v4-flash` |
| `OPENROUTER_BASE_URL` | OpenRouter base URL | `https://openrouter.ai/api/v1` |
| `OPENROUTER_READ_TIMEOUT_SECONDS` | OpenRouter 读超时 | `60` |
| `OPENROUTER_PROVIDER_SORT` | 回答模型 provider 排序 | `throughput` |
| `PLANNER_PROVIDER_SORT` | Planner provider 排序 | `latency` |
| `PLANNER_TIMEOUT_SECONDS` | Planner 阶段超时 | `28` |
| `ANSWER_STREAM_IDLE_TIMEOUT_SECONDS` | 流式无增量 idle 超时 | `35` |
| `ANSWER_STREAM_TOTAL_TIMEOUT_SECONDS` | 回答总时长超时 | `150` |
| `ANSWER_FALLBACK_TIMEOUT_SECONDS` | 非流式补全超时 | `45` |

## 本地命令

安装依赖：

```bash
cd oj_platform/agent-service
uv sync
```

运行服务：

```bash
cd oj_platform/agent-service
.venv/bin/uvicorn main:app --host 0.0.0.0 --port 8001 --reload
```

运行测试：

```bash
cd oj_platform/agent-service
.venv/bin/python -m pytest
```

运行 lint：

```bash
cd oj_platform/agent-service
.venv/bin/python -m ruff check app tests
```

构建或刷新知识库索引：

```bash
cd oj_platform/agent-service
.venv/bin/python scripts/ingest_knowledge.py
```

检查 OpenRouter 连通性：

```bash
cd oj_platform/agent-service
.venv/bin/python scripts/check_openrouter.py
```

Docker Compose 重建服务：

```bash
docker compose -f oj_platform/docker-compose.yml up -d --build agent-service
```

健康检查：

```bash
curl http://127.0.0.1:8001/health
curl http://127.0.0.1:8001/ready
```

## 详细设计

完整模块设计见 `oj_agent_module_design.md`。
