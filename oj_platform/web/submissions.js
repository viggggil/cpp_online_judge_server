function formatTimestamp(timestamp) {
  if (!timestamp) {
    return '-';
  }
  return new Date(timestamp * 1000).toLocaleString('zh-CN');
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

function renderStatus(status) {
  return ['OK', 'QUEUED', 'RUNNING'].includes(status)
    ? `<span class="status-ok">${status}</span>`
    : `<span class="status-bad">${status}</span>`;
}

// 加载当前用户的提交历史，并按列表形式展示状态、耗时和跳转链接。
async function loadSubmissions() {
  await window.ojAuth.initAuth();
  if (!window.ojAuth.protectPage()) {
    return;
  }

  bindTopNavigation();

  const container = document.getElementById('submission-list');
  container.textContent = '加载中...';

  const response = await window.ojAuth.authFetch('/api/submissions');
  if (!response.ok) {
    throw new Error('加载提交列表失败');
  }

  const data = await response.json();
  const submissions = data.submissions || [];
  if (!submissions.length) {
    container.textContent = '暂无提交记录。';
    return;
  }

  container.innerHTML = '';
  for (const item of submissions) {
    const div = document.createElement('div');
    div.className = 'list-item';
    div.innerHTML = `
      <h3><a href="/submissions/${item.submission_id}">${item.submission_id}</a></h3>
      <p>题号：${item.problem_id} ｜ 语言：${item.language}</p>
      <p>当前状态：${renderStatus(item.status)} ｜ 最终结果：${renderStatus(item.final_status || item.status)}</p>
      <p>耗时：${item.total_time_used_ms ?? 0} ms ｜ 内存：${item.peak_memory_used_kb ?? 0} KB</p>
      <p>提交时间：${formatTimestamp(item.created_at)}</p>
      <p>${item.detail || ''}</p>
    `;
    container.appendChild(div);
  }
}

loadSubmissions().catch(err => {
  document.getElementById('submission-list').textContent = `加载失败: ${err.message}`;
});
