# services

该目录放置平台中的各类服务进程。

目录包含三个主要服务：

- `oj_server/`：面向用户和前端的主服务
- `judge_dispatcher/`：从 Redis 提交队列取任务，并按 round-robin 通过 HTTP 派发到 worker
- `judge_worker/`：执行编译、运行、判题的工作节点；Docker Compose 默认启动 3 个实例
