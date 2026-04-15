# judge_worker

`judge_worker` 表示平台中的判题工作节点。

当前目录中的代码已经拆分出几个明确职责：

- `judge_core.*`：判题主流程
- `compile_service.*`：负责编译用户代码
- `run_service.*`：负责执行编译产物并收集结果
- `routes.*`：worker 的 HTTP 接口
- `main.cpp`：服务入口

当前项目中，判题核心仍可被本地直接调用；未来可以进一步演进为：

- 独立部署的远程 worker
- 支持心跳注册
- 支持任务拉取 / 推送
- 支持负载上报
- 支持沙箱、资源限制与多语言运行时