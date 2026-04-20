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
- 题目、题面、测试点统一从 MySQL 读取
- 提交结果保存在 `runtime/submissions/`
- 用户信息保存在 `runtime/users/users.json`
- 登录态使用 JWT
- 密码使用 bcrypt 哈希
- MySQL 访问通过简单连接池复用连接

## 提交结果页显示

- 默认只显示每个测试点的状态、耗时、内存占用
- 点击下箭头后展开详细信息
- 详细信息包含输入、期望输出、实际输出、错误信息
- 过长内容默认只展示前缀并追加省略号，避免页面过长

## 题目录入约定

新增题目时，先把题目源文件放在 `problems/<id>/` 下维护，再执行：

```bash
cd /home/max85/webserver/oj_platform && ./build-mysql-check/problem_migrator
```

迁移完成后，`oj_server` 才能正常查询该题并接受提交。

当前还属于原型结构，后续可继续增强：

- 接入数据库
- 接入异步评测队列
- 通过 HTTP / RPC 调度远程 worker
- 增加管理员接口与题目录入接口