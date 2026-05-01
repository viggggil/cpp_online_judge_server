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

function setCreateEntryVisible(currentUser) {
  const createLink = document.getElementById('nav-problem-create');
  if (!createLink) {
    return;
  }

  createLink.classList.toggle('hidden', !currentUser?.is_admin);
}

// 根据当前登录用户是否为管理员，动态决定首页导航里是否展示出题入口。
async function refreshAdminNavigation() {
  try {
    const currentUser = await fetchCurrentUserOptional();
    setCreateEntryVisible(currentUser);
  } catch (_) {
    setCreateEntryVisible(null);
  }
}

// 加载题目列表并绑定需要登录或管理员权限的导航与入口行为。
async function loadProblems() {
  await window.ojAuth.initAuth();
  await refreshAdminNavigation();

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
        <a class="button problem-link" href="/problems/${problem.id}">查看题面</a>
        <a class="button submit-link" href="/submit/${problem.id}">提交代码</a>
      </div>
    `;

    item.querySelector('.problem-link').addEventListener('click', (event) => {
      if (!window.ojAuth.requireLogin('查看题面前请先登录')) {
        event.preventDefault();
      }
    });
    item.querySelector('.submit-link').addEventListener('click', (event) => {
      if (!window.ojAuth.requireLogin('提交代码前请先登录')) {
        event.preventDefault();
      }
    });

    container.appendChild(item);
  }

  document.getElementById('nav-submissions')?.addEventListener('click', (event) => {
    if (!window.ojAuth.requireLogin('查看提交记录前请先登录')) {
      event.preventDefault();
    }
  });

  document.getElementById('nav-problem-create')?.addEventListener('click', async (event) => {
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

window.addEventListener('oj-auth-changed', () => {
  refreshAdminNavigation().catch(() => {
    setCreateEntryVisible(null);
  });
});

loadProblems().catch(err => {
  document.getElementById('problem-list').textContent = `加载失败: ${err.message}`;
});
