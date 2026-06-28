# oj_platform

一个基于 **Crow** 实现的在线判题平台交付工程，覆盖“首页 + 题库 + 提交 + 判题 + 作业 + 排行榜 + 管理后台页面 + 多 worker 调度”的完整运行链路。

## Docker Compose（完整容器化部署）

项目已提供完整容器编排，可直接启动：

- `mysql`
- `redis`
- `oj_server`
- `judge_dispatcher`
- `judge_worker_1`
- `judge_worker_2`
- `judge_worker_3`

整体拓扑如下：

```text
Browser
  |
  | http://localhost:8080
  v
oj_server container
  |
  | Redis queue
  v
redis container
  ^
  |
judge_dispatcher container
  |
  | HTTP /api/judge
  v
judge_worker_1 container
judge_worker_2 container
judge_worker_3 container

oj_server / dispatcher
  |
  | MySQL
  v
mysql container
```

### 启动全部服务

```bash
cd /home/max85/webserver/oj_platform
./compose-up.sh
```

如果你的环境里安装的是旧版 `docker-compose`（例如 1.29.x）并且 Docker Engine 较新，直接执行 `docker-compose up` 可能会报：

```text
KeyError: 'ContainerConfig'
```

这是旧版 `docker-compose` 在“重建已存在容器”时的兼容性问题。仓库里新增的 `./compose-up.sh` 会先清理本项目的旧容器，再执行 `docker-compose up -d --build`，可绕过这个问题。

### 停止服务

```bash
cd /home/max85/webserver/oj_platform
docker compose down
```

### 服务说明

- MySQL
  - 容器名：`oj_platform_mysql`
  - 端口映射：`3306:3306`
  - 数据库名：`oj_platform`
  - 普通用户：`oj`
  - 密码：`oj123456`
  - root 密码：`root123456`
- Redis
  - 容器名：`oj_platform_redis`
  - 端口映射：`6379:6379`
- oj_server
  - 容器名：`oj_platform_oj_server`
  - 宿主机访问地址：`http://127.0.0.1:8080`
- judge_dispatcher
  - 容器名：`oj_platform_judge_dispatcher`
  - 负责消费 Redis 提交队列并调度 worker
- judge_worker_1 / judge_worker_2 / judge_worker_3
  - 容器名：`oj_platform_judge_worker_1` / `oj_platform_judge_worker_2` / `oj_platform_judge_worker_3`
  - 仅在 compose 内网中被 dispatcher 调用

### 初始化说明

- `sql/schema.sql` 已挂载到 MySQL 容器的 `/docker-entrypoint-initdb.d/01-schema.sql`
- **仅在 MySQL 数据目录第一次初始化时自动执行**
- 如果你已经存在旧的数据卷，又想重新初始化表结构，可以执行：

```bash
cd /home/max85/webserver/oj_platform
docker compose down -v
docker compose up -d --build
```

### Dockerfile 说明

根目录新增了多阶段 `Dockerfile`，包含 3 个 target：

- `oj_server`
- `judge_dispatcher`
- `judge_worker`

`docker-compose.yml` 会基于同一个 Dockerfile 分别构建不同服务镜像。

### 与当前代码配置的关系

`common/platform_config.h` 已支持从环境变量读取容器内连接配置，未设置时仍保留本地开发默认值：

- MySQL：`127.0.0.1:3306`
- Redis：`127.0.0.1:6379`

容器内实际使用的是 compose 服务名：

- `OJ_MYSQL_HOST=mysql`
- `OJ_REDIS_HOST=redis`
- `OJ_JUDGE_WORKER_1=judge_worker_1:18081/api/judge`
- `OJ_JUDGE_WORKER_2=judge_worker_2:18081/api/judge`
- `OJ_JUDGE_WORKER_3=judge_worker_3:18081/api/judge`

因此：

- 在宿主机直接运行服务时，仍可继续使用默认配置
- 在容器内运行时，会自动切换到容器网络地址

平台将以下数据源统一存储在 MySQL 中：

- 用户信息：`users`
- 题目元数据：`problems`
- 题面：`problem_statements`
- 题目标签：`problem_tags`
- 测试点源数据：`problem_testcases`

说明：磁盘中的 `problems/<id>/tests/*.in`、`*.out` 作为**题目录入源文件**保留；`oj_server` 运行时仅从 MySQL 读取题目、题面和测试点。新增题目后必须执行迁移，把数据写入数据库。

### 建表

先创建数据库，再执行：

```bash
mysql -uroot -p oj_platform < /home/max85/webserver/oj_platform/sql/schema.sql
```

