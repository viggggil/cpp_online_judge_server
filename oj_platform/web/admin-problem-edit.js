function getProblemIdFromEditPath() {
  const params = new URLSearchParams(window.location.search);
  const fromQuery = params.get('problem_id');
  if (fromQuery) return fromQuery;

  const parts = window.location.pathname.split('/').filter(Boolean);
  return parts[2] || '';
}

function escapeHtml(text) {
  return String(text)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;');
}

function renderInlineMarkdown(text) {
  return text
    .replace(/`([^`]+)`/g, '<code>$1</code>')
    .replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>')
    .replace(/\*([^*]+)\*/g, '<em>$1</em>')
    .replace(/\[([^\]]+)\]\((https?:\/\/[^\s)]+)\)/g, '<a href="$2" target="_blank" rel="noopener noreferrer">$1</a>');
}

function markdownToHtml(markdown) {
  const escaped = escapeHtml(markdown || '');
  const lines = escaped.split('\n');
  const html = [];
  let inList = false;
  let inCodeBlock = false;
  let codeBuffer = [];

  const closeList = () => {
    if (inList) {
      html.push('</ul>');
      inList = false;
    }
  };

  const closeCodeBlock = () => {
    if (inCodeBlock) {
      html.push(`<pre><code>${codeBuffer.join('\n')}</code></pre>`);
      codeBuffer = [];
      inCodeBlock = false;
    }
  };

  for (const line of lines) {
    if (line.startsWith('```')) {
      closeList();
      if (inCodeBlock) {
        closeCodeBlock();
      } else {
        inCodeBlock = true;
      }
      continue;
    }

    if (inCodeBlock) {
      codeBuffer.push(line);
      continue;
    }

    if (!line.trim()) {
      closeList();
      html.push('');
      continue;
    }

    const headingMatch = line.match(/^(#{1,6})\s+(.*)$/);
    if (headingMatch) {
      closeList();
      const level = headingMatch[1].length;
      html.push(`<h${level}>${renderInlineMarkdown(headingMatch[2])}</h${level}>`);
      continue;
    }

    const listMatch = line.match(/^[-*]\s+(.*)$/);
    if (listMatch) {
      if (!inList) {
        html.push('<ul>');
        inList = true;
      }
      html.push(`<li>${renderInlineMarkdown(listMatch[1])}</li>`);
      continue;
    }

    closeList();
    html.push(`<p>${renderInlineMarkdown(line)}</p>`);
  }

  closeList();
  closeCodeBlock();
  return html.join('\n') || '<p>暂无内容</p>';
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
  document.getElementById('statement-preview').innerHTML = markdownToHtml(source);
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

  document.getElementById('page-title').textContent = `编辑题面 - ${problemId}`;
  document.getElementById('page-subtitle').textContent = `当前管理员：${currentUser.username}`;
  document.getElementById('back-to-problem').href = `/problems/${problemId}`;
  document.getElementById('cancel-btn').addEventListener('click', () => {
    window.location.href = `/problems/${problemId}`;
  });
  document.getElementById('save-btn').addEventListener('click', () => saveStatement(problemId));
  document.getElementById('statement-editor').addEventListener('input', updatePreview);

  await loadStatement(problemId);
}

initPage().catch((error) => {
  setStatus(error.message || '页面初始化失败', true);
});