function problemIdFromPath() {
  const parts = window.location.pathname.split('/').filter(Boolean);
  return parts[1];
}

async function fetchCurrentUserOptional() {
  if (!window.ojAuth.isLoggedIn()) {
    return null;
  }

  const response = await fetch('/api/auth/me', {
    headers: {
      Authorization: `Bearer ${window.ojAuth.getToken()}`,
    },
  });

  if (!response.ok) {
    return null;
  }

  return response.json();
}

function bindTopNavigation() {
  document.querySelector('.nav-submissions')?.addEventListener('click', (event) => {
    if (!window.ojAuth.requireLogin('查看提交记录前请先登录')) {
      event.preventDefault();
    }
  });

  document.querySelector('.nav-create')?.addEventListener('click', async (event) => {
    if (!window.ojAuth.requireLogin('进入创建页前请先登录')) {
      event.preventDefault();
      return;
    }

    const currentUser = await fetchCurrentUserOptional();
    if (!currentUser?.is_admin) {
      event.preventDefault();
      alert('权限不足，仅管理员可以进入题目创建页面');
    }
  });
}

// 加载提交页顶部的题目信息，帮助用户在提交前确认当前题号与限制。
async function loadProblemInfo() {
  await window.ojAuth.initAuth();
  if (!window.ojAuth.protectPage()) {
    return;
  }
  const id = problemIdFromPath();
  const response = await window.ojAuth.authFetch(`/api/problems/${id}`);
  if (!response.ok) throw new Error('题目不存在');
  const problem = await response.json();
  document.getElementById('problem-info').textContent = `${problem.id} - ${problem.title}\n时间限制: ${problem.time_limit_ms} ms | 内存限制: ${problem.memory_limit_mb} MB`;
  bindTopNavigation();
}

// 收集用户填写的代码和语言，并调用提交接口后跳转到对应的评测详情页。
async function submitCode() {
  await window.ojAuth.initAuth();
  if (!window.ojAuth.requireLogin('提交代码前请先登录')) {
    return;
  }
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
  message.textContent = `提交成功，当前状态: ${result.status}`;
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
