# judge_worker

`judge_worker` 表示平台中的判题工作节点。

当前目录中的代码已经拆分出几个明确职责：

- `judge_core.*`：判题主流程
- `compile_service.*`：负责编译用户代码
- `run_service.*`：负责执行编译产物并收集结果
- `routes.*`：worker 的 HTTP 接口
- `main.cpp`：服务入口

当前项目中，`judge_worker` 已经是可独立部署的 HTTP 判题节点：

- 默认监听 `OJ_JUDGE_WORKER_PORT`，未设置时为 `18081`
- 提供 `POST /api/judge`，接收 dispatcher 发送的判题请求
- Docker Compose 默认启动 `judge_worker_1`、`judge_worker_2`、`judge_worker_3` 三个实例
- `judge_dispatcher` 会按 round-robin 顺序调用这些 worker；worker 异常时会短暂冷却并尝试下一个可用节点

后续可以进一步演进为：

- 支持心跳注册
- 支持任务拉取 / 推送
- 支持负载上报
- 支持沙箱、资源限制与多语言运行时