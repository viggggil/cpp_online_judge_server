function monitorFormatTimestamp(timestamp) {
  if (!timestamp) {
    return '-';
  }
  return new Date(timestamp * 1000).toLocaleString('zh-CN');
}

function monitorEscapeHtml(value) {
  return String(value ?? '')
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;');
}

function monitorRenderStatus(status) {
  const normalized = String(status || '').toUpperCase();
  return ['OK', 'QUEUED', 'RUNNING', 'ACCEPTED'].includes(normalized)
    ? `<span class="status-ok">${monitorEscapeHtml(status || '-')}</span>`
    : `<span class="status-bad">${monitorEscapeHtml(status || '-')}</span>`;
}

function monitorServiceClass(status) {
  if (status === 'ok' || status === 'ready' || status === 'configured') {
    return 'is-ok';
  }
  if (status === 'degraded' || status === 'bad_status' || status === 'not_initialized') {
    return 'is-degraded';
  }
  return 'is-down';
}

function monitorRenderServiceCard(service) {
  const name = monitorEscapeHtml(service?.name || '-');
  const status = monitorEscapeHtml(service?.status || 'unknown');
  const latency = Number.isFinite(service?.latency_ms) ? `${service.latency_ms} ms` : '-';
  const detail = monitorEscapeHtml(service?.detail || service?.error || '-');

  return `
    <article class="monitor-status-card ${monitorServiceClass(service?.status)}">
      <div class="monitor-status-card-header">
        <h3>${name}</h3>
        <span class="monitor-badge">${status}</span>
      </div>
      <p class="monitor-status-card-meta">连通性：${service?.alive ? '正常' : '异常'}</p>
      <p class="monitor-status-card-meta">延迟：${monitorEscapeHtml(latency)}</p>
      <p class="monitor-status-card-detail">${detail}</p>
    </article>
  `;
}

function monitorRenderAgentChecks(checks) {
  const labels = {
    config: '配置',
    vector_store: 'Chroma',
    embedding: 'Embedding',
    oj_client: 'OJ Client',
    llm_client: 'OpenRouter',
  };
  const keys = ['config', 'vector_store', 'embedding', 'oj_client', 'llm_client'];
  return keys.map((key) => {
    const value = checks?.[key] || 'unknown';
    return `
      <li class="monitor-check-item ${monitorServiceClass(value)}">
        <span>${monitorEscapeHtml(labels[key] || key)}</span>
        <strong>${monitorEscapeHtml(value)}</strong>
      </li>
    `;
  }).join('');
}

function monitorRenderAgentCard(agent) {
  const name = monitorEscapeHtml(agent?.name || 'agent-service');
  const status = monitorEscapeHtml(agent?.status || 'unknown');
  const latency = Number.isFinite(agent?.latency_ms) ? `${agent.latency_ms} ms` : '-';
  const detail = monitorEscapeHtml(agent?.detail || agent?.error || '-');

  return `
    <article class="monitor-status-card monitor-agent-card ${monitorServiceClass(agent?.status)}">
      <div class="monitor-status-card-header">
        <h3>${name}</h3>
        <span class="monitor-badge">${status}</span>
      </div>
      <p class="monitor-status-card-meta">连通性：${agent?.alive ? '正常' : '异常'}</p>
      <p class="monitor-status-card-meta">延迟：${monitorEscapeHtml(latency)}</p>
      <ul class="monitor-check-list">
        ${monitorRenderAgentChecks(agent?.checks || {})}
      </ul>
      <p class="monitor-status-card-detail">${detail}</p>
    </article>
  `;
}

function monitorRenderWorkerCard(worker) {
  const name = monitorEscapeHtml(worker?.name || '-');
  const status = monitorEscapeHtml(worker?.status || 'unknown');
  const url = monitorEscapeHtml(worker?.url || '-');
  const detail = monitorEscapeHtml(worker?.error || '健康检查通过');

  return `
    <article class="monitor-worker-card ${worker?.alive ? 'is-ok' : 'is-down'}">
      <div class="monitor-status-card-header">
        <h3>${name}</h3>
        <span class="monitor-badge">${status}</span>
      </div>
      <p class="monitor-status-card-meta">地址：${url}</p>
      <p class="monitor-status-card-detail">${detail}</p>
    </article>
  `;
}

