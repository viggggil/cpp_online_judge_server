# web

该目录存放平台前端静态资源。

当前采用最轻量的组织方式：

- `index.html/js`：题目列表页
- `problem.html/js`：题目详情页
- `submit.html/js`：提交页
- `submission.html/js`：提交结果页
- `auth.js`：登录/注册弹窗与登录态管理
- `styles.css`：公共样式

当前是原生 HTML + JS 版本，优点是简单直接，便于快速验证后端流程。

后续可以逐步升级为：

- Vue / React 前端
- 更细的组件拆分
- 统一 API 封装
- 更友好的题面渲染与代码编辑器