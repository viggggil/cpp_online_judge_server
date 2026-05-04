(function () {
  async function fetchCurrentUserOptional() {
    if (!window.ojAuth || !window.ojAuth.isLoggedIn()) {
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

  async function handleCreateNavigation(event) {
    if (!window.ojAuth.requireLogin('进入创建页前请先登录')) {
      event.preventDefault();
      return;
    }

    const currentUser = await fetchCurrentUserOptional();
    if (!currentUser?.is_admin) {
      event.preventDefault();
      alert('权限不足，需要管理员权限才能进入创建页面');
    }
  }

  function handleProtectedNavigation(event, message) {
    if (!window.ojAuth.requireLogin(message)) {
      event.preventDefault();
    }
  }

  function bindProtectedNavigation() {
    document.querySelectorAll('.nav-submissions').forEach((element) => {
      element.addEventListener('click', (event) => {
        handleProtectedNavigation(event, '查看提交记录前请先登录');
      });
    });

    document.querySelectorAll('.nav-create').forEach((element) => {
      element.addEventListener('click', handleCreateNavigation);
    });
  }

  window.ojNav = {
    bindProtectedNavigation,
    fetchCurrentUserOptional,
  };
})();