function renderMonitorSummary(summary) {
  const panel = document.getElementById('monitor-summary-panel');
  const meta = document.getElementById('monitor-summary-meta');
  const checkedAt = monitorFormatTimestamp(summary?.checked_at);
  const workers = summary?.workers || {};
  const workerItems = Array.isArray(workers.items) ? workers.items : [];

  meta.textContent =
    `整体状态：${summary?.status || 'unknown'} ｜ 检查时间：${checkedAt} ｜ worker 在线 ${workers.alive ?? 0}/${workers.total ?? 0}`;

  panel.innerHTML = `
    <div class="monitor-overview-grid">
      <article class="monitor-overview-card ${monitorServiceClass(summary?.status)}">
        <h3>总览</h3>
        <p class="monitor-overview-value">${monitorEscapeHtml(summary?.status || 'unknown')}</p>
        <p class="monitor-status-card-meta">服务：${monitorEscapeHtml(summary?.service || '-')}</p>
      </article>
      <article class="monitor-overview-card ${monitorServiceClass(workers?.status)}">
        <h3>Worker</h3>
        <p class="monitor-overview-value">${monitorEscapeHtml(String(workers.alive ?? 0))}/${monitorEscapeHtml(String(workers.total ?? 0))}</p>
        <p class="monitor-status-card-meta">状态：${monitorEscapeHtml(workers.status || 'unknown')} ｜ 异常 ${monitorEscapeHtml(String(workers.down ?? 0))}</p>
      </article>
    </div>
    <div class="monitor-status-grid">
      ${monitorRenderServiceCard(summary?.mysql)}
      ${monitorRenderServiceCard(summary?.redis)}
      ${monitorRenderServiceCard(summary?.rabbitmq)}
      ${monitorRenderServiceCard(summary?.minio)}
      ${monitorRenderAgentCard(summary?.agent_service)}
    </div>
    <div class="monitor-workers-grid">
      ${workerItems.map((worker) => monitorRenderWorkerCard(worker)).join('')}
    </div>
  `;
}

async function loadMonitorSummary(currentUser) {
  if (!window.ojAuth.requireLogin('进入监控页前请先登录')) {
    return;
  }
  if (!currentUser?.is_admin) {
    return;
  }

  const panel = document.getElementById('monitor-summary-panel');
  const meta = document.getElementById('monitor-summary-meta');
  panel.innerHTML = '<div class="monitor-summary-empty">正在加载系统状态...</div>';
  meta.textContent = '系统状态加载中...';

  const response = await window.ojAuth.authFetch('/api/admin/monitor/summary');
  const data = await response.json();
  if (!response.ok) {
    throw new Error(data.error || '加载系统状态失败');
  }

  renderMonitorSummary(data);
}

function setMonitorStatus(message, isError = false) {
  const node = document.getElementById('monitor-status-message');
  node.textContent = message;
  node.classList.toggle('status-bad', isError);
  node.classList.toggle('status-ok', !isError && Boolean(message));
}

async function monitorFetchCurrentUserOptional() {
  if (!window.ojAuth.isLoggedIn()) {
    return null;
  }

  const response = await fetch('/api/auth/me', {
    headers: {
      Authorization: `Bearer ${window.ojAuth.getToken()}`,
    },
  });

  if (!response.ok) {
    return null;
  }

  return response.json();
}

function setMonitorPermissionNotice(message, isError = false) {
  const node = document.getElementById('monitor-permission-notice');
  if (!message) {
    node.textContent = '';
    node.classList.add('hidden');
    node.classList.remove('status-bad', 'status-ok');
    return;
  }

  node.textContent = message;
  node.classList.remove('hidden');
  node.classList.toggle('status-bad', isError);
  node.classList.toggle('status-ok', !isError);
}

function setMonitorControlsDisabled(disabled) {
  ['monitor-problem-id-input', 'monitor-limit-input', 'monitor-search-btn', 'monitor-reset-btn']
    .forEach((id) => {
      const node = document.getElementById(id);
      if (node) {
        node.disabled = disabled;
      }
    });
}

function renderMonitorItems(items) {
  const container = document.getElementById('monitor-submission-list');
  if (!items.length) {
    container.textContent = '没有符合条件的提交记录。';
    return;
  }

  container.innerHTML = '';
  for (const item of items) {
    const div = document.createElement('div');
    div.className = 'list-item';
    div.innerHTML = `
      <h3><a href="/submissions/${monitorEscapeHtml(item.submission_id)}">${monitorEscapeHtml(item.submission_id)}</a></h3>
      <p>用户：${monitorEscapeHtml(item.username)} ｜ 题号：${monitorEscapeHtml(item.problem_id_text || item.problem_id)} ｜ 语言：${monitorEscapeHtml(item.language)}</p>
      <p>当前状态：${monitorRenderStatus(item.status)} ｜ 最终结果：${monitorRenderStatus(item.final_status || item.status)}</p>
      <p>耗时：${item.time_used_ms ?? 0} ms ｜ 内存：${item.memory_used_kb ?? 0} KB</p>
      <p>提交时间：${monitorFormatTimestamp(item.created_at)}</p>
      <p>${monitorEscapeHtml(item.detail || '')}</p>
    `;
    container.appendChild(div);
  }
}