### 构建依赖

除 Crow / Redis 相关依赖外，还需要安装 MySQL Connector/C++ 开发包，要求提供：

- `cppconn/connection.h`
- `libmysqlcppconn` 或 `libmysqlcppconn8`

如果本机尚未安装该开发包，CMake 会直接报错：

```text
MySQL Connector/C++ not found. Please install mysqlcppconn development package.
```

### 迁移题目数据

```bash
/home/max85/webserver/oj_platform/build-mysql-check/problem_migrator
```

该工具会扫描 `problems/` 目录，并把 `meta.json`、`statement_zh.md`、`tests/*.in`、`tests/*.out` 导入 MySQL。

### 新增 / 更新题目的标准流程

1. 在 `problems/<id>/` 下维护题目源文件：
   - `meta.json`
   - `statement_zh.md`
   - `tests/*.in`
   - `tests/*.out`
2. 执行迁移工具，把题目同步进 MySQL：

```bash
cd /home/max85/webserver/oj_platform && ./build-mysql-check/problem_migrator
```

3. 重启 `oj_server` / `judge_dispatcher`，确保新数据生效。

> 约定：以后新增题目时，**以数据库数据为准**；不要再依赖 `oj_server` 直接从本地目录兜底读题。

### 运行说明

`oj_server` 的题目接口与认证接口现在依赖 MySQL：

- `/api/problems`
- `/api/problems/:id`
- `/api/auth/register`
- `/api/auth/login`

如需修改连接参数，请调整 `common/platform_config.h` 中的 `MySqlConfig` 默认值，或通过环境变量覆盖。

平台已具备完整可运行能力：

- 使用 `Crow` 作为第三方 Web 框架
- 拥有 `oj_server`、`judge_dispatcher` 与 `judge_worker` 三个服务目标
- 支持题目列表、题目详情、代码提交、提交结果查看
- 支持基于 MySQL 的题目、题面与测试点读取
- 支持用户注册 / 登录
- 密码采用 `bcrypt` 哈希保存
- 登录态采用 JWT
- 前端页面支持登录弹窗、提交与结果展示
- 提交结果页支持测试点折叠详情查看（默认只展示状态、耗时、内存）
- 支持 Redis 提交队列、异步评测状态流转与轮询展示
- 支持 dispatcher 通过 HTTP 调用多个 judge_worker，并按轮询顺序分发任务
- 支持主线程调度 + 后台 future 等待 worker 返回 + 主线程统一回写数据库
- MySQL 访问已封装为连接池

> 说明：当前版本已把“提交入口”和“判题消费”拆成异步队列模型：`oj_server` 负责入队，`judge_dispatcher` 负责消费 Redis 队列，并通过 HTTP 调用 compose 中配置的 3 个 `judge_worker`。dispatcher 维护 worker 列表下标，成功派发后递增，下次从下一个 worker 开始，实现 round-robin；worker 失败时会短暂冷却并尝试下一个可用 worker。worker 阻塞等待发生在后台 future 中，主线程持续调度并在 future 完成后统一更新数据库。

---

## 交付范围

- `oj_server` 路由、静态资源服务与鉴权能力
- 题库、题面、提交记录与提交结果查询 API
- MySQL 题库存储、用户存储与连接池封装
- Redis 列表缓存、提交队列与异步评测状态流转
- `judge_dispatcher` 多 worker 调度与主线程统一回写数据库
- Web 端首页、题库、提交、作业、排行榜与后台管理页面
- 管理员题目创建 / 编辑、测试数据追加、作业创建 / 编辑能力

## 目录结构

```text
oj_platform/
├─ third_party/             # 第三方依赖
│  └─ crow/                 # Crow 源码（当前为软链接）
├─ cmake/                   # 自定义 CMake 模块
├─ common/                  # 共享协议、路径工具、公共类型
├─ services/
│  ├─ oj_server/            # 对外服务：题目、提交、鉴权、静态页面
│  ├─ judge_worker/         # 判题节点：编译、运行、测试点执行
│  └─ judge_dispatcher/     # 队列消费者：从 Redis 取任务并驱动判题
├─ problems/                # 题库数据（每题一个目录）
├─ web/                     # 前端静态页面与 JS/CSS
├─ runtime/                 # 运行时数据：日志、临时文件、沙箱目录等
├─ tests/                   # 测试代码目录
└─ README.md
```

各子目录下均包含独立 `README.md`，方便单独阅读职责说明。

---

## 已实现的 HTTP 接口

### `oj_server`

#### 页面路由

