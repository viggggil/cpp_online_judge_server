function assignmentIdFromPath() {
  const parts = window.location.pathname.split('/').filter(Boolean);
  return parts[1];
}

async function fetchCurrentUserOptional() {
  return window.ojNav.fetchCurrentUserOptional();
}

function formatTimestamp(timestamp) {
  if (!timestamp) {
    return '-';
  }
  return new Date(timestamp * 1000).toLocaleString('zh-CN');
}

function normalizeStatusText(status) {
  const value = String(status || 'NONE').toUpperCase();
  if (value === 'NONE') return '未提交';
  if (value === 'ACCEPTED' || value === 'OK') return 'Accepted';
  if (value === 'WRONG_ANSWER') return 'Wrong Answer';
  if (value === 'COMPILE_ERROR') return 'Compile Error';
  if (value === 'RUNTIME_ERROR') return 'Runtime Error';
  if (value === 'TIME_LIMIT_EXCEEDED') return 'Time Limit Exceeded';
  if (value === 'MEMORY_LIMIT_EXCEEDED') return 'Memory Limit Exceeded';
  if (value === 'OUTPUT_LIMIT_EXCEEDED') return 'Output Limit Exceeded';
  if (value === 'PRESENTATION_ERROR') return 'Presentation Error';
  if (value === 'SYSTEM_ERROR') return 'System Error';
  if (value === 'RUNNING') return 'Running';
  if (value === 'QUEUED') return 'Queued';
  return value.replaceAll('_', ' ');
}

function buildStatusMap(statuses) {
  const map = new Map();
  (statuses || []).forEach((status) => {
    map.set(String(status.problem_id), status);
  });
  return map;
}

function renderProblemStatusMeta(status) {
  if (!status || !status.has_submission) {
    return `
      <p class="problem-status-line">
        状态：<span class="problem-status-text problem-status-none">未提交</span>
      </p>
      <p class="problem-status-time">最后提交时间：-</p>
    `;
  }

  const cssClass = status.accepted
    ? 'problem-status-accepted'
    : 'problem-status-failed';

  return `
    <p class="problem-status-line">
      状态：<span class="problem-status-text ${cssClass}">${normalizeStatusText(status.status)}</span>
    </p>
    <p class="problem-status-time">最后提交时间：${formatTimestamp(status.last_submitted_at)}</p>
  `;
}

function bindEditButton(assignmentId, currentUser) {
  const editButton = document.getElementById('edit-assignment-btn');
  if (!editButton) {
    return;
  }

  if (!currentUser?.is_admin) {
    editButton.classList.add('hidden');
    return;
  }

  editButton.classList.remove('hidden');
  editButton.addEventListener('click', () => {
    window.location.href = `/web/admin-assignment-edit.html?assignment_id=${encodeURIComponent(assignmentId)}`;
  });
}

function isAssignmentStarted(startAt) {
  return Math.floor(Date.now() / 1000) >= startAt;
}

function renderProblemList(assignment, problems, statusMap) {
  const container = document.getElementById('assignment-problems');
  if (!problems.length) {
    container.textContent = '当前作业还没有题目。';
    return;
  }

  const started = isAssignmentStarted(assignment.start_at);
  container.innerHTML = '';
  problems.forEach((problem) => {
    const status = statusMap.get(String(problem.problem_id));
    const item = document.createElement('div');
    const problemHref = `/problems/${problem.problem_id}?assignment_id=${encodeURIComponent(assignment.id)}`;
    const submitHref = `/submit/${problem.problem_id}?assignment_id=${encodeURIComponent(assignment.id)}`;
    item.className = 'list-item';
    item.innerHTML = `
      <h3>${problem.alias || '-'} - ${problem.problem_id}</h3>
      <p>${problem.title}</p>
      ${renderProblemStatusMeta(status)}
      <div class="actions compact-actions">
        <a class="button problem-link" href="${problemHref}">查看题目</a>
        <a class="button auth-btn-secondary submit-link ${started ? '' : 'button-disabled'}" href="${submitHref}" aria-disabled="${started ? 'false' : 'true'}">去提交</a>
      </div>
    `;

    item.querySelector('.problem-link')?.addEventListener('click', (event) => {
      if (!window.ojAuth.requireLogin('查看题目前请先登录')) {
        event.preventDefault();
      }
    });

    item.querySelector('.submit-link')?.addEventListener('click', (event) => {
      if (!window.ojAuth.requireLogin('提交代码前请先登录')) {
        event.preventDefault();
        return;
      }
      if (!started) {
        event.preventDefault();
        alert('作业尚未开始，当前不能提交');
      }
    });

    container.appendChild(item);
  });
}

function renderLeaderboardEntryLink(assignmentId) {
  const toolbar = document.getElementById('assignment-toolbar');
  if (!toolbar) {
    return;
  }

  toolbar.innerHTML = `
    <a class="button auth-btn-secondary" href="/assignments/${encodeURIComponent(assignmentId)}/leaderboard">查看排行榜</a>
  `;
}

async function loadProblemStatusesIfAvailable(assignmentId) {
  if (!window.ojAuth.isLoggedIn()) {
    return new Map();
  }

  try {
    const response = await window.ojAuth.authFetch(
      `/api/problems/my-status?assignment_id=${encodeURIComponent(assignmentId)}`
    );
    if (!response.ok) {
      return new Map();
    }
    const data = await response.json();
    return buildStatusMap(data.statuses || []);
  } catch (_) {
    return new Map();
  }
}

async function loadAssignment() {
  await window.ojAuth.initAuth();
  window.ojNav.bindProtectedNavigation();

  const assignmentId = assignmentIdFromPath();
  const [response, currentUser, statusMap] = await Promise.all([
    fetch(`/api/assignments/${assignmentId}`),
    fetchCurrentUserOptional().catch(() => null),
    loadProblemStatusesIfAvailable(assignmentId),
  ]);
  const data = await response.json();
  if (!response.ok) {
    throw new Error(data.error || '加载作业详情失败');
  }

  document.getElementById('assignment-title').textContent = data.title || `作业 ${assignmentId}`;
  document.getElementById('assignment-meta').textContent =
    `作业编号：${data.id}\n开始时间：${formatTimestamp(data.start_at)}\n结束时间：${formatTimestamp(data.end_at)}\n题目数量：${(data.problems || []).length}`;
  document.getElementById('assignment-description').innerHTML =
    window.ojMarkdown.markdownToHtml(data.description_markdown || '');

  bindEditButton(assignmentId, currentUser);
  renderLeaderboardEntryLink(assignmentId);
  renderProblemList(data, data.problems || [], statusMap);
}

loadAssignment().catch((error) => {
  document.getElementById('assignment-meta').textContent = `加载失败: ${error.message}`;
  document.getElementById('assignment-description').innerHTML = '';
  document.getElementById('assignment-problems').innerHTML = '';
});
