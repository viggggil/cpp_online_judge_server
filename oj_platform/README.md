# oj_platform

一个基于 **Crow** 实现的轻量级在线判题平台原型工程，目标是逐步演进成“题库 + 提交 + 判题 + 负载均衡 worker + Web 前端”的完整 OJ 系统。

当前仓库已经不是只有目录骨架，而是具备了一个可以运行和演示的最小版本：

- 使用 `Crow` 作为第三方 Web 框架
- 拥有 `oj_server` 与 `judge_worker` 两个服务目标
- 支持题目列表、题目详情、代码提交、提交结果查看
- 支持本地题库存储与测试点读取
- 支持用户注册 / 登录
- 密码采用 `bcrypt` 哈希保存
- 登录态采用 JWT
- 前端页面支持登录弹窗、提交与结果展示
- 支持 Redis 提交队列、异步评测状态流转与轮询展示

> 说明：当前版本已把“提交入口”和“判题消费”拆成异步队列模型：`oj_server` 负责入队，`judge_dispatcher` 负责消费并调用本地判题核心；后续可以继续把 dispatcher 演进成真正的远程调度和多 worker 负载均衡架构。

---

## 当前项目状态

### 已完成

- [x] 基础目录结构搭建
- [x] Crow 作为第三方库接入
- [x] `oj_server` 路由与静态资源服务
- [x] 题目列表 / 题面详情 API
- [x] 提交代码 / 查询提交结果 API
- [x] 本地测试点驱动的判题流程
- [x] 登录注册能力
- [x] JWT 鉴权
- [x] Web 端登录弹窗与登录态控制
- [x] Redis 题目列表缓存接入代码
- [x] Redis 提交队列与异步评测状态轮询
- [x] 独立 `judge_dispatcher` 消费进程
- [x] 慢题示例 `1005` 与 20 组测试数据

### 当前仍属于原型 / 待增强

- [ ] 远程 `judge_worker` HTTP 调度与真正负载均衡
- [ ] 多语言支持（当前主要按 C++17 路径组织）
- [ ] 沙箱隔离进一步增强
- [ ] 数据库存储用户/提交/题目元数据
- [ ] 管理后台、题目录入后台、队列监控面板
- [ ] 更完整的单元测试与集成测试

---

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
├─ runtime/                 # 运行时数据：提交记录、用户文件等
├─ tests/                   # 测试代码目录（预留）
└─ README.md
```

各子目录下还补充了独立 `README.md`，方便单独阅读职责说明。

---

## 已实现的 HTTP 接口

### `oj_server`

#### 页面路由

- `GET /`：题目列表页
- `GET /problems/<id>`：题目详情页
- `GET /submit/<id>`：提交页
- `GET /submissions/<id>`：提交结果页
- `GET /web/<path>`：静态资源

#### 公共 API

- `GET /api/health`
- `GET /api/problems`

#### 鉴权 API

- `POST /api/auth/register`
- `POST /api/auth/login`
- `GET /api/auth/me`

#### 需要登录的 API

- `GET /api/problems/<id>`
- `POST /api/submissions`（异步入队，返回 `202 Accepted`）
- `GET /api/submissions/<submission_id>`

### `judge_worker`

目前已具备独立服务目标和基础路由骨架，便于后续改造成真实的远程判题节点。

### `judge_dispatcher`

- 从 Redis List（默认 key：`oj:queue:submissions`）阻塞消费提交任务
- 将提交状态从 `QUEUED` 更新为 `RUNNING`
- 调用判题核心完成编译与测试点执行
- 将状态更新为最终态，如 `OK / WRONG_ANSWER / TLE / SYSTEM_ERROR`

---

## 当前前端行为

- 首页可匿名查看题目列表
- 点击“查看题面 / 提交代码”时，如果未登录，会弹出登录 / 注册框
- 登录成功后可访问题面、提交代码并查看结果
- 提交后结果页会自动轮询，展示 `QUEUED -> RUNNING -> FINAL_STATUS` 的变化
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

### 构建命令

```bash
cd /home/max85/webserver
cmake -S oj_platform -B oj_platform/build
cmake --build oj_platform/build -j
```

如果你只是复用当前已验证的构建目录，也可以使用：

```bash
cmake -S /home/max85/webserver/oj_platform -B /home/max85/webserver/oj_platform/build-auth-test
cmake --build /home/max85/webserver/oj_platform/build-auth-test -j
```

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

如果你要继续调试 `judge_worker` 的 HTTP 接口，也可以单独启动它。

---

## 题库格式说明

每道题使用一个独立目录，例如：

```text
problems/1000/
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
  "id": 1000,
  "title": "A + B Problem",
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

---

## 目前内置题目

当前题库包含：

- `1000` A + B Problem
- `1001` 两数之和（数组版）
- `1002` 最长不重复子串长度
- `1003` 合并两个有序数组
- `1004` 二叉树层序遍历（数组输入版）
- `1005` 失落文明（困难版，异步慢题演示）

> 这些题目是参考常见面试 / Hot100 类型自行整理的训练题，不直接复制第三方平台原题文本。

---

## 下一步建议

1. 把 `judge_dispatcher -> judge_worker` 的本地调用改为 HTTP / RPC 派发
2. 增加多个 worker 的注册、心跳与调度策略
3. 补充用户、题目、提交的数据持久化
4. 接入管理员后台与题目录入工具
5. 增加更多题目与更完整的特殊判题支持

---

## 说明

如果你后面希望继续往“负载均衡在线判题系统”方向推进，我建议下一阶段优先做：

1. worker 注册与心跳
2. 任务队列
3. 调度策略（轮询 / 最少连接 / 按负载）
4. 提交异步化
5. 判题沙箱隔离强化
