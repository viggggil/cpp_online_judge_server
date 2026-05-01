function setStatus(message, isError = false) {
  const node = document.getElementById('status-message');
  node.textContent = message;
  node.classList.toggle('status-bad', isError);
  node.classList.toggle('status-ok', !isError && Boolean(message));
}

function setImportButtonDisabled(disabled) {
  const button = document.getElementById('import-problem-btn');
  if (button) {
    button.disabled = disabled;
  }
}

async function getCurrentUser() {
  const response = await window.ojAuth.authFetch('/api/auth/me');
  if (!response.ok) {
    throw new Error('获取当前用户失败');
  }
  return response.json();
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
      <a class="button auth-btn-secondary" href="/web/admin-problem-edit.html?problem_id=${encodeURIComponent(result.problem_id)}">编辑题面</a>
    </div>
  `;
}

// 读取管理员选择的 ZIP 题目包并调用后台导入接口，随后刷新页面提示与结果区域。
async function importProblemPackage() {
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

  setImportButtonDisabled(true);
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
    setImportButtonDisabled(false);
  }
}

// 初始化创建题目页面，校验管理员身份并绑定导入按钮事件。
async function initPage() {
  await window.ojAuth.initAuth();
  if (!window.ojAuth.protectPage()) {
    return;
  }

  const currentUser = await getCurrentUser();
  if (!currentUser.is_admin) {
    setStatus('权限不足，仅管理员可以进入题目创建页面', true);
    alert('权限不足，仅管理员可以进入题目创建页面');
    window.location.href = '/';
    return;
  }

  document.getElementById('page-subtitle').textContent = `当前管理员：${currentUser.username}`;
  document.getElementById('import-problem-btn').addEventListener('click', importProblemPackage);
}

initPage().catch((error) => {
  setStatus(error.message || '页面初始化失败', true);
});
