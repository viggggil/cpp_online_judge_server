const HOME_INTRO_MARKDOWN = `
# C++ 在线判题平台

**C++17 / Crow / MySQL / Redis / Linux**

设计并实现在线判题平台后端，将提交服务、任务调度和判题执行拆分为 \`oj_server\`、\`judge_dispatcher\`、\`judge_worker\` 三类进程，降低提交请求与耗时判题任务之间的耦合。

基于 Redis List 构建异步任务队列，dispatcher 阻塞消费提交任务，将状态从 \`QUEUED\` 更新为 \`RUNNING\`，并在 worker 返回后统一回写最终判题状态。

实现 dispatcher 到多个 judge worker 的 HTTP 调度，支持 round-robin 派发、worker 异常冷却和失败重试，提高判题节点扩展能力。

实现判题 worker 的编译与运行模块，使用 \`fork\` / \`exec\` / \`waitpid\` 管理子进程，使用 \`dup2\` 完成输入输出重定向，使用 \`setrlimit\` 限制 CPU、内存、文件大小、进程数和文件描述符。

设计 MySQL 表结构存储题目、测试点、提交记录、测试点明细和作业数据，并针对用户提交记录、题目提交记录等查询路径建立索引。

使用 Docker Compose 编排 MySQL、Redis、主服务、dispatcher 和多个 worker，实现服务化部署和完整判题链路演示。

## 当前项目能力

- 支持题目列表、题目详情、代码提交与提交结果查看
- 支持作业列表、作业详情与管理员创建作业
- 支持用户注册、登录、JWT 鉴权与前端登录态控制
- 支持基于 MySQL 的题目、题面、标签与测试点读取
- 支持 Redis 题目列表缓存与提交异步队列
- 支持独立的 \`judge_dispatcher\` 与多个 \`judge_worker\` 轮询分发任务
- 支持管理员创建题目、编辑题面、维护题目编号、名称及时空限制
`;

async function initHomePage() {
  await window.ojAuth.initAuth();
  document.getElementById('home-intro').innerHTML = window.ojMarkdown.markdownToHtml(HOME_INTRO_MARKDOWN);
  window.ojNav.bindProtectedNavigation();
}

initHomePage().catch((error) => {
  document.getElementById('home-intro').textContent = `加载失败: ${error.message}`;
});
