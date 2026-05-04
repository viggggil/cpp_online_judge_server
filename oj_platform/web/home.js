const HOME_INTRO_MARKDOWN = `
# OJ Platform

一个基于 **Crow** 实现的轻量级在线判题平台原型工程，目标是逐步演进成完整的在线判题系统，覆盖题库、提交、判题调度、分布式 worker 与 Web 前端。

## 当前项目能力

- 支持题目列表、题目详情、代码提交与提交结果查看
- 支持作业列表、作业详情与管理员创建作业
- 支持用户注册、登录、JWT 鉴权与前端登录态控制
- 支持基于 MySQL 的题目、题面、标签与测试点读取
- 支持 Redis 题目列表缓存与提交异步队列
- 支持独立的 \`judge_dispatcher\` 与多个 \`judge_worker\` 轮询分发任务
- 支持管理员创建题目、编辑题面、维护题目编号、名称及时空限制

## 运行拓扑

\`\`\`
Browser -> oj_server -> Redis queue -> judge_dispatcher -> judge_worker
                     \\
                      -> MySQL
\`\`\`

## 使用方式

- 在“题库”页浏览题目并进入题面
- 在“作业”页查看课程作业与对应题目列表
- 在“提交”页查看历史提交记录
- 管理员可在“创建”页导入题目包、手动创建题目，或创建作业
- 新建空题目的测试数据可以后续在编辑页追加

## 项目定位

当前版本已经不是单纯的目录骨架，而是一个可以运行和演示的最小 OJ 原型，适合继续扩展判题能力、管理后台和前端体验。
`;

async function initHomePage() {
  await window.ojAuth.initAuth();
  document.getElementById('home-intro').innerHTML = window.ojMarkdown.markdownToHtml(HOME_INTRO_MARKDOWN);
  window.ojNav.bindProtectedNavigation();
}

initHomePage().catch((error) => {
  document.getElementById('home-intro').textContent = `加载失败: ${error.message}`;
});
