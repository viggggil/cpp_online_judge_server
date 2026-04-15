# common

该目录存放 **跨服务共享** 的基础代码，避免 `oj_server` 和 `judge_worker` 重复定义协议与工具函数。

当前主要包含：

- `protocol.hpp`：判题请求/响应、题目详情、测试点等协议定义
- `platform_types.h`：面向平台上层的公共数据结构
- `platform_config.h`：平台配置项定义
- `path_utils.h/.cpp`：可执行文件路径与项目根路径解析工具

适合继续放入这里的内容：

- 公共 DTO / VO
- 配置加载器
- 日志封装
- 公共错误码
- JSON 编解码辅助函数