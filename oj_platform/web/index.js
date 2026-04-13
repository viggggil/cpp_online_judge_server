async function loadProblems() {
  const container = document.getElementById('problem-list');
  container.innerHTML = '加载中...';
  const response = await fetch('/api/problems');
  const data = await response.json();
  const problems = data.problems || [];
  container.innerHTML = '';

  for (const problem of problems) {
    const item = document.createElement('div');
    item.className = 'list-item';
    item.innerHTML = `
      <h3>${problem.id} - ${problem.title}</h3>
      <p>难度：${problem.difficulty}</p>
      <div class="actions">
        <a class="button" href="/problems/${problem.id}">查看题面</a>
        <a class="button" href="/submit/${problem.id}">提交代码</a>
      </div>
    `;
    container.appendChild(item);
  }
}

loadProblems().catch(err => {
  document.getElementById('problem-list').textContent = `加载失败: ${err.message}`;
});