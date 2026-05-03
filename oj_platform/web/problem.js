function problemIdFromPath() {
  const parts = window.location.pathname.split('/').filter(Boolean);
  return parts[1];
}

async function fetchCurrentUser() {
  const response = await window.ojAuth.authFetch('/api/auth/me');
  if (!response.ok) {
    throw new Error('获取用户信息失败');
  }
  return response.json();
}

function bindEditButton(problemId, currentUser) {
  const editButton = document.getElementById('edit-problem-btn');
  if (!editButton) return;

  if (!currentUser?.is_admin) {
    editButton.classList.add('hidden');
    return;
  }

  editButton.classList.remove('hidden');
  editButton.addEventListener('click', () => {
    if (!currentUser || !currentUser.is_admin) {
      alert('权限不足，仅管理员可以编辑题面');
      return;
    }
    window.location.href = `/web/admin-problem-edit.html?problem_id=${encodeURIComponent(problemId)}`;
  });
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

// 加载题面页数据，并在管理员访问时额外开放后台编辑入口。
async function loadProblem() {
  await window.ojAuth.initAuth();
  if (!window.ojAuth.protectPage()) {
    return;
  }
  const id = problemIdFromPath();
  const [problemResponse, currentUser] = await Promise.all([
    window.ojAuth.authFetch(`/api/problems/${id}`),
    fetchCurrentUser().catch(() => null),
  ]);
  const response = problemResponse;
  if (!response.ok) throw new Error('题目不存在');
  const problem = await response.json();

  document.getElementById('title').textContent = `${problem.id} - ${problem.title}`;
  document.getElementById('meta').textContent = `时间限制: ${problem.time_limit_ms} ms\n内存限制: ${problem.memory_limit_mb} MB\n标签: ${(problem.tags || []).join(', ')}`;
  document.getElementById('statement').innerHTML = window.ojMarkdown.markdownToHtml(problem.statement_markdown || '');
  document.getElementById('submit-link').href = `/submit/${problem.id}`;
  bindEditButton(problem.id, currentUser);
  bindTopNavigation();
}

loadProblem().catch(err => {
  document.getElementById('title').textContent = `加载失败: ${err.message}`;
});
