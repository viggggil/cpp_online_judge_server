function problemIdFromPath() {
  const parts = window.location.pathname.split('/').filter(Boolean);
  return parts[1];
}

async function loadProblemInfo() {
  if (!window.ojAuth.protectPage()) {
    return;
  }
  const id = problemIdFromPath();
  document.getElementById('problem-link').href = `/problems/${id}`;
  const response = await window.ojAuth.authFetch(`/api/problems/${id}`);
  if (!response.ok) throw new Error('题目不存在');
  const problem = await response.json();
  document.getElementById('problem-info').textContent = `${problem.id} - ${problem.title}\n时间限制: ${problem.time_limit_ms} ms | 内存限制: ${problem.memory_limit_mb} MB`;
}

async function submitCode() {
  const id = problemIdFromPath();
  const sourceCode = document.getElementById('source_code').value;
  const language = document.getElementById('language').value;
  const message = document.getElementById('submit-message');
  message.textContent = '提交中...';

  const response = await window.ojAuth.authFetch('/api/submissions', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      problem_id: id,
      language,
      source_code: sourceCode
    })
  });

  const result = await response.json();
  if (!response.ok) {
    throw new Error(result.error || '提交失败');
  }
  message.textContent = `提交成功，状态: ${result.status}`;
  if (result.submission_id) {
    window.location.href = `/submissions/${result.submission_id}`;
  }
}

document.getElementById('submit-btn').addEventListener('click', () => {
  submitCode().catch(err => {
    document.getElementById('submit-message').textContent = `提交失败: ${err.message}`;
  });
});

loadProblemInfo().catch(err => {
  document.getElementById('problem-info').textContent = `加载失败: ${err.message}`;
});