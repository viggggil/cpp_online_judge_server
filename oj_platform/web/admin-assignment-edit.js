function getAssignmentIdFromEditPath() {
  const params = new URLSearchParams(window.location.search);
  const fromQuery = params.get('assignment_id');
  if (fromQuery) return fromQuery;

  const parts = window.location.pathname.split('/').filter(Boolean);
  return parts[2] || '';
}

async function getCurrentUser() {
  const response = await window.ojAuth.authFetch('/api/auth/me');
  if (!response.ok) {
    throw new Error('获取当前用户失败');
  }
  return response.json();
}

function formatTimestamp(timestamp) {
  if (!timestamp) {
    return '';
  }
  const date = new Date(timestamp * 1000);
  const pad = (value) => String(value).padStart(2, '0');
  return [
    date.getFullYear(),
    '-',
    pad(date.getMonth() + 1),
    '-',
    pad(date.getDate()),
    'T',
    pad(date.getHours()),
    ':',
    pad(date.getMinutes()),
  ].join('');
}

function updatePreview() {
  const source = document.getElementById('assignment-description-editor').value;
  document.getElementById('assignment-description-preview').innerHTML =
    window.ojMarkdown.markdownToHtml(source);
}

function setButtonsDisabled(ids, disabled) {
  ids.forEach((id) => {
    const element = document.getElementById(id);
    if (element) {
      element.disabled = disabled;
    }
  });
}

function setStatus(message, isError = false) {
  const node = document.getElementById('status-message');
  node.textContent = message;
  node.classList.toggle('status-bad', isError);
  node.classList.toggle('status-ok', !isError && Boolean(message));
}

function renderAssignmentProblems(assignment) {
  const container = document.getElementById('assignment-problems');
  const problems = assignment.problems || [];
  if (!problems.length) {
    container.textContent = '当前作业还没有题目。';
    return;
  }

  container.innerHTML = '';
  problems.forEach((problem) => {
    const item = document.createElement('div');
    item.className = 'list-item';
    item.innerHTML = `
      <h3>${problem.alias || '-'} - ${problem.problem_id}</h3>
      <p>${problem.title}</p>
      <p>顺序：${problem.display_order}</p>
      <div class="actions compact-actions">
        <a class="button" href="/problems/${problem.problem_id}?assignment_id=${encodeURIComponent(assignment.id)}">查看题目</a>
      </div>
    `;
    container.appendChild(item);
  });
}

async function loadAssignment(assignmentId) {
  const response = await fetch(`/api/assignments/${assignmentId}`);
  const data = await response.json();
  if (!response.ok) {
    throw new Error(data.error || '加载作业详情失败');
  }

  document.getElementById('assignment-title-input').value = data.title || '';
  document.getElementById('assignment-start-input').value = formatTimestamp(data.start_at);
  document.getElementById('assignment-end-input').value = formatTimestamp(data.end_at);
  document.getElementById('assignment-description-editor').value =
    data.description_markdown || '';
  document.getElementById('page-title').textContent = `编辑作业 - ${data.id}`;
  updatePreview();
  renderAssignmentProblems(data);
  return data;
}

async function loadProblemOptions(assignmentId) {
  const [problemResponse, assignmentResponse] = await Promise.all([
    fetch('/api/problems'),
    fetch(`/api/assignments/${assignmentId}`),
  ]);

  if (!problemResponse.ok) {
    throw new Error('加载题库失败');
  }
  if (!assignmentResponse.ok) {
    throw new Error('加载作业题目失败');
  }

  const problemData = await problemResponse.json();
  const assignmentData = await assignmentResponse.json();
  const existingProblemIds = new Set((assignmentData.problems || []).map((item) => String(item.problem_id)));

  const select = document.getElementById('assignment-add-problems-select');
  select.innerHTML = '';

  (problemData.problems || []).forEach((problem) => {
    if (existingProblemIds.has(String(problem.id))) {
      return;
    }

    const option = document.createElement('option');
    option.value = String(problem.id);
    option.textContent = `${problem.id} - ${problem.title}`;
    select.appendChild(option);
  });
}

async function refreshPageData(assignmentId) {
  const assignment = await loadAssignment(assignmentId);
  await loadProblemOptions(assignmentId);
  return assignment;
}

async function updateAssignmentTitle(assignmentId) {
  const title = document.getElementById('assignment-title-input').value.trim();
  if (!title) {
    setStatus('作业名称不能为空', true);
    return;
  }

  setButtonsDisabled(['update-title-btn'], true);
  setStatus('正在保存作业名称...');
  try {
    const response = await window.ojAuth.authFetch(`/api/admin/assignments/${assignmentId}`, {
      method: 'PATCH',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ title }),
    });
    const data = await response.json();
    if (!response.ok) {
      throw new Error(data.error || '修改作业名称失败');
    }
    document.getElementById('page-title').textContent = `编辑作业 - ${assignmentId}`;
    setStatus('作业名称已更新');
  } catch (error) {
    setStatus(error.message || '修改作业名称失败', true);
  } finally {
    setButtonsDisabled(['update-title-btn'], false);
  }
}

