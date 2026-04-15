function submissionIdFromPath() {
  const parts = window.location.pathname.split('/').filter(Boolean);
  return parts[1];
}

function renderStatus(status) {
  return ['OK'].includes(status)
    ? `<span class="status-ok">${status}</span>`
    : `<span class="status-bad">${status}</span>`;
}

function isTerminalStatus(status) {
  return [
    'OK',
    'COMPILE_ERROR',
    'RUNTIME_ERROR',
    'TIME_LIMIT_EXCEEDED',
    'MEMORY_LIMIT_EXCEEDED',
    'WRONG_ANSWER',
    'PRESENTATION_ERROR',
    'SYSTEM_ERROR',
    'NOT_FOUND'
  ].includes(status);
}

function isPositiveStatus(status) {
  return ['OK', 'QUEUED', 'RUNNING'].includes(status);
}

function renderSummaryStatus(status) {
  return isPositiveStatus(status)
    ? `<span class="status-ok">${status}</span>`
    : `<span class="status-bad">${status}</span>`;
}

async function fetchSubmission() {
  if (!window.ojAuth.protectPage()) {
    return null;
  }
  const id = submissionIdFromPath();
  const response = await window.ojAuth.authFetch(`/api/submissions/${id}`);
  if (!response.ok) throw new Error('提交不存在');
  return response.json();
}

function renderSubmission(data) {
  document.getElementById('summary').innerHTML = `
提交号: ${data.submission_id}\n
题号: ${data.problem_id}\n
语言: ${data.language}\n
状态: ${renderSummaryStatus(data.status)}\n
是否通过: ${data.accepted}
  `;

  const judge = data.judge_response || {};
  document.getElementById('compile-info').textContent =
    `compile_success: ${judge.compile_success}\nfinal_status: ${judge.final_status}\ncompile_stdout:\n${judge.compile_stdout || ''}\n\ncompile_stderr:\n${judge.compile_stderr || ''}\n\nsystem_message:\n${judge.system_message || ''}`;

  const container = document.getElementById('case-results');
  container.innerHTML = '';
  (judge.test_case_results || []).forEach((item, index) => {
    const div = document.createElement('div');
    div.className = 'case-item';
    div.innerHTML = `
      <h3>测试点 ${index + 1} - ${renderStatus(item.status)}</h3>
      <pre class="notice">time_used_ms: ${item.time_used_ms}\nmemory_used_kb: ${item.memory_used_kb}\nerror_message: ${item.error_message || ''}\n\nexpected_output:\n${item.expected_output || ''}\n\nactual_output:\n${item.actual_output || ''}</pre>
    `;
    container.appendChild(div);
  });
}

async function loadSubmission() {
  const hint = document.getElementById('polling-hint');
  while (true) {
    const data = await fetchSubmission();
    if (!data) {
      return;
    }
    renderSubmission(data);

    if (isTerminalStatus(data.status)) {
      hint.textContent = '评测已结束。';
      break;
    }

    hint.textContent = data.status === 'QUEUED'
      ? '任务正在队列中等待调度，页面会自动刷新状态。'
      : '任务正在评测中，页面会自动刷新状态。';
    await new Promise(resolve => setTimeout(resolve, 1200));
  }
}

loadSubmission().catch(err => {
  document.getElementById('summary').textContent = `加载失败: ${err.message}`;
});