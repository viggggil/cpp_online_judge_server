function problemIdFromPath() {
  const parts = window.location.pathname.split('/').filter(Boolean);
  return parts[1];
}

function assignmentIdFromQuery() {
  const params = new URLSearchParams(window.location.search);
  return params.get('assignment_id') || '';
}

function formatTimestamp(timestamp) {
  if (!timestamp) {
    return '-';
  }
  return new Date(timestamp * 1000).toLocaleString('zh-CN');
}

function setSubmitDisabled(disabled) {
  const button = document.getElementById('submit-btn');
  if (button) {
    button.disabled = disabled;
  }
}

// 加载提交页顶部的题目信息，帮助用户在提交前确认当前题号与限制。
async function loadProblemInfo() {
  await window.ojAuth.initAuth();
  if (!window.ojAuth.protectPage()) {
    return;
  }
  const id = problemIdFromPath();
  const response = await window.ojAuth.authFetch(`/api/problems/${id}`);
  if (!response.ok) throw new Error('题目不存在');
  const problem = await response.json();
  let message = `${problem.id} - ${problem.title}\n时间限制: ${problem.time_limit_ms} ms | 内存限制: ${problem.memory_limit_mb} MB`;

  const assignmentId = assignmentIdFromQuery();
  if (assignmentId) {
    const assignmentResponse = await fetch(`/api/assignments/${encodeURIComponent(assignmentId)}`);
    const assignment = await assignmentResponse.json();
    if (!assignmentResponse.ok) {
      throw new Error(assignment.error || '作业不存在');
    }

    if (!(assignment.problems || []).some((item) => String(item.problem_id) === String(id))) {
      throw new Error('当前题目不属于该作业');
    }

    const now = Math.floor(Date.now() / 1000);
    const started = now >= assignment.start_at;
    message += `\n所属作业：${assignment.title}`;
    message += `\n作业开始时间：${formatTimestamp(assignment.start_at)} | 结束时间：${formatTimestamp(assignment.end_at)}`;

    if (!started) {
      message += '\n作业尚未开始，当前不能提交。';
      setSubmitDisabled(true);
    } else {
      setSubmitDisabled(false);
    }
  } else {
    setSubmitDisabled(false);
  }

  document.getElementById('problem-info').textContent = message;
  window.ojNav.bindProtectedNavigation();
}

// 收集用户填写的代码和语言，并调用提交接口后跳转到对应的评测详情页。
async function submitCode() {
  await window.ojAuth.initAuth();
  if (!window.ojAuth.requireLogin('提交代码前请先登录')) {
    return;
  }
  const id = problemIdFromPath();
  const assignmentId = assignmentIdFromQuery();
  const sourceCode = document.getElementById('source_code').value;
  const language = document.getElementById('language').value;
  const message = document.getElementById('submit-message');
  message.textContent = '提交中...';

  const response = await window.ojAuth.authFetch('/api/submissions', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      problem_id: id,
      assignment_id: assignmentId || undefined,
      language,
      source_code: sourceCode
    })
  });

  const result = await response.json();
  if (!response.ok) {
    throw new Error(result.error || '提交失败');
  }
  message.textContent = `提交成功，当前状态: ${result.status}`;
  if (result.submission_id) {
    window.location.href = `/submissions/${result.submission_id}`;
  }
}

document.getElementById('submit-btn').addEventListener('click', () => {
  submitCode().catch(err => {
    document.getElementById('submit-message').textContent = `提交失败: ${err.message}`;
  });
});

loadProblemInfo().catch(err => {
  document.getElementById('problem-info').textContent = `加载失败: ${err.message}`;
});
