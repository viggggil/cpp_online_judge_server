# oj_platform

`oj_platform` 是一个在线判题与编程学习平台。当前工程包含 C++ 主站服务、异步判题调度、多个判题 worker、题目/提交/作业管理页面、监控服务，以及基于 Python + LangChain 工具抽象的 AI 编程助手。

项目主目录是 `oj_platform/`，根目录下的 `Crow/` 是 C++ Web 框架源码依赖。

## 当前能力

- 用户注册、登录、JWT 鉴权和管理员注册码。
- 题库列表、题目详情、提交代码、提交记录、提交详情。
- 管理员创建/编辑题目、更新题面、追加测试数据、导入题目。
- 作业与排行榜页面。
- RabbitMQ 判题队列、dispatcher 调度、3 个 judge worker 并行执行。
- MinIO 对象存储测试数据。
- MySQL 持久化用户、题目、题面、测试点、提交、作业、AI 会话等数据。
- Redis 缓存和历史兼容队列配置。
- Go 监控服务汇总 MySQL、Redis、RabbitMQ、MinIO、worker 和 agent 状态。
- AI 助手页面 `/agent`，支持新对话、多轮对话、Markdown 渲染、题目/提交上下文、RAG、本地知识库检索和流式回答。
- Agent 流式输出异常时自动切换非流式补全，避免前端长时间停在半截输出。

## 架构

```text
Browser
  |
  | http://localhost:8080
  v
oj_server (C++ Crow)
  |  \
  |   \-- static web pages, REST APIs, auth, submissions, assignments, AI bridge
  |
  +--> MySQL
  +--> Redis
  +--> RabbitMQ --------------+
  +--> MinIO                  |
  +--> agent-service          |
  +--> go_monitor             |
                               v
                         judge_dispatcher
                               |
                               v
                judge_worker_1 / judge_worker_2 / judge_worker_3

agent-service (Python FastAPI)
  |
  +--> OpenRouter chat/completions
  +--> Chroma / fastembed local knowledge base
  +--> oj_server internal AI APIs
```

## 服务清单

Docker Compose 默认启动以下服务：

| 服务 | 容器名 | 说明 | 端口 |
| --- | --- | --- | --- |
| MySQL | `oj_platform_mysql` | 主数据库 | Compose 内网 |
| Redis | `oj_platform_redis` | 缓存/兼容队列 | Compose 内网 |
| RabbitMQ | `oj_platform_rabbitmq` | 判题消息队列 | `5672`, `15672` |
| MinIO | `oj_minio` | 测试数据对象存储 | `9000`, `9001` |
| oj_server | `oj_platform_oj_server` | C++ 主站/API/静态页面 | `8080 -> 18080` |
| agent-service | `oj_platform_agent_service` | Python AI 助手服务 | `8001 -> 8001` |
| judge_dispatcher | `oj_platform_judge_dispatcher` | 消费判题队列并调度 worker | Compose 内网 |
| judge_worker_1/2/3 | `oj_platform_judge_worker_*` | 编译、运行、判题 | Compose 内网 |
| go_monitor | `oj_platform_go_monitor` | 平台运行状态汇总 | `18090 -> 18090` |

## 快速启动

进入平台目录：

```bash
cd /home/max85/webserver/oj_platform
```

复制或维护 `.env`。当前 compose 依赖这些关键变量：

```dotenv
MYSQL_ROOT_PASSWORD=...
MYSQL_PASSWORD=...
RABBITMQ_PASSWORD=oj_pass
MINIO_ROOT_USER=...
MINIO_ROOT_PASSWORD=...
OJ_ADMIN_REGISTER_CODE=...
INTERNAL_API_TOKEN=...
OPENROUTER_API_KEY=...
CHAT_MODEL=deepseek/deepseek-v4-flash
```

启动全部服务：

```bash
./compose-up.sh
```

也可以直接使用 Docker Compose：

```bash
docker compose up -d --build
```

访问：

- 主站：`http://127.0.0.1:8080`
- AI 助手：`http://127.0.0.1:8080/agent`
- 监控页：`http://127.0.0.1:8080/monitor`
- RabbitMQ 管理界面：`http://127.0.0.1:15672`
- MinIO 控制台：`http://127.0.0.1:9001`

