# oj_platform

基于 Crow 的在线判题平台骨架工程。

## 目录结构

```text
oj_platform/
├─ third_party/
│  └─ crow/                 # 当前为软链接，指向 ../../Crow
├─ common/                  # 跨服务共享类型与配置
├─ services/
│  ├─ oj_server/            # 对外 API 服务：题目、提交、查询
│  └─ judge_worker/         # 判题工作节点：编译、运行、判题
├─ problems/                # 题目数据
├─ web/                     # 前端资源
├─ runtime/                 # 运行时目录（编译产物/日志/沙箱）
└─ tests/                   # 测试代码
```

## 当前已实现的骨架

- `oj_server`
  - `GET /`
  - `GET /api/health`
  - `GET /api/problems`
  - `POST /api/submissions`
- `judge_worker`
  - `GET /`
  - `GET /api/health`
  - `POST /api/judge`

## 构建

```bash
cd /home/max85/webserver
cmake -S oj_platform -B oj_platform/build
cmake --build oj_platform/build -j
```

## 运行

```bash
/home/max85/webserver/oj_platform/build/oj_server
/home/max85/webserver/oj_platform/build/judge_worker
```

默认端口：

- `oj_server`: `18080`
- `judge_worker`: `18081`

## 后续建议扩展

1. 增加题目持久化与配置文件加载。
2. 让 `oj_server` 通过 HTTP/RPC 调度 `judge_worker`。
3. 补充沙箱执行、编译缓存、测试点比对与结果回传。
4. 为 `web/` 增加题目列表、提交页和状态轮询页面。
