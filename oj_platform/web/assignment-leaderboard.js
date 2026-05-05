function assignmentIdFromPath() {
  const parts = window.location.pathname.split('/').filter(Boolean);
  return parts[1];
}

function formatTimestamp(timestamp) {
  if (!timestamp) {
    return '-';
  }
  return new Date(timestamp * 1000).toLocaleString('zh-CN');
}

function formatDuration(totalSeconds) {
  const seconds = Math.max(0, Number(totalSeconds) || 0);
  const hours = Math.floor(seconds / 3600);
  const minutes = Math.floor((seconds % 3600) / 60);
  const remainSeconds = seconds % 60;
  const pad = (value) => String(value).padStart(2, '0');
  return `${hours}:${pad(minutes)}:${pad(remainSeconds)}`;
}

function normalizeStatusText(status) {
  const value = String(status || 'NONE').toUpperCase();
  if (value === 'NONE') return '未提交';
  if (value === 'ACCEPTED' || value === 'OK') return 'Accepted';
  if (value === 'WRONG_ANSWER') return 'Wrong Answer';
  if (value === 'COMPILE_ERROR') return 'Compile Error';
  if (value === 'RUNTIME_ERROR') return 'Runtime Error';
  if (value === 'TIME_LIMIT_EXCEEDED') return 'Time Limit Exceeded';
  if (value === 'MEMORY_LIMIT_EXCEEDED') return 'Memory Limit Exceeded';
  if (value === 'OUTPUT_LIMIT_EXCEEDED') return 'Output Limit Exceeded';
  if (value === 'PRESENTATION_ERROR') return 'Presentation Error';
  if (value === 'SYSTEM_ERROR') return 'System Error';
  if (value === 'RUNNING') return 'Running';
  if (value === 'QUEUED') return 'Queued';
  return value.replaceAll('_', ' ');
}

function renderLeaderboardCell(cell) {
  if (!cell || !cell.has_submission) {
    return '<div class="leaderboard-cell-empty">-</div>';
  }

  if (cell.accepted) {
    return `
      <div class="leaderboard-cell-score leaderboard-cell-accepted">100</div>
      <div class="leaderboard-cell-time leaderboard-cell-accepted">${formatDuration(cell.time_from_start_seconds)}</div>
    `;
  }

  return `
    <div class="leaderboard-cell-score leaderboard-cell-failed">0</div>
    <div class="leaderboard-cell-time leaderboard-cell-failed">${formatDuration(cell.time_from_start_seconds)}</div>
    <div class="leaderboard-cell-status">${normalizeStatusText(cell.status)}</div>
  `;
}

function renderLeaderboardTable(leaderboard) {
  const container = document.getElementById('assignment-leaderboard');
  const summary = document.getElementById('assignment-leaderboard-summary');
  const problems = leaderboard.problems || [];
  const entries = leaderboard.entries || [];

  summary.textContent =
    `共 ${entries.length} 名用户上榜，题目数 ${problems.length}，按过题数优先、总罚时次之排序。`;

  if (!problems.length) {
    container.innerHTML = '<div class="leaderboard-empty">当前作业还没有题目，暂无排行榜。</div>';
    return;
  }

  if (!entries.length) {
    container.innerHTML = '<div class="leaderboard-empty">当前还没有有效作业内提交，暂无排行榜。</div>';
    return;
  }

  const headerCells = problems.map((problem) => `
    <th class="leaderboard-problem-col">
      <div class="leaderboard-problem-alias">${problem.alias || '-'}</div>
      <div class="leaderboard-problem-meta">${problem.accepted_user_count}/${problem.submission_count}</div>
    </th>
  `).join('');

  const bodyRows = entries.map((entry) => {
    const cellsHtml = (entry.cells || []).map((cell) => `
      <td class="leaderboard-cell">
        ${renderLeaderboardCell(cell)}
      </td>
    `).join('');

    return `
      <tr>
        <td class="leaderboard-rank">${entry.rank}</td>
        <td class="leaderboard-user">${entry.username}</td>
        <td class="leaderboard-score">${entry.score}</td>
        <td class="leaderboard-penalty">${formatDuration(entry.penalty_seconds)}</td>
        ${cellsHtml}
      </tr>
    `;
  }).join('');

  container.innerHTML = `
    <div class="leaderboard-table-wrap">
      <table class="leaderboard-table">
        <thead>
          <tr>
            <th>排名</th>
            <th>用户</th>
            <th>分数</th>
            <th>总耗时</th>
            ${headerCells}
          </tr>
        </thead>
        <tbody>
          ${bodyRows}
        </tbody>
      </table>
    </div>
  `;
}

function renderLeaderboardLoadError(message) {
  document.getElementById('assignment-leaderboard').innerHTML =
    `<div class="leaderboard-empty">加载失败: ${message}</div>`;
  document.getElementById('assignment-leaderboard-summary').textContent = '排行榜加载失败';
}

async function loadLeaderboard(assignmentId) {
  const container = document.getElementById('assignment-leaderboard');
  const summary = document.getElementById('assignment-leaderboard-summary');
  container.innerHTML = '<div class="leaderboard-empty">排行榜加载中...</div>';
  summary.textContent = '排行榜加载中...';

  const response = await fetch(`/api/assignments/${assignmentId}/leaderboard`);
  const data = await response.json();
  if (!response.ok) {
    throw new Error(data.error || '加载排行榜失败');
  }

  document.getElementById('leaderboard-title').textContent = `${data.title} 排行榜`;
  document.getElementById('leaderboard-meta').textContent =
    `作业编号：${data.assignment_id}\n开始时间：${formatTimestamp(data.start_at)}\n结束时间：${formatTimestamp(data.end_at)}\n题目数量：${(data.problems || []).length}`;
  document.getElementById('back-to-assignment-link').href = `/assignments/${encodeURIComponent(assignmentId)}`;

  renderLeaderboardTable(data);
}

async function loadLeaderboardPage() {
  await window.ojAuth.initAuth();
  window.ojNav.bindProtectedNavigation();

  const assignmentId = assignmentIdFromPath();
  document.getElementById('reload-leaderboard-btn').addEventListener('click', () => {
    loadLeaderboard(assignmentId).catch((error) => {
      renderLeaderboardLoadError(error.message);
    });
  });

  await loadLeaderboard(assignmentId).catch((error) => {
    document.getElementById('leaderboard-meta').textContent = `加载失败: ${error.message}`;
    renderLeaderboardLoadError(error.message);
  });
}

loadLeaderboardPage();
