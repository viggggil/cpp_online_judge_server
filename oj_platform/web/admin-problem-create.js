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

function updatePreview() {
  const editor = document.getElementById('statement-editor');
  const preview = document.getElementById('statement-preview');
  if (!editor || !preview) {
    return;
  }
  preview.innerHTML = window.ojMarkdown.markdownToHtml(editor.value);
}

function renderImportResult(result) {
  const container = document.getElementById('import-result');
  container.classList.remove('hidden');
  container.innerHTML = `
    <h2>导入成功</h2>
    <p>题号：${result.problem_id}</p>
    <p>标题：${result.title}</p>
    <p>测试点数量：${result.testcase_count}</p>
    <div class="actions compact-actions">
      <a class="button" href="/problems/${encodeURIComponent(result.problem_id)}">查看题目</a>
      <a class="button auth-btn-secondary" href="/web/admin-problem-edit.html?problem_id=${encodeURIComponent(result.problem_id)}">前往编辑页</a>
    </div>
  `;
}

function renderManualCreateResult(result) {
  const container = document.getElementById('manual-result');
  container.classList.remove('hidden');
  container.innerHTML = `
    <h2>创建成功</h2>
    <p>题号：${result.problem_id}</p>
    <p>标题：${result.title}</p>
    <p>当前题目不包含样例和测试数据，可稍后在编辑页追加测试数据文件。</p>
    <div class="actions compact-actions">
      <a class="button" href="/problems/${encodeURIComponent(result.problem_id)}">查看题目</a>
      <a class="button auth-btn-secondary" href="/web/admin-problem-edit.html?problem_id=${encodeURIComponent(result.problem_id)}">前往编辑页追加测试数据</a>
    </div>
  `;
}

function resetManualForm() {
  document.getElementById('problem-id-input').value = '';
  document.getElementById('problem-title-input').value = '';
  document.getElementById('problem-time-limit-input').value = '1000';
  document.getElementById('problem-memory-limit-input').value = '256';
  document.getElementById('statement-editor').value = '';
  document.getElementById('manual-result').classList.add('hidden');
  updatePreview();
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

function setAdminControlsDisabled(disabled) {
  const ids = [
    'problem-package-input',
    'import-problem-btn',
    'problem-id-input',
    'problem-title-input',
    'problem-time-limit-input',
    'problem-memory-limit-input',
    'statement-editor',
    'create-problem-btn',
    'reset-form-btn',
  ];
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

function validateManualProblemForm() {
  const rawProblemId = document.getElementById('problem-id-input').value.trim();
  const title = document.getElementById('problem-title-input').value.trim();
  const rawTimeLimit = document.getElementById('problem-time-limit-input').value.trim();
  const rawMemoryLimit = document.getElementById('problem-memory-limit-input').value.trim();
  const statementMarkdown = document.getElementById('statement-editor').value.trim();

  const problemId = Number(rawProblemId);
  const timeLimitMs = Number(rawTimeLimit);
  const memoryLimitMb = Number(rawMemoryLimit);

  if (!rawProblemId || !Number.isInteger(problemId) || problemId <= 0) {
    throw new Error('请输入合法的题号');
  }
  if (!title) {
    throw new Error('题目名称不能为空');
  }
  if (title.length > 128) {
    throw new Error('题目名称不能超过 128 个字符');
  }
  if (!rawTimeLimit || !Number.isInteger(timeLimitMs) || timeLimitMs <= 0) {
    throw new Error('请输入合法的时间限制（正整数毫秒）');
  }
  if (timeLimitMs > 60000) {
    throw new Error('时间限制不能超过 60000 毫秒');
  }
  if (!rawMemoryLimit || !Number.isInteger(memoryLimitMb) || memoryLimitMb <= 0) {
    throw new Error('请输入合法的空间限制（正整数 MB）');
  }
  if (memoryLimitMb > 4096) {
    throw new Error('空间限制不能超过 4096 MB');
  }
  if (!statementMarkdown) {
    throw new Error('题面 Markdown 不能为空');
  }

  return {
    problem_id: problemId,
    title,
    time_limit_ms: timeLimitMs,
    memory_limit_mb: memoryLimitMb,
    statement_markdown: statementMarkdown,
  };
}

function ensureAdminAction(currentUser, message) {
  if (!window.ojAuth.requireLogin('进入创建页前请先登录')) {
    return false;
  }
  if (!currentUser?.is_admin) {
    setStatus(message, true);
    alert(message);
    return false;
  }
  return true;
}

async function importProblemPackage(currentUser) {
  if (!ensureAdminAction(currentUser, '权限不足，仅管理员可以导入题目包')) {
    return;
  }

  const input = document.getElementById('problem-package-input');
  const file = input.files?.[0];

  if (!file) {
    setStatus('请先选择题目包 ZIP 文件', true);
    return;
  }

  const fileName = file.name.toLowerCase();
  if (!fileName.endsWith('.zip')) {
    setStatus('当前仅支持导入 .zip 题目包', true);
    return;
  }

  setButtonsDisabled(['import-problem-btn'], true);
  setStatus('正在导入题目包...');

  try {
    const buffer = await file.arrayBuffer();
    const response = await window.ojAuth.authFetch('/api/admin/problems/import', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/zip',
      },
      body: buffer,
    });

    const data = await response.json();
    if (!response.ok) {
      throw new Error(data.error || '导入失败');
    }

    renderImportResult(data);
    setStatus('题目包导入成功');
    input.value = '';
  } catch (error) {
    setStatus(error.message || '导入失败', true);
  } finally {
    setButtonsDisabled(['import-problem-btn'], false);
  }
}