- `GET /`：首页
- `GET /problems`：题库页
- `GET /problems/<id>`：题目详情页
- `GET /submit/<id>`：提交页
- `GET /submissions`：提交记录页
- `GET /submissions/<id>`：提交结果页
- `GET /assignments`：作业列表页
- `GET /assignments/<id>`：作业详情页
- `GET /assignments/<id>/leaderboard`：作业排行榜页
- `GET /web/<path>`：静态资源

#### 公共 API

- `GET /api/health`
- `GET /api/problems`
- `GET /api/problems/my-status`
- `GET /api/assignments`
- `GET /api/assignments/<id>`
- `GET /api/assignments/<id>/leaderboard`

#### 鉴权 API

- `POST /api/auth/register`
- `POST /api/auth/admin/register`
- `POST /api/auth/login`
- `GET /api/auth/me`

#### 需要登录的 API

- `GET /api/problems/<id>`
- `POST /api/submissions`（异步入队，返回 `202 Accepted`）
- `GET /api/submissions`
- `GET /api/submissions/<submission_id>`

#### 管理员 API

- `GET /api/admin/problems/<id>/statement`
- `PUT /api/admin/problems/<id>/statement`
- `PUT /api/admin/problems/<id>/id`
- `DELETE /api/admin/problems/<id>`
- `PUT /api/admin/problems/<id>/title`
- `PUT /api/admin/problems/<id>/limits`
- `POST /api/admin/problems/import`
- `POST /api/admin/problems/<id>/testcases/file`
- `POST /api/admin/problems`
- `POST /api/admin/assignments`
- `PATCH /api/admin/assignments/<id>`
- `POST /api/admin/assignments/<id>/problems`

### `judge_worker`

已具备独立服务目标和 HTTP 判题接口，dispatcher 会把 Redis 队列中的提交任务通过 `POST /api/judge` 派发给 worker。

### `judge_dispatcher`

- 从 Redis List（默认 key：`oj:queue:submissions`）阻塞消费提交任务
- 将提交状态从 `QUEUED` 更新为 `RUNNING`
- 通过 HTTP 调用 `judge_worker` 完成编译与测试点执行
- 支持从 `OJ_JUDGE_WORKERS` 或 `OJ_JUDGE_WORKER_1..3` 读取多个 worker，当前 compose 配置 3 个 worker
- 使用 round-robin 顺序派发；若某个 worker 失败，会进入短暂冷却并尝试下一个可用 worker
- worker 请求在后台 future 中等待返回，主线程持续调度后续任务
- 将状态更新为最终态，如 `OK / WRONG_ANSWER / TLE / SYSTEM_ERROR`

---

## 前端页面

- 首页可匿名查看项目介绍
- 题库页可匿名查看题目列表
- 点击“查看题面 / 提交代码”时，如果未登录，会弹出登录 / 注册框
- 登录成功后可访问题面、提交代码并查看结果
- 提交后结果页会自动轮询，展示 `QUEUED -> RUNNING -> FINAL_STATUS` 的变化
- 普通用户可查看作业列表、作业详情、作业排行榜与个人作业题目状态
- 管理员可创建题目、编辑题面、编辑题号/标题/时空限制、追加测试数据文件
- 管理员可创建作业、编辑作业、调整起止时间、补充作业题目
- 页面右上角显示当前登录用户并支持退出登录

---

## 构建

### 依赖

当前构建脚本依赖：

- C++17 编译器
- CMake 3.15+
- OpenSSL
- `crypt`（用于 bcrypt）
- hiredis
- redis++
- Redis Server（运行异步评测链路时需要）

> 注意：由于 `judge_dispatcher` 使用的是 Redis 阻塞队列消费，如果 Redis 连接超时时间过短，会出现“任务已经入队但长期停留在 QUEUED”的现象。当前默认超时已调大到 5 秒，以避免 `BLPOP` 被过早打断。

### 构建命令

```bash
cd /home/max85/webserver
cmake -S oj_platform -B oj_platform/build
cmake --build oj_platform/build -j
```

如果你希望直接复用仓库中的已验证构建目录，也可以使用：

```bash
cmake -S /home/max85/webserver/oj_platform -B /home/max85/webserver/oj_platform/build-auth-test
cmake --build /home/max85/webserver/oj_platform/build-auth-test -j
```

### 测试构建

项目已接入基于 `GoogleTest` 的单元测试与集成测试，默认关闭，需要在配置阶段显式打开：

```bash
cmake -S /home/max85/webserver/oj_platform \
  -B /home/max85/webserver/oj_platform/build-tests \
  -DOJ_PLATFORM_BUILD_TESTS=ON

cmake --build /home/max85/webserver/oj_platform/build-tests -j
```

