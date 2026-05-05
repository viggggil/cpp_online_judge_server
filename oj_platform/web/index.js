function setCreateEntryVisible(currentUser) {
  const createLink = document.getElementById('nav-problem-create');
  if (!createLink) {
    return;
  }

  createLink.classList.remove('hidden');
}

// 根据当前登录用户是否为管理员，动态决定首页导航里是否展示出题入口。
async function refreshAdminNavigation() {
  try {
    const currentUser = await window.ojNav.fetchCurrentUserOptional();
    setCreateEntryVisible(currentUser);
  } catch (_) {
    setCreateEntryVisible(null);
  }
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

async function loadProblemStatusesIfAvailable() {
  if (!window.ojAuth.isLoggedIn()) {
    return new Map();
  }

  try {
    const response = await window.ojAuth.authFetch('/api/problems/my-status');
    if (!response.ok) {
      return new Map();
    }
    const data = await response.json();
    return buildStatusMap(data.statuses || []);
  } catch (_) {
    return new Map();
  }
}

// 加载题目列表并绑定需要登录或管理员权限的导航与入口行为。
async function loadProblems() {
  await window.ojAuth.initAuth();
  await refreshAdminNavigation();

  const container = document.getElementById('problem-list');
  container.innerHTML = '加载中...';
  const [response, statusMap] = await Promise.all([
    fetch('/api/problems'),
    loadProblemStatusesIfAvailable(),
  ]);
  const data = await response.json();
  const problems = data.problems || [];
  container.innerHTML = '';

  for (const problem of problems) {
    const status = statusMap.get(String(problem.id));
    const item = document.createElement('div');
    item.className = 'list-item';
    item.innerHTML = `
      <h3>${problem.id} - ${problem.title}</h3>
      <p>难度：${problem.difficulty}</p>
      ${renderProblemStatusMeta(status)}
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
}

window.addEventListener('oj-auth-changed', () => {
  refreshAdminNavigation().catch(() => {
    setCreateEntryVisible(null);
  });
});

window.ojNav.bindProtectedNavigation();

loadProblems().catch(err => {
  document.getElementById('problem-list').textContent = `加载失败: ${err.message}`;
});