async function saveAssignmentDescription(assignmentId) {
  const descriptionMarkdown = document.getElementById('assignment-description-editor').value;

  setButtonsDisabled(['save-description-btn'], true);
  setStatus('正在保存作业简介...');
  try {
    const response = await window.ojAuth.authFetch(`/api/admin/assignments/${assignmentId}`, {
      method: 'PATCH',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ description_markdown: descriptionMarkdown }),
    });
    const data = await response.json();
    if (!response.ok) {
      throw new Error(data.error || '保存作业简介失败');
    }
    setStatus('作业简介已更新');
  } catch (error) {
    setStatus(error.message || '保存作业简介失败', true);
  } finally {
    setButtonsDisabled(['save-description-btn'], false);
  }
}

async function updateAssignmentSchedule(assignmentId) {
  const startValue = document.getElementById('assignment-start-input').value;
  const endValue = document.getElementById('assignment-end-input').value;

  if (!startValue || !endValue) {
    setStatus('开始时间和结束时间都不能为空', true);
    return;
  }

  const startAt = Math.floor(Date.parse(startValue) / 1000);
  const endAt = Math.floor(Date.parse(endValue) / 1000);

  if (!Number.isFinite(startAt) || !Number.isFinite(endAt)) {
    setStatus('请输入合法的起止时间', true);
    return;
  }

  setButtonsDisabled(['update-schedule-btn'], true);
  setStatus('正在保存起止时间...');
  try {
    const response = await window.ojAuth.authFetch(`/api/admin/assignments/${assignmentId}`, {
      method: 'PATCH',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        start_at: startAt,
        end_at: endAt,
      }),
    });
    const data = await response.json();
    if (!response.ok) {
      throw new Error(data.error || '修改起止时间失败');
    }
    document.getElementById('assignment-start-input').value = formatTimestamp(data.start_at);
    document.getElementById('assignment-end-input').value = formatTimestamp(data.end_at);
    setStatus('起止时间已更新');
  } catch (error) {
    setStatus(error.message || '修改起止时间失败', true);
  } finally {
    setButtonsDisabled(['update-schedule-btn'], false);
  }
}

async function addAssignmentProblems(assignmentId) {
  const select = document.getElementById('assignment-add-problems-select');
  const selected = Array.from(select.selectedOptions).map((option) => Number(option.value));

  if (!selected.length) {
    setStatus('请先选择至少一道题再追加', true);
    return;
  }

  setButtonsDisabled(['add-problems-btn'], true);
  setStatus('正在追加题目...');
  try {
    const response = await window.ojAuth.authFetch(`/api/admin/assignments/${assignmentId}/problems`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        problem_ids: selected,
      }),
    });
    const data = await response.json();
    if (!response.ok) {
      throw new Error(data.error || '追加题目失败');
    }

    await refreshPageData(assignmentId);
    setStatus(`已追加 ${data.added_count || selected.length} 道题目`);
  } catch (error) {
    setStatus(error.message || '追加题目失败', true);
  } finally {
    setButtonsDisabled(['add-problems-btn'], false);
  }
}

async function initPage() {
  await window.ojAuth.initAuth();
  if (!window.ojAuth.protectPage()) {
    return;
  }

  const assignmentId = getAssignmentIdFromEditPath();
  const currentUser = await getCurrentUser();
  if (!currentUser.is_admin) {
    setStatus('权限不足，仅管理员可以编辑作业', true);
    alert('权限不足，仅管理员可以编辑作业');
    window.location.href = `/assignments/${assignmentId}`;
    return;
  }

  document.getElementById('page-subtitle').textContent = `当前管理员：${currentUser.username}`;
  document.getElementById('back-to-assignment').href = `/assignments/${assignmentId}`;
  document.getElementById('cancel-btn').addEventListener('click', () => {
    window.location.href = `/assignments/${assignmentId}`;
  });
  document.getElementById('update-title-btn').addEventListener('click', () => updateAssignmentTitle(assignmentId));
  document.getElementById('save-description-btn').addEventListener('click', () => saveAssignmentDescription(assignmentId));
  document.getElementById('update-schedule-btn').addEventListener('click', () => updateAssignmentSchedule(assignmentId));
  document.getElementById('add-problems-btn').addEventListener('click', () => addAssignmentProblems(assignmentId));
  document.getElementById('reload-problems-btn').addEventListener('click', () => {
    loadProblemOptions(assignmentId).catch((error) => {
      setStatus(error.message || '刷新题库失败', true);
    });
  });
  document.getElementById('assignment-description-editor').addEventListener('input', updatePreview);

  await refreshPageData(assignmentId);
}

initPage().catch((error) => {
  setStatus(error.message || '页面初始化失败', true);
});