async function createProblem(currentUser) {
  if (!ensureAdminAction(currentUser, '权限不足，仅管理员可以创建题目')) {
    return;
  }

  let payload;
  try {
    payload = validateManualProblemForm();
  } catch (error) {
    setStatus(error.message || '表单校验失败', true);
    return;
  }

  setButtonsDisabled(['create-problem-btn'], true);
  setStatus('正在创建题目...');

  try {
    const response = await window.ojAuth.authFetch('/api/admin/problems', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify(payload),
    });

    const data = await response.json();
    if (!response.ok) {
      throw new Error(data.error || '创建题目失败');
    }

    renderManualCreateResult(data);
    setStatus('空题目创建成功');
  } catch (error) {
    setStatus(error.message || '创建题目失败', true);
  } finally {
    setButtonsDisabled(['create-problem-btn'], false);
  }
}

async function initPage() {
  await window.ojAuth.initAuth();
  resetManualForm();

  document.getElementById('statement-editor').addEventListener('input', updatePreview);

  let currentUser = await fetchCurrentUserOptional();

  document.getElementById('import-problem-btn').addEventListener('click', async () => {
    await importProblemPackage(currentUser);
  });

  document.getElementById('create-problem-btn').addEventListener('click', async () => {
    await createProblem(currentUser);
  });

  document.getElementById('reset-form-btn').addEventListener('click', () => {
    resetManualForm();
    setStatus('');
  });

  window.addEventListener('oj-auth-changed', async () => {
    currentUser = await fetchCurrentUserOptional();
    applyPagePermissionState(currentUser);
  });

  applyPagePermissionState(currentUser);
}

function applyPagePermissionState(currentUser) {
  if (!currentUser) {
    document.getElementById('page-subtitle').textContent = '请先登录；只有管理员可以导入或创建题目';
    setPermissionNotice('普通用户可以看到创建入口，但需要管理员权限才能导入或创建题目。', true);
    setAdminControlsDisabled(true);
    return;
  }

  if (!currentUser.is_admin) {
    document.getElementById('page-subtitle').textContent = `当前用户：${currentUser.username}`;
    setPermissionNotice('当前账号不是管理员，无法导入或创建题目。需要管理员权限后才能继续操作。', true);
    setAdminControlsDisabled(true);
    return;
  }

  document.getElementById('page-subtitle').textContent = `当前管理员：${currentUser.username}`;
  setPermissionNotice('你可以选择导入题目包，或手动创建一个暂不带样例和测试数据的题目。');
  setAdminControlsDisabled(false);
}

initPage().catch((error) => {
  setStatus(error.message || '页面初始化失败', true);
});