停止服务：

```bash
docker compose down
```

清空数据卷并重新初始化数据库：

```bash
docker compose down -v
docker compose up -d --build
```

## 数据初始化

MySQL 首次创建数据卷时会自动执行：

```text
oj_platform/sql/schema.sql
```

该文件被挂载到 MySQL 容器：

```text
/docker-entrypoint-initdb.d/01-schema.sql
```

注意：这个初始化脚本只在 MySQL 数据目录第一次创建时执行。已有数据卷不会重复执行 schema 初始化。

## 判题链路

当前判题主链路是 RabbitMQ：

1. 用户在 `oj_server` 提交代码。
2. `oj_server` 写入提交记录，并向 RabbitMQ 发布判题消息。
3. `judge_dispatcher` 消费 RabbitMQ 队列。
4. dispatcher 按 worker 配置调度到 `judge_worker_1/2/3`。
5. worker 从 MySQL/MinIO 获取题目和测试数据，编译运行用户代码。
6. dispatcher 汇总结果并回写 MySQL。
7. 前端轮询提交详情展示状态和测试点摘要。

Redis 仍用于缓存和部分兼容配置，但当前判题调度的主要消息通道是 RabbitMQ。

## AI 助手

AI 助手由 `agent-service` 提供，`oj_server` 负责鉴权、会话持久化和前端轮询桥接。

主要接口：

- `POST /api/assistant/chat/stream`：开启新对话。
- `POST /api/assistant/conversations/{conversation_id}/chat/stream`：继续多轮对话。
- `GET /api/assistant/chat/jobs/{job_id}/events`：轮询流式事件。
- `GET /api/assistant/conversations`：列出历史会话。
- `GET /api/assistant/conversations/{conversation_id}`：读取会话详情。

Agent 当前行为：

- Planner LLM 根据用户问题、最近历史和当前上下文决定工具调用。
- Planner 会输出 `rewritten_question`，用于把“这个题”“这份提交”等多轮指代重述为明确问题。
- 工具层使用 LangChain `StructuredTool` 封装。
- OpenRouter client 仍使用自写 `httpx` 逻辑，因为当前测试中 `langchain-openrouter` 在本环境下不比自写 client 稳定。
- 回答输出 Markdown 源文本，前端渲染 Markdown。
- 流式输出中断或 idle timeout 时，会自动切换非流式补全。

当前已实现工具：

- `get_problem`：查询公开题面、标签、时间/内存限制。
- `get_submission`：查询当前用户自己的提交、源码和判题摘要。
- `get_conversation_history`：查询指定会话最近消息。
- `retrieve_knowledge`：检索本地 Markdown 知识库。

注意：`search_problem` 和 `list_problem_submissions` 目前在 Python 代码中仍有接口草稿，但 oj_server 对应能力尚未完整接入，不应依赖它们作为稳定工具。

Agent 相关配置：

```dotenv
OPENROUTER_API_KEY=...
CHAT_MODEL=deepseek/deepseek-v4-flash
OPENROUTER_READ_TIMEOUT_SECONDS=180
OPENROUTER_PROVIDER_SORT=throughput
ANSWER_FALLBACK_TIMEOUT_SECONDS=45
OJ_AGENT_READ_TIMEOUT_MS=180000
INTERNAL_API_TOKEN=...
```

## 常用运维命令

查看服务状态：

```bash
docker compose ps
```

查看主站日志：

```bash
docker logs -f oj_platform_oj_server
```

查看 Agent 日志：

```bash
docker logs -f oj_platform_agent_service
```

查看 dispatcher 日志：

```bash
docker logs -f oj_platform_judge_dispatcher
```

查看 worker 日志：

```bash
docker logs -f oj_platform_judge_worker_1
```

只重建 Agent：

```bash
docker compose up -d --build agent-service
```

只重建主站：

```bash
docker compose up -d --build oj_server
```

健康检查：

```bash
curl http://127.0.0.1:8080/api/health
curl http://127.0.0.1:8001/health
curl http://127.0.0.1:8001/ready
```

