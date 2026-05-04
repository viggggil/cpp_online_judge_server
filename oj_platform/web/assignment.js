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

function renderProblemList(assignment, problems) {
  const container = document.getElementById('assignment-problems');
  if (!problems.length) {
    container.textContent = '当前作业还没有题目。';
    return;
  }

  const started = isAssignmentStarted(assignment.start_at);
  container.innerHTML = '';
  problems.forEach((problem) => {
    const item = document.createElement('div');
    const problemHref = `/problems/${problem.problem_id}?assignment_id=${encodeURIComponent(assignment.id)}`;
    const submitHref = `/submit/${problem.problem_id}?assignment_id=${encodeURIComponent(assignment.id)}`;
    item.className = 'list-item';
    item.innerHTML = `
      <h3>${problem.alias || '-'} - ${problem.problem_id}</h3>
      <p>${problem.title}</p>
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

async function loadAssignment() {
  await window.ojAuth.initAuth();
  window.ojNav.bindProtectedNavigation();

  const assignmentId = assignmentIdFromPath();
  const [response, currentUser] = await Promise.all([
    fetch(`/api/assignments/${assignmentId}`),
    fetchCurrentUserOptional().catch(() => null),
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
  renderProblemList(data, data.problems || []);
}

loadAssignment().catch((error) => {
  document.getElementById('assignment-meta').textContent = `加载失败: ${error.message}`;
  document.getElementById('assignment-description').innerHTML = '';
  document.getElementById('assignment-problems').innerHTML = '';
});
