function agentEscapeHtml(value) {
  return String(value ?? '')
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;');
}

function agentFormatTimestamp(timestamp) {
  if (!timestamp) {
    return '-';
  }
  return new Date(timestamp * 1000).toLocaleString('zh-CN');
}

function agentRenderList(title, items) {
  const values = Array.isArray(items) ? items : [];
  if (!values.length) {
    return '';
  }
  return `
    <section class="agent-result-section">
      <h3>${agentEscapeHtml(title)}</h3>
      <ul>
        ${values.map((item) => `<li>${agentEscapeHtml(item)}</li>`).join('')}
      </ul>
    </section>
  `;
}

function setAgentStatus(message, isError = false) {
  const node = document.getElementById('agent-status');
  node.textContent = message || '';
  node.classList.toggle('status-bad', isError);
  node.classList.toggle('status-ok', !isError && Boolean(message));
}

function selectedSubmission() {
  const select = document.getElementById('agent-submission-select');
  const option = select.options[select.selectedIndex];
  if (!option?.value) {
    return null;
  }
  return {
    submission_id: option.value,
    problem_id: Number(option.dataset.problemId || '0'),
  };
}

function renderSubmissions(submissions) {
  const select = document.getElementById('agent-submission-select');
  select.innerHTML = '';

  if (!submissions.length) {
    const option = document.createElement('option');
    option.value = '';
    option.textContent = '暂无提交记录';
    select.appendChild(option);
    select.disabled = true;
    document.getElementById('agent-submit-btn').disabled = true;
    return;
  }

  for (const item of submissions) {
    const option = document.createElement('option');
    option.value = item.submission_id;
    option.dataset.problemId = item.problem_id;
    option.textContent = `${item.submission_id} ｜ 题号 ${item.problem_id} ｜ ${item.final_status || item.status} ｜ ${agentFormatTimestamp(item.created_at)}`;
    select.appendChild(option);
  }
}

function renderConversations(conversations) {
  const select = document.getElementById('agent-conversation-select');
  select.innerHTML = '';

  if (!conversations.length) {
    const option = document.createElement('option');
    option.value = '';
    option.textContent = '暂无历史会话';
    select.appendChild(option);
    select.disabled = true;
    document.getElementById('agent-load-conversation-btn').disabled = true;
    return;
  }

  select.disabled = false;
  document.getElementById('agent-load-conversation-btn').disabled = false;

  for (const item of conversations) {
    const option = document.createElement('option');
    option.value = item.conversation_id;
    option.dataset.problemId = item.problem_id;
    option.dataset.submissionId = item.submission_id || '';
    option.textContent =
      `${item.title || item.conversation_id} ｜ 题号 ${item.problem_id} ｜ ${item.round_count || 0} 轮 ｜ ${agentFormatTimestamp(item.updated_at)}`;
    select.appendChild(option);
  }
}

async function loadAgentSubmissions() {
  const response = await window.ojAuth.authFetch('/api/submissions');
  const data = await response.json();
  if (!response.ok) {
    throw new Error(data.error || '加载提交列表失败');
  }

  renderSubmissions(data.submissions || []);
}

async function loadAgentConversations() {
  const response = await window.ojAuth.authFetch('/api/assistant/conversations?limit=50');
  const data = await response.json();
  if (!response.ok) {
    throw new Error(data.error || '加载历史会话失败');
  }

  renderConversations(data.conversations || []);
}

function renderDiagnosis(data) {
  document.getElementById('agent-result-card').classList.remove('hidden');
  document.getElementById('agent-result-title').textContent = data.summary || '诊断结果';
  document.getElementById('agent-result-meta').textContent =
    `提交：${data.submission_id || '-'} ｜ 题号：${data.problem_id || '-'} ｜ 类型：${data.error_type || '-'} ｜ 置信度：${data.confidence ?? '-'}`;

  document.getElementById('agent-result-content').innerHTML = `
    <section class="agent-result-section">
      <h3>分析</h3>
      <p>${agentEscapeHtml(data.analysis || '')}</p>
    </section>
    ${agentRenderList('证据', data.evidence)}
    ${agentRenderList('知识点', data.knowledge_points)}
    ${agentRenderList('提示', data.hints)}
    <section class="agent-result-section">
      <h3>模型</h3>
      <p>${agentEscapeHtml(data.model || '-')} ${data.provider ? `｜ ${agentEscapeHtml(data.provider)}` : ''}</p>
      <p>会话：${agentEscapeHtml(data.conversation_id || '-')} ｜ 消息：${agentEscapeHtml(data.message_id || '-')}</p>
    </section>
  `;
}

