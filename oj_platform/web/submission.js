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

function escapeHtml(text) {
  return String(text ?? '')
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#39;');
}

function truncateText(text, maxLength = 160) {
  const normalized = String(text ?? '');
  if (normalized.length <= maxLength) {
    return normalized || '(空)';
  }
  return `${normalized.slice(0, maxLength)}...`;
}

function renderCaseDetailRow(label, value) {
  return `
    <div class="case-detail-block">
      <div class="case-detail-label">${label}</div>
      <pre class="case-detail-content">${escapeHtml(truncateText(value))}</pre>
    </div>
  `;
}

async function fetchSubmission() {
  await window.ojAuth.initAuth();
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
    const detailId = `case-detail-${index}`;
    div.innerHTML = `
      <div class="case-summary-row">
        <h3>测试点 ${index + 1} - ${renderStatus(item.status)}</h3>
        <div class="case-summary-metrics">
          <span>耗时: ${item.time_used_ms ?? 0} ms</span>
          <span>内存: ${item.memory_used_kb ?? 0} KB</span>
          <button class="case-toggle" type="button" data-target="${detailId}" aria-expanded="false">▼</button>
        </div>
      </div>
      <div id="${detailId}" class="case-detail hidden">
        ${renderCaseDetailRow('输入数据', item.input || '')}
        ${renderCaseDetailRow('期望输出', item.expected_output || '')}
        ${renderCaseDetailRow('实际输出', item.actual_output || '')}
        ${item.error_message ? renderCaseDetailRow('错误信息', item.error_message) : ''}
      </div>
    `;

    const toggle = div.querySelector('.case-toggle');
    const detail = div.querySelector('.case-detail');
    toggle?.addEventListener('click', () => {
      const hidden = detail.classList.toggle('hidden');
      toggle.textContent = hidden ? '▼' : '▲';
      toggle.setAttribute('aria-expanded', hidden ? 'false' : 'true');
    });

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