构建完成后会生成两个测试目标：

- `oj_platform_unit_tests`
- `oj_platform_integration_tests`

### 测试运行

推荐直接使用 `ctest` 统一执行：

```bash
ctest --test-dir /home/max85/webserver/oj_platform/build-tests --output-on-failure
```

也可以单独运行测试二进制：

```bash
/home/max85/webserver/oj_platform/build-tests/oj_platform_unit_tests
/home/max85/webserver/oj_platform/build-tests/oj_platform_integration_tests
```

如需只验证某一类测试，可配合 `--gtest_filter`：

```bash
/home/max85/webserver/oj_platform/build-tests/oj_platform_unit_tests \
  --gtest_filter=CompileServiceTest.*:RunServiceTest.*

/home/max85/webserver/oj_platform/build-tests/oj_platform_integration_tests \
  --gtest_filter=JudgeWorkerHttpIntegrationTest.*
```

### 测试覆盖范围

当前测试覆盖以下关键模块：

- 协议编解码：`protocol_json`
- 调度器工具：worker 地址解析、轮询选择、失败信息归类
- 判题汇总：测试点结果汇总与最终状态生成
- 判题核心：请求携带测试点、题目录径回退、本地缓存测试点加载
- 平台配置：环境变量读取与默认值逻辑
- 路径工具：项目路径解析、可执行文件目录定位
- 编译服务：成功编译、编译失败、编译结果产物检查
- 运行服务：正常执行、运行时错误、超时处理
- judge_worker HTTP 集成链路：`POST /api/judge` 成功请求与非法 JSON 请求

### 测试说明

- 单元测试主要验证公共工具、判题核心逻辑以及编译/运行组件的本地行为。
- 集成测试会在进程内启动一个临时 `Crow` 服务，直接调用 `judge_worker` 的 HTTP 路由，验证序列化、路由注册、判题执行与返回结果的完整链路。
- 测试使用 `tests/test_support/` 下的辅助组件统一处理临时目录、环境变量恢复和最小 HTTP 客户端请求。

### 测试环境要求

- 运行编译与判题相关测试时，本机需要可用的 `g++`。
- `judge_worker` / `JudgeCore` 的对象缓存测试默认优先命中本地缓存文件，不要求本机必须安装 `mc` 或启动 MinIO。
- 如在极端受限的沙箱环境中运行测试，若宿主机禁止编译器派生其内部子进程，编译相关测试将无法通过。当前实现已经修正了编译阶段不合理的进程数限制，正常 Linux 开发环境下可稳定通过。

---

## 运行

### 启动 `oj_server`

```bash
/home/max85/webserver/oj_platform/build-auth-test/oj_server
```

默认监听：

- `oj_server`: `18080`

访问：

```text
http://127.0.0.1:18080
```

### 启动 `judge_worker`

```bash
/home/max85/webserver/oj_platform/build-auth-test/judge_worker
```

默认监听：

- `judge_worker`: `18081`

### 启动 `judge_dispatcher`

```bash
/home/max85/webserver/oj_platform/build-auth-test/judge_dispatcher
```

该进程本身不提供 HTTP 接口，负责后台消费 Redis 队列。

### 推荐启动顺序

```bash
redis-server --port 6379
/home/max85/webserver/oj_platform/build-auth-test/judge_dispatcher
/home/max85/webserver/oj_platform/build-auth-test/oj_server
```

本地多 worker 调试时可在不同终端用不同端口启动 `judge_worker`，再设置 `OJ_JUDGE_WORKERS` 或 `OJ_JUDGE_WORKER_1..3` 后启动 `judge_dispatcher`。Docker Compose 默认已经配置 3 个 worker，通常推荐直接使用 `docker compose up -d --build`。

---

## 题库源文件格式

每道题使用一个独立目录，例如：

```text
problems/<problem_id>/
├─ meta.json
├─ statement_zh.md
├─ checker.cpp
└─ tests/
   ├─ 1.in
   ├─ 1.out
   ├─ 2.in
   └─ 2.out
```

### `meta.json`

示例：

```json
{
  "id": 2000,
  "title": "Problem Title",
  "time_limit_ms": 1000,
  "memory_limit_mb": 128,
  "tags": ["implementation", "math"]
}
```

### 约定

- `statement_zh.md`：题面
- `tests/N.in`：输入
- `tests/N.out`：标准输出
- `checker.cpp`：特殊判题器预留（当前默认未真正编译使用）
