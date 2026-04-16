function problemIdFromPath() {
  const parts = window.location.pathname.split('/').filter(Boolean);
  return parts[1];
}

async function loadProblem() {
  await window.ojAuth.initAuth();
  if (!window.ojAuth.protectPage()) {
    return;
  }
  const id = problemIdFromPath();
  const response = await window.ojAuth.authFetch(`/api/problems/${id}`);
  if (!response.ok) throw new Error('题目不存在');
  const problem = await response.json();

  document.getElementById('title').textContent = `${problem.id} - ${problem.title}`;
  document.getElementById('meta').textContent = `时间限制: ${problem.time_limit_ms} ms\n内存限制: ${problem.memory_limit_mb} MB\n标签: ${(problem.tags || []).join(', ')}`;
  document.getElementById('statement').textContent = problem.statement || '';
  document.getElementById('submit-link').href = `/submit/${problem.id}`;

  const samples = document.getElementById('samples');
  samples.innerHTML = '<h2>样例</h2>';
  (problem.samples || []).forEach((sample, index) => {
    const div = document.createElement('div');
    div.className = 'list-item';
    div.innerHTML = `<h3>样例 ${index + 1}</h3><pre class="notice">输入:\n${sample.input}\n\n输出:\n${sample.output}</pre>`;
    samples.appendChild(div);
  });
}

loadProblem().catch(err => {
  document.getElementById('title').textContent = `加载失败: ${err.message}`;
});