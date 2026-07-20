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

function sleep(ms) {
  return new Promise((resolve) => window.setTimeout(resolve, ms));
}

let agentMode = 'new_conversation';
let activeConversationId = '';
let isSubmittingAgentQuestion = false;

function setAgentMode(mode) {
  agentMode = mode;
  document
    .getElementById('agent-submission-picker')
    .classList.toggle('agent-mode-active', mode === 'new_conversation');
  document
    .getElementById('agent-conversation-picker')
    .classList.toggle('agent-mode-active', mode === 'continue_conversation');
}

function parseAgentEventData(event) {
  if (!event?.data_json) {
    return {};
  }
  try {
    return JSON.parse(event.data_json);
  } catch {
    return {};
  }
}

function chatMessagesNode() {
  return document.getElementById('agent-chat-messages');
}

function scrollChatToBottom() {
  const node = chatMessagesNode();
  node.scrollTop = node.scrollHeight;
}

function clearChat() {
  chatMessagesNode().innerHTML = '';
}

function renderMarkdownToNode(contentNode) {
  if (!contentNode) {
    return;
  }
  const markdown = contentNode.dataset.rawMarkdown || '';
  if (!markdown) {
    contentNode.innerHTML = '';
    return;
  }
  if (window.ojMarkdown?.markdownToHtml) {
    contentNode.innerHTML = window.ojMarkdown.markdownToHtml(markdown);
  } else {
    contentNode.textContent = markdown;
  }
}

function appendChatMessage(role, content = '', meta = '') {
  const wrapper = document.createElement('div');
  wrapper.className = `agent-chat-message agent-chat-message-${role}`;

  const bubble = document.createElement('div');
  bubble.className = 'agent-chat-bubble';

  if (meta) {
    const metaNode = document.createElement('div');
    metaNode.className = 'agent-chat-meta';
    metaNode.textContent = meta;
    bubble.appendChild(metaNode);
  }

  const contentNode = document.createElement('div');
  contentNode.className = 'agent-chat-content';
  contentNode.dataset.rawMarkdown = content || '';
  renderMarkdownToNode(contentNode);
  bubble.appendChild(contentNode);

  wrapper.appendChild(bubble);
  chatMessagesNode().appendChild(wrapper);
  scrollChatToBottom();
  return contentNode;
}

function appendToMessage(contentNode, content, { paragraph = false } = {}) {
  if (!contentNode || !content) {
    return;
  }
  const current = contentNode.dataset.rawMarkdown || '';
  const prefix = paragraph && current ? '\n\n' : '';
  contentNode.dataset.rawMarkdown = `${current}${prefix}${content}`;
  renderMarkdownToNode(contentNode);
  scrollChatToBottom();
}

function setMessage(contentNode, content) {
  if (!contentNode) {
    return;
  }
  contentNode.dataset.rawMarkdown = content || '';
  renderMarkdownToNode(contentNode);
  scrollChatToBottom();
}

function showNewConversationPrompt() {
  clearChat();
  appendChatMessage('assistant', '已开启新对话。可以直接问算法、题目思路，也可以选择一次提交作为上下文。');
}

function activateNewConversation({ clearMessages = false } = {}) {
  activeConversationId = '';
  setAgentMode('new_conversation');
  const conversationSelect = document.getElementById('agent-conversation-select');
  if (conversationSelect) {
    conversationSelect.value = '';
  }
  if (clearMessages) {
    showNewConversationPrompt();
  }
}

function formatSourceLine(source) {
  const title = source.title || source.document_id || source.source || '知识片段';
  const score = source.score !== undefined ? `，score ${source.score}` : '';
  return `${title}${source.knowledge_point ? `，${source.knowledge_point}` : ''}${score}`;
}

function formatFinalChat(data) {
  const lines = [];
  if (data.answer) {
    lines.push(data.answer);
  }
  if (Array.isArray(data.sources) && data.sources.length) {
    lines.push(`资料来源：\n${data.sources.map((source) => `- ${formatSourceLine(source)}`).join('\n')}`);
  }
  lines.push(`模型：${data.model || '-'}${data.provider ? ` ｜ ${data.provider}` : ''}`);
  lines.push(`会话：${data.conversation_id || '-'} ｜ 消息：${data.message_id || '-'}`);
  return lines.filter(Boolean).join('\n\n');
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
    label: option.textContent || option.value,
  };
}

