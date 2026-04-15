# oj_server

`oj_server` 是当前平台的主入口服务，对外提供：

- 页面访问
- 静态资源
- 题目列表 / 题目详情 API
- 提交代码 API
- 查询提交结果 API
- 用户注册 / 登录 / 登录态校验

当前实现特点：

- 使用 Crow 提供 HTTP 路由
- 题库直接从 `problems/` 目录读取
- 提交结果保存在 `runtime/submissions/`
- 用户信息保存在 `runtime/users/users.json`
- 登录态使用 JWT
- 密码使用 bcrypt 哈希

当前还属于原型结构，后续可继续增强：

- 接入数据库
- 接入异步评测队列
- 通过 HTTP / RPC 调度远程 worker
- 增加管理员接口与题目录入接口