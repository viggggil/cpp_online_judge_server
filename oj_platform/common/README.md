# common

该目录存放 **跨服务共享** 的基础代码，避免 `oj_server` 和 `judge_worker` 重复定义协议与工具函数。

主要包含：

- `protocol.hpp`：判题请求/响应、题目详情、测试点等协议定义
- `platform_types.h`：面向平台上层的公共数据结构
- `platform_config.h`：平台配置项定义
- `path_utils.h/.cpp`：可执行文件路径与项目根路径解析工具

目录中的代码以跨服务复用为目标，适合承载公共 DTO / VO、配置加载、日志封装、错误码以及 JSON 编解码辅助函数等基础能力。
