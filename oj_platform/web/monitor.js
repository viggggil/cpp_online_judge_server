function monitorFormatTimestamp(timestamp) {
  if (!timestamp) {
    return '-';
  }
  return new Date(timestamp * 1000).toLocaleString('zh-CN');
}

function monitorRenderStatus(status) {
  return ['OK', 'QUEUED', 'RUNNING', 'ACCEPTED'].includes(status)
    ? `<span class="status-ok">${status}</span>`
    : `<span class="status-bad">${status}</span>`;
}

function setMonitorStatus(message, isError = false) {
  const node = document.getElementById('monitor-status-message');
  node.textContent = message;
  node.classList.toggle('status-bad', isError);
  node.classList.toggle('status-ok', !isError && Boolean(message));
}

async function monitorFetchCurrentUserOptional() {
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

function setMonitorPermissionNotice(message, isError = false) {
  const node = document.getElementById('monitor-permission-notice');
  if (!message) {
    node.textContent = '';
    node.classList.add('hidden');
    node.classList.remove('status-bad', 'status-ok');
    return;
  }

  node.textContent = message;
  node.classList.remove('hidden');
  node.classList.toggle('status-bad', isError);
  node.classList.toggle('status-ok', !isError);
}

function setMonitorControlsDisabled(disabled) {
  ['monitor-problem-id-input', 'monitor-limit-input', 'monitor-search-btn', 'monitor-reset-btn']
    .forEach((id) => {
      const node = document.getElementById(id);
      if (node) {
        node.disabled = disabled;
      }
    });
}

function renderMonitorItems(items) {
  const container = document.getElementById('monitor-submission-list');
  if (!items.length) {
    container.textContent = '没有符合条件的提交记录。';
    return;
  }

  container.innerHTML = '';
  for (const item of items) {
    const div = document.createElement('div');
    div.className = 'list-item';
    div.innerHTML = `
      <h3><a href="/submissions/${item.submission_id}">${item.submission_id}</a></h3>
      <p>用户：${item.username} ｜ 题号：${item.problem_id} ｜ 语言：${item.language}</p>
      <p>当前状态：${monitorRenderStatus(item.status)} ｜ 最终结果：${monitorRenderStatus(item.final_status || item.status)}</p>
      <p>耗时：${item.total_time_used_ms ?? 0} ms ｜ 内存：${item.peak_memory_used_kb ?? 0} KB</p>
      <p>提交时间：${monitorFormatTimestamp(item.created_at)}</p>
      <p>${item.detail || ''}</p>
    `;
    container.appendChild(div);
  }
}

async function loadMonitorSubmissions(currentUser) {
  if (!window.ojAuth.requireLogin('进入监控页前请先登录')) {
    return;
  }
  if (!currentUser?.is_admin) {
    setMonitorStatus('权限不足，需要管理员权限才能查看监控页面', true);
    return;
  }

  const limitValue = Number(document.getElementById('monitor-limit-input').value || '20');
  const limit = Number.isInteger(limitValue) ? Math.max(1, Math.min(100, limitValue)) : 20;
  const problemId = document.getElementById('monitor-problem-id-input').value.trim();

  const params = new URLSearchParams();
  params.set('limit', String(limit));
  if (problemId) {
    params.set('problem_id', problemId);
  }

  const container = document.getElementById('monitor-submission-list');
  container.textContent = '加载中...';
  setMonitorStatus('正在查询最近提交记录...');

  const response = await window.ojAuth.authFetch(`/api/admin/monitor/submissions?${params.toString()}`);
  const data = await response.json();
  if (!response.ok) {
    throw new Error(data.error || '加载监控数据失败');
  }

  renderMonitorItems(data.items || []);
  setMonitorStatus(`已加载 ${data.total_shown || 0} 条提交记录`);
}

function applyMonitorPermissionState(currentUser) {
  const subtitle = document.getElementById('monitor-subtitle');
  const container = document.getElementById('monitor-submission-list');

  if (!currentUser) {
    subtitle.textContent = '请先登录；只有管理员可以查看全站提交监控';
    setMonitorPermissionNotice('当前页面仅管理员可用。普通用户可以看到入口，但无法查看监控数据。', true);
    setMonitorControlsDisabled(true);
    container.textContent = '';
    return;
  }

  if (!currentUser.is_admin) {
    subtitle.textContent = `当前用户：${currentUser.username}`;
    setMonitorPermissionNotice('当前账号不是管理员，无法查看全站提交监控。', true);
    setMonitorControlsDisabled(true);
    container.textContent = '';
    return;
  }

  subtitle.textContent = `当前管理员：${currentUser.username}`;
  setMonitorPermissionNotice('可查看最近提交记录，并按题号筛选。');
  setMonitorControlsDisabled(false);
}

async function initMonitorPage() {
  await window.ojAuth.initAuth();
  window.ojNav.bindProtectedNavigation();

  let currentUser = await monitorFetchCurrentUserOptional();
  applyMonitorPermissionState(currentUser);

  document.getElementById('monitor-search-btn').addEventListener('click', async () => {
    await loadMonitorSubmissions(currentUser);
  });

  document.getElementById('monitor-reset-btn').addEventListener('click', async () => {
    document.getElementById('monitor-problem-id-input').value = '';
    document.getElementById('monitor-limit-input').value = '20';
    await loadMonitorSubmissions(currentUser);
  });

  window.addEventListener('oj-auth-changed', async () => {
    currentUser = await monitorFetchCurrentUserOptional();
    applyMonitorPermissionState(currentUser);
  });

  if (currentUser?.is_admin) {
    await loadMonitorSubmissions(currentUser);
  }
}

initMonitorPage().catch((error) => {
  setMonitorStatus(error.message || '页面初始化失败', true);
  document.getElementById('monitor-submission-list').textContent =
    `加载失败: ${error.message || '页面初始化失败'}`;
});
