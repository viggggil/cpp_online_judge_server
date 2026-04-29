(function () {
  const STORAGE_KEY = 'oj_platform_jwt';
  const USERNAME_KEY = 'oj_platform_username';
  let authSyncPromise = null;

  function getToken() {
    return localStorage.getItem(STORAGE_KEY) || '';
  }

  function getUsername() {
    return localStorage.getItem(USERNAME_KEY) || '';
  }

  function setSession(token, username) {
    localStorage.setItem(STORAGE_KEY, token);
    localStorage.setItem(USERNAME_KEY, username);
  }

  function isAdminRegisterMode(mode) {
    return mode === 'admin-register';
  }

  function isRegisterMode(mode) {
    return mode === 'register' || isAdminRegisterMode(mode);
  }

  function getAuthSubmitLabel(mode) {
    if (isAdminRegisterMode(mode)) return '管理员注册';
    return mode === 'register' ? '注册' : '登录';
  }

  function getAuthToggleLabel(mode) {
    return isRegisterMode(mode) ? '切换到登录' : '切换到普通注册';
  }

  function clearSession() {
    localStorage.removeItem(STORAGE_KEY);
    localStorage.removeItem(USERNAME_KEY);
  }

  function isLoggedIn() {
    return Boolean(getToken());
  }

  async function authFetch(url, options = {}) {
    const headers = new Headers(options.headers || {});
    if (getToken()) {
      headers.set('Authorization', `Bearer ${getToken()}`);
    }
    const response = await fetch(url, { ...options, headers });
    if (response.status === 401) {
      clearSession();
      updateAuthUI();
      openAuthModal('login', '请先登录后再继续操作');
      throw new Error('请先登录');
    }
    return response;
  }

  async function syncSession() {
    if (!getToken()) {
      updateAuthUI();
      return false;
    }

    try {
      const response = await fetch('/api/auth/me', {
        headers: {
          'Authorization': `Bearer ${getToken()}`
        }
      });

      if (!response.ok) {
        clearSession();
        updateAuthUI();
        return false;
      }

      const data = await response.json();
      if (data.username) {
        setSession(getToken(), data.username);
      }
      updateAuthUI();
      return true;
    } catch (_) {
      updateAuthUI();
      return Boolean(getToken());
    }
  }

  function initAuth() {
    if (!authSyncPromise) {
      authSyncPromise = syncSession().finally(() => {
        authSyncPromise = null;
      });
    }
    return authSyncPromise;
  }

  function ensureAuthShell() {
    if (document.getElementById('auth-floating')) return;

    const floating = document.createElement('div');
    floating.id = 'auth-floating';
    floating.className = 'auth-floating';
    floating.innerHTML = `
      <div id="auth-userinfo" class="auth-userinfo"></div>
      <button id="auth-open-btn" class="button auth-btn"></button>
      <button id="auth-logout-btn" class="button auth-btn auth-btn-secondary" style="display:none;">退出登录</button>
    `;
    document.body.appendChild(floating);

    const modal = document.createElement('div');
    modal.id = 'auth-modal';
    modal.className = 'auth-modal hidden';
    modal.innerHTML = `
      <div class="auth-modal-mask"></div>
      <div class="auth-dialog card">
        <div class="auth-dialog-header">
          <h2 id="auth-title">登录</h2>
          <button id="auth-close-btn" class="auth-close-btn">×</button>
        </div>
        <label class="label" for="auth-username">用户名</label>
        <input id="auth-username" class="input" placeholder="请输入用户名" />
        <label class="label" for="auth-password">密码</label>
        <input id="auth-password" type="password" class="input" placeholder="请输入密码" />
        <div id="auth-admin-code-group" class="hidden">
          <label class="label" for="auth-admin-code">管理员注册码</label>
          <input id="auth-admin-code" class="input" placeholder="请输入管理员注册码" />
        </div>
        <div class="actions auth-actions-row">
          <button id="auth-submit-btn" class="button">登录</button>
          <button id="auth-toggle-btn" class="button auth-btn-secondary">切换到注册</button>
          <button id="auth-admin-btn" class="button auth-btn-secondary">管理员注册</button>
        </div>
        <pre id="auth-message" class="notice"></pre>
      </div>
    `;
    document.body.appendChild(modal);

    document.getElementById('auth-open-btn').addEventListener('click', () => openAuthModal('login'));
    document.getElementById('auth-close-btn').addEventListener('click', closeAuthModal);
    modal.querySelector('.auth-modal-mask').addEventListener('click', closeAuthModal);
    document.getElementById('auth-toggle-btn').addEventListener('click', () => {
      const nextMode = isRegisterMode(modal.dataset.mode) ? 'login' : 'register';
      openAuthModal(nextMode);
    });
    document.getElementById('auth-admin-btn').addEventListener('click', () => openAuthModal('admin-register'));
    document.getElementById('auth-submit-btn').addEventListener('click', submitAuth);
    document.getElementById('auth-logout-btn').addEventListener('click', () => {
      clearSession();
      updateAuthUI();
    });

    updateAuthUI();
  }

  function updateAuthUI() {
    ensureAuthShell();
    const openBtn = document.getElementById('auth-open-btn');
    const logoutBtn = document.getElementById('auth-logout-btn');
    const userInfo = document.getElementById('auth-userinfo');
    if (isLoggedIn()) {
      openBtn.textContent = '已登录';
      logoutBtn.style.display = 'inline-block';
      userInfo.textContent = `当前用户：${getUsername()}`;
    } else {
      openBtn.textContent = '登录 / 注册';
      logoutBtn.style.display = 'none';
      userInfo.textContent = '未登录';
    }
  }

  function openAuthModal(mode = 'login', message = '') {
    ensureAuthShell();
    const modal = document.getElementById('auth-modal');
    const adminCodeGroup = document.getElementById('auth-admin-code-group');
    const adminBtn = document.getElementById('auth-admin-btn');
    modal.classList.remove('hidden');
    modal.dataset.mode = mode;
    document.getElementById('auth-title').textContent = getAuthSubmitLabel(mode);
    document.getElementById('auth-submit-btn').textContent = getAuthSubmitLabel(mode);
    document.getElementById('auth-toggle-btn').textContent = getAuthToggleLabel(mode);
    document.getElementById('auth-message').textContent = message;
    adminCodeGroup.classList.toggle('hidden', !isAdminRegisterMode(mode));
    adminBtn.style.display = mode === 'login' ? 'inline-block' : 'none';
  }

  function closeAuthModal() {
    const modal = document.getElementById('auth-modal');
    if (modal) modal.classList.add('hidden');
  }

  async function submitAuth() {
    const modal = document.getElementById('auth-modal');
    const mode = modal.dataset.mode || 'login';
    const username = document.getElementById('auth-username').value.trim();
    const password = document.getElementById('auth-password').value;
    const adminCode = document.getElementById('auth-admin-code').value.trim();
    const message = document.getElementById('auth-message');

    const endpoint = isAdminRegisterMode(mode) ? '/api/auth/admin/register' : `/api/auth/${mode}`;
    const payload = { username, password };
    if (isAdminRegisterMode(mode)) {
      payload.admin_code = adminCode;
    }

    message.textContent = isRegisterMode(mode) ? '注册中...' : '登录中...';
    const response = await fetch(endpoint, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });

    const data = await response.json();
    if (!response.ok) {
      throw new Error(data.error || '操作失败');
    }

    setSession(data.token, data.username);
    updateAuthUI();
    message.textContent = isRegisterMode(mode) ? '注册成功' : '登录成功';
    setTimeout(closeAuthModal, 300);
  }

  function requireLogin(message = '请先登录') {
    if (isLoggedIn()) return true;
    openAuthModal('login', message);
    return false;
  }

  function protectPage() {
    ensureAuthShell();
    if (!isLoggedIn()) {
      openAuthModal('login', '请先登录后再访问该页面');
      return false;
    }
    return true;
  }

  document.addEventListener('DOMContentLoaded', () => {
    ensureAuthShell();
    updateAuthUI();
    initAuth();
  });

  window.ojAuth = {
    getToken,
    getUsername,
    isLoggedIn,
    authFetch,
    requireLogin,
    protectPage,
    syncSession,
    initAuth,
    openAuthModal,
    clearSession,
    updateAuthUI,
  };

  window.addEventListener('unhandledrejection', (event) => {
    if (event.reason && event.reason.message) {
      const message = document.getElementById('auth-message');
      if (message && !document.getElementById('auth-modal').classList.contains('hidden')) {
        message.textContent = event.reason.message;
        event.preventDefault();
      }
    }
  });
})();