function formatTimestamp(timestamp) {
  if (!timestamp) {
    return '-';
  }
  return new Date(timestamp * 1000).toLocaleString('zh-CN');
}

function renderAssignmentStatus(assignment) {
  const now = Math.floor(Date.now() / 1000);
  if (now < assignment.start_at) {
    return '<span class="status-bad">未开始</span>';
  }
  if (now > assignment.end_at) {
    return '<span class="status-bad">已结束</span>';
  }
  return '<span class="status-ok">进行中</span>';
}

function setCreateAssignmentEntryVisible(currentUser) {
  const link = document.getElementById('create-assignment-link');
  if (!link) {
    return;
  }
  link.classList.toggle('hidden', !currentUser?.is_admin);
}

async function refreshAssignmentToolbar() {
  try {
    const currentUser = await window.ojNav.fetchCurrentUserOptional();
    setCreateAssignmentEntryVisible(currentUser);
    return currentUser;
  } catch (_) {
    setCreateAssignmentEntryVisible(null);
    return null;
  }
}

async function loadAssignments() {
  await window.ojAuth.initAuth();
  window.ojNav.bindProtectedNavigation();
  const currentUser = await refreshAssignmentToolbar();

  const container = document.getElementById('assignment-list');
  container.textContent = '加载中...';

  const response = await fetch('/api/assignments');
  if (!response.ok) {
    throw new Error('加载作业列表失败');
  }

  const data = await response.json();
  const assignments = data.assignments || [];
  if (!assignments.length) {
    container.textContent = '暂无作业。';
    return;
  }

  container.innerHTML = '';
  assignments.forEach((assignment) => {
    const item = document.createElement('div');
    item.className = 'list-item';
    item.innerHTML = `
      <h3>${assignment.title}</h3>
      <p>作业编号：${assignment.id}</p>
      <p>状态：${renderAssignmentStatus(assignment)} ｜ 题目数量：${assignment.problem_count}</p>
      <p>开始时间：${formatTimestamp(assignment.start_at)}</p>
      <p>结束时间：${formatTimestamp(assignment.end_at)}</p>
      <div class="actions compact-actions">
        <a class="button" href="/assignments/${assignment.id}">查看作业</a>
        ${currentUser?.is_admin ? `<a class="button auth-btn-secondary" href="/web/admin-assignment-edit.html?assignment_id=${assignment.id}">编辑作业</a>` : ''}
      </div>
    `;
    container.appendChild(item);
  });
}

window.addEventListener('oj-auth-changed', () => {
  refreshAssignmentToolbar().catch(() => {
    setCreateAssignmentEntryVisible(null);
  });
});

loadAssignments().catch((error) => {
  document.getElementById('assignment-list').textContent = `加载失败: ${error.message}`;
});