## 本地构建与测试

C++ 构建：

```bash
cmake -S oj_platform -B oj_platform/build -DCMAKE_BUILD_TYPE=Release
cmake --build oj_platform/build --target oj_server -j2
```

运行 CTest：

```bash
ctest --test-dir oj_platform/build --output-on-failure
```

Agent 单测：

```bash
cd oj_platform/agent-service
.venv/bin/python -m pytest
.venv/bin/python -m ruff check app tests
```

说明：本地 C++ 测试依赖数据库、对象存储或 fixture 时，可能需要先启动对应容器或准备测试数据。

## 目录结构

```text
.
├─ Crow/                         # Crow C++ Web 框架源码依赖
├─ oj_platform/
│  ├─ agent-service/             # Python FastAPI AI 助手、RAG、OpenRouter client
│  ├─ cmake/                     # CMake 模块
│  ├─ common/                    # C++ 公共配置、协议、对象存储、RabbitMQ 等工具
│  ├─ problems/                  # 题目源文件和导入素材
│  ├─ runtime/                   # 运行时目录
│  ├─ services/
│  │  ├─ oj_server/              # C++ 主站/API/静态页面/AI 桥接
│  │  ├─ judge_dispatcher/       # 判题队列消费者和 worker 调度
│  │  ├─ judge_worker/           # 编译运行和测试点执行
│  │  └─ go_monitor/             # Go 监控服务
│  ├─ sql/                       # MySQL schema
│  ├─ tests/                     # C++ 测试
│  ├─ third_party/               # 第三方依赖入口
│  ├─ tools/                     # 辅助工具
│  ├─ web/                       # 前端静态页面、JS、CSS
│  ├─ Dockerfile                 # 多阶段镜像构建
│  └─ docker-compose.yml         # 本地完整编排
└─ runtime/                      # 根级运行时/历史目录
```

## 主要页面

- `/`：首页
- `/problems`：题库
- `/problems/{id}`：题目详情
- `/submit/{id}`：提交页面
- `/submissions`：提交列表
- `/submissions/{submission_id}`：提交详情
- `/assignments`：作业列表
- `/assignments/{id}`：作业详情
- `/assignments/{id}/leaderboard`：作业排行榜
- `/agent`：AI 助手
- `/monitor`：平台监控

## 主要 API 分组

- `/api/auth/*`：注册、登录、当前用户。
- `/api/problems*`：题目列表、题目详情、用户题目状态。
- `/api/submissions*`：提交、提交列表、提交详情。
- `/api/assignments*`：作业和排行榜。
- `/api/admin/*`：管理员题目、作业、监控接口。
- `/api/assistant/*`：AI 助手对话、历史会话和流式 job 事件。
- `/api/ai/*`：agent-service 调用的内部接口，需要 `X-Internal-Token`。

## 开发注意事项

- Docker Compose 没有挂载 `agent-service/app` 源码；修改 Python 代码后需要重建 `agent-service` 镜像。
- 修改 `web/` 静态文件后，需要重建 `oj_server` 镜像，浏览器才会拿到最新文件。
- 修改 C++ 代码后，需要重新构建对应 target 或重建相关容器。
- 不要把 `INTERNAL_API_TOKEN`、`OPENROUTER_API_KEY`、MySQL/MinIO 密码提交到仓库。
- MySQL schema 的自动初始化只发生在数据卷第一次创建时；已有环境需要通过迁移或手动 SQL 更新。
- `Crow/` 是独立目录，当前工作区里可能显示为未跟踪或独立状态，提交前请确认是否需要包含。

## 后续建议

- 隐藏或实现尚未稳定的 Agent 工具：`search_problem`、`list_problem_submissions`。
- 给 Agent tool result 增加结构化 `error_code`，便于前端和日志定位。
- 增加 Agent run trace 持久化，记录 plan、tool calls、tool results、fallback 和耗时。
- 将 planner/answer/fallback 模型配置拆开，增强 OpenRouter 稳定性调优。
- 增加会话上下文栏，允许用户查看和清除当前题目/提交绑定。
- 建立固定 Agent 评测集，回放多轮问答、RAG、提交诊断和缺参数场景。
