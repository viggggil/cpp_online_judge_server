let problemCache = [];

function setStatus(message, isError = false) {
  const node = document.getElementById('status-message');
  node.textContent = message;
  node.classList.toggle('status-bad', isError);
  node.classList.toggle('status-ok', !isError && Boolean(message));
}

function setButtonsDisabled(ids, disabled) {
  ids.forEach((id) => {
    const element = document.getElementById(id);
    if (element) {
      element.disabled = disabled;
    }
  });
}

function setPermissionNotice(message, isError = false) {
  const node = document.getElementById('permission-notice');
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

function setAdminControlsDisabled(disabled) {
  const ids = [
    'assignment-title-input',
    'assignment-start-input',
    'assignment-end-input',
    'assignment-description-editor',
    'reload-problems-btn',
    'create-assignment-btn',
    'reset-assignment-form-btn',
  ];

  ids.forEach((id) => {
    const element = document.getElementById(id);
    if (element) {
      element.disabled = disabled;
    }
  });

  document.querySelectorAll('.assignment-problem-checkbox, .assignment-problem-alias').forEach((element) => {
    element.disabled = disabled;
  });
}

function updatePreview() {
  const source = document.getElementById('assignment-description-editor').value;
  document.getElementById('assignment-description-preview').innerHTML = window.ojMarkdown.markdownToHtml(source);
}

function defaultAliasForIndex(index) {
  if (index < 26) {
    return String.fromCharCode('A'.charCodeAt(0) + index);
  }
  return `P${index + 1}`;
}

function resetForm() {
  document.getElementById('assignment-title-input').value = '';
  document.getElementById('assignment-description-editor').value = '';

  const now = new Date();
  const start = new Date(now.getTime() + 60 * 60 * 1000);
  const end = new Date(now.getTime() + 7 * 24 * 60 * 60 * 1000);
  document.getElementById('assignment-start-input').value = toDateTimeLocalValue(start);
  document.getElementById('assignment-end-input').value = toDateTimeLocalValue(end);

  document.querySelectorAll('.assignment-problem-checkbox').forEach((checkbox) => {
    checkbox.checked = false;
  });
  document.querySelectorAll('.assignment-problem-alias').forEach((input, index) => {
    input.value = defaultAliasForIndex(index);
  });

  document.getElementById('create-result').classList.add('hidden');
  updatePreview();
}

function toDateTimeLocalValue(date) {
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

function toUnixTimestampSeconds(value) {
  const timestamp = Date.parse(value);
  if (!Number.isFinite(timestamp)) {
    return NaN;
  }
  return Math.floor(timestamp / 1000);
}

function renderProblemPicker(problems) {
  const container = document.getElementById('assignment-problem-list');
  if (!problems.length) {
    container.textContent = '题库为空，暂时无法创建作业。';
    return;
  }

  container.innerHTML = '';
  problems.forEach((problem, index) => {
    const item = document.createElement('div');
    item.className = 'list-item assignment-problem-item';
    item.innerHTML = `
      <label class="assignment-problem-row">
        <span class="assignment-problem-main">
          <input class="assignment-problem-checkbox" type="checkbox" data-problem-id="${problem.id}" />
          <span>${problem.id} - ${problem.title}</span>
        </span>
        <span class="assignment-problem-alias-group">
          <span>标号</span>
          <input class="input assignment-problem-alias" type="text" maxlength="32" value="${defaultAliasForIndex(index)}" data-problem-id="${problem.id}" />
        </span>
      </label>
    `;
    container.appendChild(item);
  });
}

async function loadProblems() {
  const container = document.getElementById('assignment-problem-list');
  container.textContent = '题库加载中...';

  const response = await fetch('/api/problems');
  if (!response.ok) {
    throw new Error('加载题库失败');
  }

  const data = await response.json();
  problemCache = data.problems || [];
  renderProblemPicker(problemCache);
}

function validateForm() {
  const title = document.getElementById('assignment-title-input').value.trim();
  const descriptionMarkdown = document.getElementById('assignment-description-editor').value.trim();
  const startAt = toUnixTimestampSeconds(document.getElementById('assignment-start-input').value);
  const endAt = toUnixTimestampSeconds(document.getElementById('assignment-end-input').value);

  if (!title) {
    throw new Error('作业名称不能为空');
  }
  if (title.length > 255) {
    throw new Error('作业名称不能超过 255 个字符');
  }
  if (!Number.isInteger(startAt) || startAt <= 0) {
    throw new Error('请输入合法的开始时间');
  }
  if (!Number.isInteger(endAt) || endAt <= startAt) {
    throw new Error('结束时间必须晚于开始时间');
  }

  const selectedProblems = [];
  document.querySelectorAll('.assignment-problem-checkbox:checked').forEach((checkbox) => {
    const problemId = Number(checkbox.dataset.problemId);
    const aliasInput = document.querySelector(`.assignment-problem-alias[data-problem-id="${checkbox.dataset.problemId}"]`);
    const alias = aliasInput ? aliasInput.value.trim() : '';
    selectedProblems.push({
      problem_id: problemId,
      alias,
    });
  });

  if (!selectedProblems.length) {
    throw new Error('请至少选择一道题加入作业');
  }

  return {
    title,
    description_markdown: descriptionMarkdown,
    start_at: startAt,
    end_at: endAt,
    problems: selectedProblems,
  };
}

function renderCreateResult(result) {
  const container = document.getElementById('create-result');
  container.classList.remove('hidden');
  container.innerHTML = `
    <h2>创建成功</h2>
    <p>作业编号：${result.assignment_id}</p>
    <p>作业名称：${result.title}</p>
    <p>题目数量：${result.problem_count}</p>
    <div class="actions compact-actions">
      <a class="button" href="/assignments/${encodeURIComponent(result.assignment_id)}">查看作业</a>
      <a class="button auth-btn-secondary" href="/assignments">返回作业列表</a>
    </div>
  `;
}

function applyPagePermissionState(currentUser) {
  if (!currentUser) {
    document.getElementById('page-subtitle').textContent = '请先登录；只有管理员可以创建作业';
    setPermissionNotice('普通用户可以看到创建入口，但创建作业需要管理员权限。', true);
    setAdminControlsDisabled(true);
    return;
  }

  if (!currentUser.is_admin) {
    document.getElementById('page-subtitle').textContent = `当前用户：${currentUser.username}`;
    setPermissionNotice('当前账号不是管理员，无法创建作业。需要管理员权限后才能继续操作。', true);
    setAdminControlsDisabled(true);
    return;
  }

  document.getElementById('page-subtitle').textContent = `当前管理员：${currentUser.username}`;
  setPermissionNotice('填写作业名称、起止时间和简介后，从题库中选择题目即可创建作业。');
  setAdminControlsDisabled(false);
}

async function createAssignment(currentUser) {
  if (!window.ojAuth.requireLogin('进入创建页前请先登录')) {
    return;
  }

  if (!currentUser?.is_admin) {
    const message = '权限不足，仅管理员可以创建作业';
    setStatus(message, true);
    alert(message);
    return;
  }

  let payload;
  try {
    payload = validateForm();
  } catch (error) {
    setStatus(error.message || '表单校验失败', true);
    return;
  }

  setButtonsDisabled(['create-assignment-btn'], true);
  setStatus('正在创建作业...');

  try {
    const response = await window.ojAuth.authFetch('/api/admin/assignments', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify(payload),
    });
    const data = await response.json();
    if (!response.ok) {
      throw new Error(data.error || '创建作业失败');
    }

    renderCreateResult(data);
    setStatus('作业创建成功');
  } catch (error) {
    setStatus(error.message || '创建作业失败', true);
  } finally {
    setButtonsDisabled(['create-assignment-btn'], false);
  }
}

async function initPage() {
  await window.ojAuth.initAuth();
  window.ojNav.bindProtectedNavigation();

  document.getElementById('assignment-description-editor').addEventListener('input', updatePreview);
  document.getElementById('reload-problems-btn').addEventListener('click', () => {
    loadProblems().catch((error) => {
      setStatus(error.message || '刷新题库失败', true);
    });
  });

  let currentUser = await window.ojNav.fetchCurrentUserOptional();

  document.getElementById('create-assignment-btn').addEventListener('click', () => {
    createAssignment(currentUser);
  });

  document.getElementById('reset-assignment-form-btn').addEventListener('click', () => {
    resetForm();
    setStatus('');
  });

  window.addEventListener('oj-auth-changed', async () => {
    currentUser = await window.ojNav.fetchCurrentUserOptional();
    applyPagePermissionState(currentUser);
  });

  await loadProblems();
  resetForm();
  applyPagePermissionState(currentUser);
}

initPage().catch((error) => {
  setStatus(error.message || '页面初始化失败', true);
});
