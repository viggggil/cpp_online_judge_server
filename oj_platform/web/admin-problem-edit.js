function getProblemIdFromEditPath() {
  const params = new URLSearchParams(window.location.search);
  const fromQuery = params.get('problem_id');
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

function updatePreview() {
  const source = document.getElementById('statement-editor').value;
  document.getElementById('statement-preview').innerHTML = window.ojMarkdown.markdownToHtml(source);
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

function setAppendTestcaseStatus(message, isError = false) {
  const node = document.getElementById('append-testcase-message');
  if (!node) {
    return;
  }
  node.textContent = message;
  node.classList.toggle('status-bad', isError);
  node.classList.toggle('status-ok', !isError && Boolean(message));
}

// 从后台加载题面 Markdown，并同步刷新编辑器与预览区域。
async function loadStatement(problemId) {
  const response = await window.ojAuth.authFetch(`/api/admin/problems/${problemId}/statement`);
  const data = await response.json();
  if (!response.ok) {
    throw new Error(data.error || '加载题面失败');
  }
  const statement = data.statement_markdown || '';
  document.getElementById('statement-editor').value = statement;
  updatePreview();
}

// 加载题目的基础信息，回填题号、标题和页面标题。
async function loadProblemMeta(problemId) {
  const response = await window.ojAuth.authFetch(`/api/problems/${problemId}`);
  const data = await response.json();
  if (!response.ok) {
    throw new Error(data.error || '加载题目信息失败');
  }

  document.getElementById('problem-id-input').value = data.id || problemId;
  document.getElementById('problem-title-input').value = data.title || '';
  document.getElementById('problem-time-limit-input').value = data.time_limit_ms || '';
  document.getElementById('problem-memory-limit-input').value = data.memory_limit_mb || '';
  document.getElementById('page-title').textContent = `编辑题面 - ${data.id}`;
}

// 把当前编辑器中的题面内容提交到后台并反馈保存状态。
async function saveStatement(problemId) {
  const saveButton = document.getElementById('save-btn');
  saveButton.disabled = true;
  setStatus('保存中...');

  try {
    const response = await window.ojAuth.authFetch(`/api/admin/problems/${problemId}/statement`, {
      method: 'PUT',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({
        statement_markdown: document.getElementById('statement-editor').value,
      }),
    });

    const data = await response.json();
    if (!response.ok) {
      throw new Error(data.error || '保存失败');
    }

    setStatus('保存成功');
  } catch (error) {
    setStatus(error.message || '保存失败', true);
  } finally {
    saveButton.disabled = false;
  }
}

// 更新题目标题，并在成功后同步页面上的展示文案。
async function updateProblemTitle(problemId) {
  const title = document.getElementById('problem-title-input').value.trim();
  if (!title) {
    setStatus('题目名称不能为空', true);
    return;
  }

  setButtonsDisabled(['update-title-btn'], true);
  setStatus('正在保存题目名称...');
  try {
    const response = await window.ojAuth.authFetch(`/api/admin/problems/${problemId}/title`, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ title }),
    });
    const data = await response.json();
    if (!response.ok) {
      throw new Error(data.error || '修改题目名称失败');
    }
    setStatus('题目名称已更新');
    document.getElementById('page-title').textContent = `编辑题面 - ${problemId}`;
  } catch (error) {
    setStatus(error.message || '修改题目名称失败', true);
  } finally {
    setButtonsDisabled(['update-title-btn'], false);
  }
}

// 更新题目的时间限制和空间限制，并在成功后同步表单中的当前值。
async function updateProblemLimits(problemId) {
  const rawTimeLimit = document.getElementById('problem-time-limit-input').value.trim();
  const rawMemoryLimit = document.getElementById('problem-memory-limit-input').value.trim();
  const timeLimitMs = Number(rawTimeLimit);
  const memoryLimitMb = Number(rawMemoryLimit);

  if (!rawTimeLimit || !Number.isInteger(timeLimitMs) || timeLimitMs <= 0) {
    setStatus('请输入合法的时间限制（正整数毫秒）', true);
    return;
  }

  if (!rawMemoryLimit || !Number.isInteger(memoryLimitMb) || memoryLimitMb <= 0) {
    setStatus('请输入合法的空间限制（正整数 MB）', true);
    return;
  }

  setButtonsDisabled(['update-limits-btn'], true);
  setStatus('正在保存时空限制...');
  try {
    const response = await window.ojAuth.authFetch(`/api/admin/problems/${problemId}/limits`, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        time_limit_ms: timeLimitMs,
        memory_limit_mb: memoryLimitMb,
      }),
    });
    const data = await response.json();
    if (!response.ok) {
      throw new Error(data.error || '修改时空限制失败');
    }

    document.getElementById('problem-time-limit-input').value = data.time_limit_ms;
    document.getElementById('problem-memory-limit-input').value = data.memory_limit_mb;
    setStatus('时空限制已更新');
  } catch (error) {
    setStatus(error.message || '修改时空限制失败', true);
  } finally {
    setButtonsDisabled(['update-limits-btn'], false);
  }
}

