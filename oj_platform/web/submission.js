function submissionIdFromPath() {
  const parts = window.location.pathname.split('/').filter(Boolean);
  return parts[1];
}

function renderStatus(status) {
  return ['OK'].includes(status)
    ? `<span class="status-ok">${status}</span>`
    : `<span class="status-bad">${status}</span>`;
}

async function loadSubmission() {
  const id = submissionIdFromPath();
  const response = await fetch(`/api/submissions/${id}`);
  if (!response.ok) throw new Error('提交不存在');
  const data = await response.json();

  document.getElementById('summary').innerHTML = `
提交号: ${data.submission_id}\n
题号: ${data.problem_id}\n
语言: ${data.language}\n
状态: ${data.status}\n
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

loadSubmission().catch(err => {
  document.getElementById('summary').textContent = `加载失败: ${err.message}`;
});