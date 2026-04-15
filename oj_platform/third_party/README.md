# third_party

该目录用于存放第三方依赖源码或其软链接。

当前已接入：

- `crow/`：Crow Web Framework

当前工程通过 `add_subdirectory(third_party/crow)` 的方式把 Crow 纳入构建系统。

后续如果需要，也可以继续放入：

- JSON / 序列化库
- 测试框架
- 沙箱相关依赖
- RPC / 网络通信相关依赖