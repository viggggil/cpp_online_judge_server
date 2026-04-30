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

async function loadProblemMeta(problemId) {
  const response = await window.ojAuth.authFetch(`/api/problems/${problemId}`);
  const data = await response.json();
  if (!response.ok) {
    throw new Error(data.error || '加载题目信息失败');
  }

  document.getElementById('problem-id-input').value = data.id || problemId;
  document.getElementById('problem-title-input').value = data.title || '';
  document.getElementById('page-title').textContent = `编辑题面 - ${data.id}`;
}

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

    setStatus('题目已删除，即将返回题目列表');
    setTimeout(() => {
      window.location.href = '/';
    }, 800);
  } catch (error) {
    setStatus(error.message || '删除题目失败', true);
    setButtonsDisabled(['delete-problem-btn'], false);
  }
}

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
  document.getElementById('delete-problem-btn').addEventListener('click', () => deleteProblem(problemId));
  document.getElementById('statement-editor').addEventListener('input', updatePreview);

  await loadProblemMeta(problemId);
  await loadStatement(problemId);
}

initPage().catch((error) => {
  setStatus(error.message || '页面初始化失败', true);
});