async function loadMonitorSubmissions(currentUser) {
  if (!window.ojAuth.requireLogin('进入监控页前请先登录')) {
    return;
  }
  if (!currentUser?.is_admin) {
    setMonitorStatus('权限不足，需要管理员权限才能查看监控页面', true);
    return;
  }

  const limitValue = Number(document.getElementById('monitor-limit-input').value || '20');
  const limit = Number.isInteger(limitValue) ? Math.max(1, Math.min(100, limitValue)) : 20;
  const problemId = document.getElementById('monitor-problem-id-input').value.trim();

  const params = new URLSearchParams();
  params.set('limit', String(limit));
  if (problemId) {
    params.set('problem_id', problemId);
  }

  const container = document.getElementById('monitor-submission-list');
  container.textContent = '加载中...';
  setMonitorStatus('正在查询最近提交记录...');

  const response = await window.ojAuth.authFetch(`/api/admin/monitor/submissions?${params.toString()}`);
  const data = await response.json();
  if (!response.ok) {
    throw new Error(data.error || '加载监控数据失败');
  }

  renderMonitorItems(data.items || []);
  setMonitorStatus(`已加载 ${data.total_shown || 0} 条提交记录`);
}

function applyMonitorPermissionState(currentUser) {
  const subtitle = document.getElementById('monitor-subtitle');
  const container = document.getElementById('monitor-submission-list');
  const summaryPanel = document.getElementById('monitor-summary-panel');
  const summaryMeta = document.getElementById('monitor-summary-meta');

  if (!currentUser) {
    subtitle.textContent = '请先登录；只有管理员可以查看全站提交监控';
    setMonitorPermissionNotice('当前页面仅管理员可用。普通用户可以看到入口，但无法查看监控数据。', true);
    setMonitorControlsDisabled(true);
    container.textContent = '';
    summaryMeta.textContent = '等待登录';
    summaryPanel.innerHTML = '<div class="monitor-summary-empty">请先登录管理员账号。</div>';
    return;
  }

  if (!currentUser.is_admin) {
    subtitle.textContent = `当前用户：${currentUser.username}`;
    setMonitorPermissionNotice('当前账号不是管理员，无法查看全站提交监控。', true);
    setMonitorControlsDisabled(true);
    container.textContent = '';
    summaryMeta.textContent = '权限不足';
    summaryPanel.innerHTML = '<div class="monitor-summary-empty">当前账号无权查看系统状态。</div>';
    return;
  }

  subtitle.textContent = `当前管理员：${currentUser.username}`;
  setMonitorPermissionNotice('可查看服务状态、worker 状态，以及最近提交记录。');
  setMonitorControlsDisabled(false);
}

async function loadMonitorDashboard(currentUser) {
  const results = await Promise.allSettled([
    loadMonitorSummary(currentUser),
    loadMonitorSubmissions(currentUser),
  ]);

  const summaryResult = results[0];
  if (summaryResult.status === 'rejected') {
    const summaryPanel = document.getElementById('monitor-summary-panel');
    const summaryMeta = document.getElementById('monitor-summary-meta');
    summaryMeta.textContent = '系统状态加载失败';
    summaryPanel.innerHTML =
      `<div class="monitor-summary-empty">加载失败: ${monitorEscapeHtml(summaryResult.reason?.message || '系统状态加载失败')}</div>`;
  }

  const submissionsResult = results[1];
  if (submissionsResult.status === 'rejected') {
    throw submissionsResult.reason;
  }
}

async function initMonitorPage() {
  await window.ojAuth.initAuth();
  window.ojNav.bindProtectedNavigation();

  let currentUser = await monitorFetchCurrentUserOptional();
  applyMonitorPermissionState(currentUser);

  document.getElementById('monitor-search-btn').addEventListener('click', async () => {
    await loadMonitorSubmissions(currentUser);
  });

  document.getElementById('monitor-reset-btn').addEventListener('click', async () => {
    document.getElementById('monitor-problem-id-input').value = '';
    document.getElementById('monitor-limit-input').value = '20';
    await loadMonitorSubmissions(currentUser);
  });

  window.addEventListener('oj-auth-changed', async () => {
    currentUser = await monitorFetchCurrentUserOptional();
    applyMonitorPermissionState(currentUser);
    if (currentUser?.is_admin) {
      await loadMonitorDashboard(currentUser);
    }
  });

  if (currentUser?.is_admin) {
    await loadMonitorDashboard(currentUser);
  }
}

initMonitorPage().catch((error) => {
  setMonitorStatus(error.message || '页面初始化失败', true);
  document.getElementById('monitor-submission-list').textContent =
    `加载失败: ${error.message || '页面初始化失败'}`;
});