function renderConversationDetail(data) {
  const conversation = data.conversation || {};
  const messages = Array.isArray(data.messages) ? data.messages : [];

  document.getElementById('agent-result-card').classList.remove('hidden');
  document.getElementById('agent-result-title').textContent = conversation.title || '历史会话';
  document.getElementById('agent-result-meta').textContent =
    `提交：${conversation.submission_id || '-'} ｜ 题号：${conversation.problem_id || '-'} ｜ 轮次：${conversation.round_count || messages.length || 0} ｜ 更新时间：${agentFormatTimestamp(conversation.updated_at)}`;

  const messageHtml = messages.length
    ? messages.map((message) => `
        <section class="agent-result-section">
          <h3>第 ${agentEscapeHtml(message.round_no || '-')} 轮</h3>
          <p><strong>用户：</strong>${agentEscapeHtml(message.user_content || '')}</p>
          <p><strong>AI：</strong>${agentEscapeHtml(message.assistant_content || '')}</p>
          <p class="meta">模型：${agentEscapeHtml(message.model || '-')} ${message.provider ? `｜ ${agentEscapeHtml(message.provider)}` : ''} ｜ 提示等级：${agentEscapeHtml(message.hint_level || '-')} ｜ 消息：${agentEscapeHtml(message.message_id || '-')}</p>
        </section>
      `).join('')
    : '<section class="agent-result-section"><p>该会话暂无消息</p></section>';

  document.getElementById('agent-result-content').innerHTML = messageHtml;
}

async function loadSelectedConversation() {
  const select = document.getElementById('agent-conversation-select');
  const conversationId = select.value;
  if (!conversationId) {
    setAgentStatus('请先选择一个历史会话', true);
    return;
  }

  const button = document.getElementById('agent-load-conversation-btn');
  button.disabled = true;
  setAgentStatus('正在加载历史会话...');

  try {
    const response = await window.ojAuth.authFetch(
      `/api/assistant/conversations/${encodeURIComponent(conversationId)}`,
    );
    const data = await response.json();
    if (!response.ok) {
      throw new Error(data.error || '加载历史会话失败');
    }
    renderConversationDetail(data);
    setAgentStatus('历史会话已加载');
  } finally {
    button.disabled = false;
  }
}

async function submitAgentQuestion() {
  const submission = selectedSubmission();
  if (!submission) {
    setAgentStatus('请先选择一次提交', true);
    return;
  }

  const question = document.getElementById('agent-question-input').value.trim();
  const hintLevel = Number(document.getElementById('agent-hint-level').value || '2');
  const button = document.getElementById('agent-submit-btn');

  button.disabled = true;
  setAgentStatus('正在生成诊断...');

  try {
    const response = await window.ojAuth.authFetch('/api/assistant/diagnoses', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        problem_id: submission.problem_id,
        submission_id: submission.submission_id,
        hint_level: hintLevel,
        question,
      }),
    });
    const data = await response.json();
    if (!response.ok) {
      throw new Error(data.error || '诊断失败');
    }
    renderDiagnosis(data);
    await loadAgentConversations();
    setAgentStatus('诊断完成');
  } finally {
    button.disabled = false;
  }
}

async function initAgentPage() {
  await window.ojAuth.initAuth();
  if (!window.ojAuth.protectPage()) {
    return;
  }
  window.ojNav.bindProtectedNavigation();

  document.getElementById('agent-submit-btn').addEventListener('click', () => {
    submitAgentQuestion().catch((error) => {
      setAgentStatus(error.message || '诊断失败', true);
    });
  });
  document.getElementById('agent-load-conversation-btn').addEventListener('click', () => {
    loadSelectedConversation().catch((error) => {
      setAgentStatus(error.message || '加载历史会话失败', true);
    });
  });

  setAgentStatus('正在加载提交记录和历史会话...');
  await Promise.all([loadAgentSubmissions(), loadAgentConversations()]);
  setAgentStatus('请选择一次提交并输入问题');
}

initAgentPage().catch((error) => {
  setAgentStatus(error.message || '页面初始化失败', true);
});