// 修改题号并在成功后跳转到新的编辑地址，保持页面状态与题号一致。
async function updateProblemId(problemId) {
  const rawValue = document.getElementById('problem-id-input').value.trim();
  const newProblemId = Number(rawValue);
  if (!rawValue || !Number.isInteger(newProblemId) || newProblemId <= 0) {
    setStatus('请输入合法的新题号', true);
    return problemId;
  }

  if (newProblemId === Number(problemId)) {
    setStatus('题号未变化');
    return problemId;
  }

  setButtonsDisabled(['update-id-btn'], true);
  setStatus('正在修改题号...');
  try {
    const response = await window.ojAuth.authFetch(`/api/admin/problems/${problemId}/id`, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ new_problem_id: newProblemId }),
    });
    const data = await response.json();
    if (!response.ok) {
      throw new Error(data.error || '修改题号失败');
    }

    setStatus(`题号已更新：${problemId} -> ${newProblemId}`);
    window.location.href = `/web/admin-problem-edit.html?problem_id=${encodeURIComponent(newProblemId)}`;
    return String(newProblemId);
  } catch (error) {
    setStatus(error.message || '修改题号失败', true);
    return problemId;
  } finally {
    setButtonsDisabled(['update-id-btn'], false);
  }
}

// 删除题目并在操作完成后把管理员带回题库页，避免停留在失效页面上。
async function deleteProblem(problemId) {
  const confirmed = window.confirm(`确定要删除题目 ${problemId} 吗？此操作不可恢复。`);
  if (!confirmed) {
    return;
  }

  setButtonsDisabled(['delete-problem-btn'], true);
  setStatus('正在删除题目...');
  try {
    const response = await window.ojAuth.authFetch(`/api/admin/problems/${problemId}`, {
      method: 'DELETE',
    });
    const data = await response.json();
    if (!response.ok) {
      throw new Error(data.error || '删除题目失败');
    }

    setStatus('题目已删除，即将返回题库页');
    setTimeout(() => {
      window.location.href = '/problems';
    }, 800);
  } catch (error) {
    setStatus(error.message || '删除题目失败', true);
    setButtonsDisabled(['delete-problem-btn'], false);
  }
}

async function appendTestcaseFile(problemId) {
  const input = document.getElementById('testcase-file-input');
  const file = input.files?.[0];

  if (!file) {
    setAppendTestcaseStatus('请先选择一个测试数据文件', true);
    return;
  }

  const normalizedName = file.name.toLowerCase();
  if (!/^\d+\.(in|out)$/.test(normalizedName)) {
    setAppendTestcaseStatus('文件名必须是数字.in 或 数字.out，例如 12.in', true);
    return;
  }

  const formData = new FormData();
  formData.append('file', file, file.name);

  setButtonsDisabled(['append-testcase-btn'], true);
  setAppendTestcaseStatus('正在上传测试数据文件...');

  try {
    const response = await window.ojAuth.authFetch(`/api/admin/problems/${problemId}/testcase-file`, {
      method: 'POST',
      body: formData,
    });
    const data = await response.json();
    if (!response.ok) {
      throw new Error(data.error || '上传测试数据文件失败');
    }

    const pairedMessage = data.paired
      ? `测试点 ${data.case_no} 已补齐并写入系统`
      : `已暂存 ${data.filename}，等待补齐另一侧文件`;
    setAppendTestcaseStatus(data.message ? `${pairedMessage}\n${data.message}` : pairedMessage);
    input.value = '';
  } catch (error) {
    setAppendTestcaseStatus(error.message || '上传测试数据文件失败', true);
  } finally {
    setButtonsDisabled(['append-testcase-btn'], false);
  }
}

// 初始化题面编辑页，校验管理员身份并串起元信息、题面和交互事件的加载流程。
async function initPage() {
  await window.ojAuth.initAuth();
  if (!window.ojAuth.protectPage()) {
    return;
  }

  const problemId = getProblemIdFromEditPath();
  const currentUser = await getCurrentUser();
  if (!currentUser.is_admin) {
    setStatus('权限不足，仅管理员可以编辑题面', true);
    alert('权限不足，仅管理员可以编辑题面');
    window.location.href = `/problems/${problemId}`;
    return;
  }

  document.getElementById('page-subtitle').textContent = `当前管理员：${currentUser.username}`;
  document.getElementById('back-to-problem').href = `/problems/${problemId}`;
  document.getElementById('cancel-btn').addEventListener('click', () => {
    window.location.href = `/problems/${problemId}`;
  });
  document.getElementById('save-btn').addEventListener('click', () => saveStatement(problemId));
  document.getElementById('update-title-btn').addEventListener('click', () => updateProblemTitle(problemId));
  document.getElementById('update-id-btn').addEventListener('click', () => updateProblemId(problemId));
  document.getElementById('update-limits-btn').addEventListener('click', () => updateProblemLimits(problemId));
  document.getElementById('append-testcase-btn').addEventListener('click', () => appendTestcaseFile(problemId));
  document.getElementById('delete-problem-btn').addEventListener('click', () => deleteProblem(problemId));
  document.getElementById('statement-editor').addEventListener('input', updatePreview);

  await loadProblemMeta(problemId);
  await loadStatement(problemId);
}

initPage().catch((error) => {
  setStatus(error.message || '页面初始化失败', true);
});