function renderSubmissions(submissions) {
  const select = document.getElementById('agent-submission-select');
  select.innerHTML = '';

  const emptyOption = document.createElement('option');
  emptyOption.value = '';
  emptyOption.textContent = '不绑定提交，直接开启新对话';
  select.appendChild(emptyOption);

  if (!submissions.length) {
    select.disabled = false;
    document.getElementById('agent-submit-btn').disabled = false;
    return;
  }

  select.disabled = false;
  document.getElementById('agent-submit-btn').disabled = false;

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

  const newOption = document.createElement('option');
  newOption.value = '';
  newOption.textContent = '开启新对话';
  select.appendChild(newOption);

  if (!conversations.length) {
    select.disabled = false;
    document.getElementById('agent-load-conversation-btn').disabled = true;
    return;
  }

  select.disabled = false;
  document.getElementById('agent-load-conversation-btn').disabled = false;

  for (const item of conversations) {
    const option = document.createElement('option');
    option.value = item.conversation_id;
    const problemText = item.problem_id ? `题号 ${item.problem_id}` : '未绑定题目';
    option.textContent =
      `${item.title || item.conversation_id} ｜ ${problemText} ｜ ${item.round_count || 0} 轮 ｜ ${agentFormatTimestamp(item.updated_at)}`;
    select.appendChild(option);
  }

  if (activeConversationId) {
    select.value = activeConversationId;
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

function renderConversationDetail(data) {
  const conversation = data.conversation || {};
  const messages = Array.isArray(data.messages) ? data.messages : [];

  clearChat();
  if (!messages.length) {
    appendChatMessage('assistant', '该会话暂无消息');
    return;
  }

  for (const message of messages) {
    appendChatMessage(
      'user',
      message.user_content || '',
      `第 ${message.round_no || '-'} 轮 ｜ 提交 ${conversation.submission_id || '-'}`,
    );
    appendChatMessage(
      'assistant',
      message.assistant_content || '',
      `AI ｜ ${message.model || '-'}${message.provider ? ` ｜ ${message.provider}` : ''}`,
    );
  }
}

async function loadSelectedConversation() {
  const select = document.getElementById('agent-conversation-select');
  const conversationId = select.value;
  if (!conversationId) {
    appendChatMessage('assistant', '请先选择一个历史会话');
    return;
  }

  const button = document.getElementById('agent-load-conversation-btn');
  button.disabled = true;
  setAgentMode('continue_conversation');
  activeConversationId = conversationId;

  try {
    const response = await window.ojAuth.authFetch(
      `/api/assistant/conversations/${encodeURIComponent(conversationId)}`,
    );
    const data = await response.json();
    if (!response.ok) {
      throw new Error(data.error || '加载历史会话失败');
    }
    renderConversationDetail(data);
  } finally {
    button.disabled = false;
  }
}

function shouldShowStatus(stage, message) {
  if ([
    'queued',
    'validating',
    'loading_problem',
    'loading_submission',
    'saving',
  ].includes(stage)) {
    return false;
  }

  return ![
    '正在查询本地知识库',
    '正在调用模型流式生成回答',
  ].includes(message);
}

async function submitAgentQuestion() {
  if (isSubmittingAgentQuestion) {
    return;
  }

  const input = document.getElementById('agent-question-input');
  const question = input.value.trim();
  const hintLevel = Number(document.getElementById('agent-hint-level').value || '2');
  const button = document.getElementById('agent-submit-btn');
  const submission = selectedSubmission();
  const conversationId = document.getElementById('agent-conversation-select').value;

  if (!question) {
    appendChatMessage('assistant', '请先输入你的问题');
    return;
  }
  if (agentMode === 'continue_conversation' && !conversationId) {
    activateNewConversation();
  }

  button.disabled = true;
  input.disabled = true;
  isSubmittingAgentQuestion = true;
  appendChatMessage(
    'user',
    question,
    agentMode === 'new_conversation'
      ? (submission ? submission.label : '新对话')
      : `继续会话 ${conversationId}`,
  );
  const assistantMessage = appendChatMessage('assistant', '', 'AI');
  input.value = '';
  input.style.height = '';

  try {
    const endpoint = agentMode === 'new_conversation'
      ? '/api/assistant/chat/stream'
      : `/api/assistant/conversations/${encodeURIComponent(conversationId)}/chat/stream`;
    const requestBody = {
      hint_level: hintLevel,
      message: question,
    };
    if (agentMode === 'new_conversation' && submission) {
      requestBody.context = {
        problem_id: submission.problem_id,
        submission_id: submission.submission_id,
      };
    }

    const response = await window.ojAuth.authFetch(endpoint, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(requestBody),
    });
    const data = await response.json();
    if (!response.ok) {
      throw new Error(data.error || '创建对话任务失败');
    }

    let lastEventId = 0;
    const pollIntervalMs = Number(data.poll_interval_ms || 700);
    let finished = false;
    let lastStatusText = '';

    while (!finished) {
      await sleep(pollIntervalMs);
      const eventsResponse = await window.ojAuth.authFetch(
        `/api/assistant/chat/jobs/${encodeURIComponent(data.job_id)}/events?after=${lastEventId}`,
      );
      const eventsData = await eventsResponse.json();
      if (!eventsResponse.ok) {
        throw new Error(eventsData.error || '读取对话进度失败');
      }

      for (const event of eventsData.events || []) {
        lastEventId = Math.max(lastEventId, Number(event.id || 0));
        const eventData = parseAgentEventData(event);

        if (event.event === 'status') {
          if (shouldShowStatus(eventData.stage, eventData.message)) {
            const statusText = eventData.message || eventData.stage || '对话进行中';
            if (statusText !== lastStatusText) {
              lastStatusText = statusText;
              appendToMessage(assistantMessage, statusText, { paragraph: true });
            }
          }
        } else if (event.event === 'tool_call') {
          appendToMessage(assistantMessage, `正在查询：${eventData.name || '工具'}`, { paragraph: true });
        } else if (event.event === 'tool_result') {
          if (eventData.summary) {
            appendToMessage(assistantMessage, eventData.summary, { paragraph: true });
          }
        } else if (event.event === 'sources') {
          const sourceCount = Array.isArray(eventData.sources) ? eventData.sources.length : 0;
          appendToMessage(assistantMessage, `${eventData.message || '知识库检索完成'}，命中 ${sourceCount} 个片段`, { paragraph: true });
        } else if (event.event === 'delta') {
          appendToMessage(assistantMessage, eventData.content || '');
        } else if (event.event === 'done') {
          setMessage(assistantMessage, formatFinalChat(eventData));
          const completedConversationId = eventData.conversation_id || activeConversationId;
          activeConversationId = completedConversationId;
          await loadAgentConversations();
          if (completedConversationId) {
            setAgentMode('continue_conversation');
            const conversationSelect = document.getElementById('agent-conversation-select');
            conversationSelect.value = completedConversationId;
          }
          finished = true;
        } else if (event.event === 'error') {
          throw new Error(eventData.message || '对话失败');
        }
      }

      if (eventsData.done && !finished) {
        finished = true;
      }
    }
  } catch (error) {
    appendToMessage(assistantMessage, error.message || '对话失败', { paragraph: true });
  } finally {
    isSubmittingAgentQuestion = false;
    button.disabled = false;
    input.disabled = false;
    input.focus();
  }
}

function bindComposer() {
  const input = document.getElementById('agent-question-input');
  input.addEventListener('input', () => {
    input.style.height = 'auto';
    input.style.height = `${Math.min(input.scrollHeight, 160)}px`;
  });
  input.addEventListener('keydown', (event) => {
    if (event.key === 'Enter' && !event.shiftKey) {
      event.preventDefault();
      submitAgentQuestion();
    }
  });
}

function bindModePickers() {
  const submissionPicker = document.getElementById('agent-submission-picker');
  const conversationPicker = document.getElementById('agent-conversation-picker');
  const submissionSelect = document.getElementById('agent-submission-select');
  const conversationSelect = document.getElementById('agent-conversation-select');

  submissionPicker.addEventListener('click', () => activateNewConversation({ clearMessages: true }));
  conversationPicker.addEventListener('click', () => setAgentMode('continue_conversation'));
  submissionSelect.addEventListener('focus', () => setAgentMode('new_conversation'));
  submissionSelect.addEventListener('change', () => activateNewConversation({ clearMessages: true }));
  conversationSelect.addEventListener('focus', () => setAgentMode('continue_conversation'));
  conversationSelect.addEventListener('change', () => {
    activeConversationId = conversationSelect.value;
    if (!activeConversationId) {
      activateNewConversation({ clearMessages: true });
      return;
    }
    setAgentMode('continue_conversation');
    loadSelectedConversation().catch((error) => {
      appendChatMessage('assistant', error.message || '加载历史会话失败');
    });
  });
}

async function initAgentPage() {
  await window.ojAuth.initAuth();
  if (!window.ojAuth.protectPage()) {
    return;
  }
  window.ojNav.bindProtectedNavigation();

  document.getElementById('agent-submit-btn').addEventListener('click', () => {
    submitAgentQuestion();
  });
  document.getElementById('agent-load-conversation-btn').addEventListener('click', () => {
    loadSelectedConversation().catch((error) => {
      appendChatMessage('assistant', error.message || '加载历史会话失败');
    });
  });
  bindComposer();
  bindModePickers();

  appendChatMessage('assistant', '正在加载提交记录和历史会话...');
  try {
    await Promise.all([loadAgentSubmissions(), loadAgentConversations()]);
    clearChat();
    appendChatMessage('assistant', '可以直接开启新对话，也可以选择一次提交作为上下文。');
  } catch (error) {
    clearChat();
    appendChatMessage('assistant', error.message || '页面初始化失败');
  }
}

initAgentPage();
