'use strict';

const $ = (id) => document.getElementById(id);
const sectionNames = {
  overview: '设备概览',
  video: '视频与识别',
  snapshot: '截图管理',
  motor: '外部控制',
  account: '账户设置',
};

let csrf = '';
let active = false;
let capabilities = null;
let sessionVersion = 0;
let pollTimer = null;
let previewURL = '';
let toastTimer = null;

class SessionChangedError extends Error {
  constructor() {
    super('会话已变更');
  }
}

function text(id, value) {
  $(id).textContent = value;
}

function notify(message, error = false) {
  text('toast', message);
  $('toast').classList.toggle('error-toast', error);
  $('toast').classList.remove('hidden');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => $('toast').classList.add('hidden'), 6500);
}

function showError(error) {
  if (!(error instanceof SessionChangedError)) {
    notify(error.message, true);
  }
}

function activity(message) {
  const list = $('activity');
  if (list.children[0]?.textContent.startsWith('登录后')) {
    list.replaceChildren();
  }
  const item = document.createElement('li');
  item.textContent = `${new Date().toLocaleTimeString()}　${message}`;
  list.prepend(item);
  while (list.children.length > 8) {
    list.lastChild.remove();
  }
}

function selectPage(page) {
  for (const button of document.querySelectorAll('.nav-item')) {
    button.classList.toggle('active', button.dataset.page === page);
  }
  for (const section of document.querySelectorAll('.page')) {
    section.classList.toggle('hidden', section.id !== `page-${page}`);
  }
  text('page-title', sectionNames[page]);
}

function clearPreview() {
  $('preview').removeAttribute('src');
  $('preview').classList.add('hidden');
  $('preview-empty').classList.remove('hidden');
  $('download').removeAttribute('href');
  $('download').classList.add('hidden');
  text('snapshot-meta', '截图尚未生成');
  if (previewURL) {
    URL.revokeObjectURL(previewURL);
    previewURL = '';
  }
}

function loggedOut() {
  // 旧会话的响应即使稍后返回，也不能更新新会话的页面。
  sessionVersion += 1;
  active = false;
  csrf = '';
  capabilities = null;
  clearTimeout(pollTimer);
  clearTimeout(toastTimer);
  $('toast').classList.add('hidden');
  $('login-view').classList.remove('hidden');
  $('console-view').classList.add('hidden');
  $('password').value = '';
  $('password-form').reset();
  clearPreview();
  selectPage('overview');

  const placeholder = document.createElement('li');
  placeholder.textContent = '登录后，操作记录将显示在这里。';
  $('activity').replaceChildren(placeholder);
}

async function api(path, method = 'GET', data) {
  const version = sessionVersion;
  const headers = {};
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 15000);
  if (method !== 'GET') {
    headers['X-CSRF-Token'] = csrf;
  }
  if (data !== undefined) {
    headers['Content-Type'] = 'application/json';
  }

  try {
    const response = await fetch(`/api/v1/${path}`, {
      method,
      headers,
      body: data === undefined ? undefined : JSON.stringify(data),
      credentials: 'same-origin',
      signal: controller.signal,
    });
    let result;
    try {
      result = await response.json();
    } catch (error) {
      if (controller.signal.aborted) {
        throw error;
      }
      throw new Error('设备响应格式异常');
    }
    if (version !== sessionVersion) {
      throw new SessionChangedError();
    }
    if (!response.ok) {
      const error = new Error(result.message || result.code);
      error.status = response.status;
      if (response.status === 401 && path !== 'auth/login' && path !== 'auth/session') {
        loggedOut();
      }
      throw error;
    }
    return result.data;
  } catch (error) {
    if (version !== sessionVersion && error.status !== 401) {
      throw new SessionChangedError();
    }
    if (controller.signal.aborted) {
      throw new Error('设备请求超时，请检查网络后重试。');
    }
    throw error;
  } finally {
    clearTimeout(timer);
  }
}

async function waitTask(task) {
  const version = sessionVersion;
  const deadline = Date.now() + 45000;
  while (Date.now() < deadline) {
    if (version !== sessionVersion) {
      throw new SessionChangedError();
    }
    const current = await api(`tasks/${task.id}`);
    if (current.state === 'succeeded') {
      return current.result;
    }
    if (current.state === 'failed') {
      throw new Error(current.error.message);
    }
    if (current.state === 'cancelled') {
      throw new Error('操作已取消');
    }
    await new Promise((resolve) => setTimeout(resolve, 300));
  }
  throw new Error(`操作仍可能在执行，请刷新设备状态；任务 ID：${task.id}`);
}

function fillForm(id, settings) {
  for (const input of $(id).elements) {
    if (!input.name || settings[input.name] === undefined) {
      continue;
    }
    if (input.type === 'checkbox') {
      input.checked = settings[input.name];
    } else {
      input.value = settings[input.name];
    }
  }
}

function readForm(form) {
  const result = {};
  for (const input of form.elements) {
    if (!input.name) {
      continue;
    }
    if (input.type === 'checkbox') {
      result[input.name] = input.checked;
    } else if (input.type === 'number') {
      result[input.name] = Number(input.value);
    } else {
      result[input.name] = input.value;
    }
  }
  return result;
}

function applyCapabilities() {
  if (!capabilities) {
    return;
  }
  const format = $('snapshot-form').elements.format;
  for (const option of format.options) {
    option.disabled = !capabilities.snapshot_formats.includes(option.value);
  }
  if (!capabilities.snapshot_formats.includes(format.value)) {
    // 表单选择在提交后才保存，不将它误报为已生效配置。
    format.value = capabilities.snapshot_formats[0] || '';
  }
  text(
    'snapshot-format-hint',
    capabilities.simulated_media
      ? '当前主机模拟后端仅生成 BMP。格式选择在保存后写入配置。'
      : '截图使用当前视频分辨率。质量仅对 JPEG 生效，BMP 为未压缩图像。',
  );
}

async function loadSettings() {
  const [video, ai, snapshot] = await Promise.all(
    ['video', 'ai', 'snapshot'].map((section) => api(`${section}/config`)),
  );
  fillForm('video-form', video);
  fillForm('ai-form', ai);
  fillForm('snapshot-form', snapshot);
  text('model-path', ai.model_path);
  text(
    'video-summary',
    `${video.width} × ${video.height} / ${video.fps} FPS / ${video.bitrate_kbps} kbps`,
  );
  text(
    'rtsp-url',
    capabilities?.simulated_media
      ? '主机模拟模式未启动 RTSP'
      : `rtsp://${location.hostname}:${video.rtsp_port}${video.rtsp_path}`,
  );
  applyCapabilities();
}

function renderStatus(status) {
  const { media, motor } = status;
  const states = {
    starting: '启动中',
    streaming: '推流中',
    unavailable: '媒体不可用',
    degraded: '媒体异常',
    simulated: '主机模拟',
  };
  text('media-state', states[media.state] || media.state);
  let mediaDetail = '请检查传感器、模型与设备占用';
  if (media.simulated) {
    mediaDetail = '模拟媒体，不产生真实视频';
  } else if (media.ready) {
    mediaDetail = '媒体通道已启动';
  }
  text('media-detail', mediaDetail);
  $('simulation-banner').classList.toggle('hidden', !media.simulated);
  text('fps', media.simulated ? '—' : Number(media.fps).toFixed(1));
  text(
    'memory',
    status.memory.MemAvailable_kib === undefined
      ? '—'
      : (status.memory.MemAvailable_kib / 1024).toFixed(1),
  );
  text(
    'memory-detail',
    status.memory.MemTotal_kib === undefined
      ? '读取中'
      : `系统可见总内存 ${(status.memory.MemTotal_kib / 1024).toFixed(1)} MiB`,
  );
  const seconds = status.uptime_seconds;
  text('uptime', `${Math.floor(seconds / 3600)}h ${Math.floor((seconds % 3600) / 60)}m`);
  text('frames', `已处理 ${Number(media.frames).toLocaleString()} 帧`);
  text('device-name', status.device_name);
  text('detections', media.simulated ? '模拟模式' : `${media.detections} 个目标`);
  text('media-error', media.error || '无');
  text('motor-backend', motor.backend);
  text('motor-moving', motor.moving ? '运动中' : '停止');

  let motorLabel = '未接入';
  let motorDescription = '当前没有电机驱动后端，接口返回 motor_unavailable。';
  if (motor.simulated) {
    motorLabel = '模拟后端';
    motorDescription = '当前为模拟电机，可验证命令和停止行为，不会驱动硬件。';
  } else if (motor.available) {
    motorLabel = '已接入';
    motorDescription = '硬件控制后端已接入。';
  }
  text('motor-badge', motorLabel);
  text('motor-description', motorDescription);
  $('motor-move').disabled = !motor.available;
  $('motor-stop').disabled = !motor.available;
  text('connection', '设备已连接');
}

async function refresh() {
  const version = sessionVersion;
  const status = await api('device/status');
  if (active && version === sessionVersion) {
    renderStatus(status);
  }
}

async function poll(version = sessionVersion) {
  if (!active || version !== sessionVersion) {
    return;
  }
  try {
    await refresh();
  } catch (_error) {
    if (active && version === sessionVersion) {
      text('connection', '连接中断');
    }
  }
  if (active && version === sessionVersion) {
    pollTimer = setTimeout(() => poll(version), 3000);
  }
}

async function enterConsole(session) {
  const version = ++sessionVersion;
  csrf = session.csrf_token;
  active = false;
  clearTimeout(pollTimer);
  try {
    const [info, supported] = await Promise.all([api('device/info'), api('capabilities')]);
    capabilities = supported;
    text('version', info.version);
    text('architecture', info.architecture);
    text('kernel', info.kernel);
    text(
      'interfaces',
      info.interfaces.map((item) => `${item.name} / ${item.ipv4}`).join('，') ||
        '未发现活动 IPv4 接口',
    );
    await loadSettings();
    if (version !== sessionVersion) {
      throw new SessionChangedError();
    }
    active = true;
    $('login-view').classList.add('hidden');
    $('console-view').classList.remove('hidden');
    await poll(version);
  } catch (error) {
    if (version === sessionVersion) {
      loggedOut();
    }
    throw error;
  }
}

$('login-form').addEventListener('submit', async (event) => {
  event.preventDefault();
  const button = event.currentTarget.querySelector('button');
  button.disabled = true;
  text('login-error', '');
  sessionVersion += 1;
  try {
    const session = await api('auth/login', 'POST', {
      username: $('username').value,
      password: $('password').value,
    });
    $('password').value = '';
    await enterConsole(session);
  } catch (error) {
    if (!(error instanceof SessionChangedError)) {
      text('login-error', error.message);
    }
  } finally {
    button.disabled = false;
  }
});

$('logout').addEventListener('click', async () => {
  $('logout').disabled = true;
  let failure = '';
  try {
    await api('auth/logout', 'POST');
  } catch (error) {
    if (!(error instanceof SessionChangedError)) {
      failure = `退出请求未完成：${error.message}`;
    }
  } finally {
    loggedOut();
    text('login-error', failure);
    $('logout').disabled = false;
  }
});

for (const button of document.querySelectorAll('.nav-item')) {
  button.addEventListener('click', () => selectPage(button.dataset.page));
}

$('refresh').addEventListener('click', () => {
  refresh()
    .then(() => notify('状态已刷新'))
    .catch(showError);
});

for (const section of ['video', 'ai', 'snapshot']) {
  $(`${section}-form`).addEventListener('submit', async (event) => {
    event.preventDefault();
    const form = event.currentTarget;
    const button = form.querySelector('button[type=submit]');
    button.disabled = true;
    try {
      const task = await api(`${section}/config`, 'PUT', readForm(form));
      notify('配置已提交，正在等待设备应用…');
      const result = await waitTask(task);
      await loadSettings();
      await refresh();
      const message = result.rtsp_reconnect_required
        ? '视频配置已应用，请重新连接 RTSP。'
        : '配置已应用并保存。';
      notify(message);
      activity(message);
    } catch (error) {
      if (!(error instanceof SessionChangedError)) {
        showError(error);
        activity(`配置操作：${error.message}`);
      }
    } finally {
      button.disabled = false;
    }
  });
}

$('capture').addEventListener('click', async () => {
  const version = sessionVersion;
  $('capture').disabled = true;
  try {
    const task = await api('snapshots', 'POST');
    const result = await waitTask(task);
    const response = await fetch(result.url, { credentials: 'same-origin' });
    if (!response.ok) {
      throw new Error('截图已过期或下载失败');
    }
    const blob = await response.blob();
    if (version !== sessionVersion) {
      throw new SessionChangedError();
    }
    clearPreview();
    previewURL = URL.createObjectURL(blob);
    $('preview').src = previewURL;
    $('preview').classList.remove('hidden');
    $('preview-empty').classList.add('hidden');
    $('download').href = previewURL;
    $('download').download = `SmartIPC-${task.id}${result.mime === 'image/jpeg' ? '.jpg' : '.bmp'}`;
    $('download').classList.remove('hidden');
    text(
      'snapshot-meta',
      `${new Date().toLocaleString()} · ${result.mime} · ${(result.size / 1024).toFixed(1)} KiB`,
    );
    activity(`已生成截图 ${task.id}`);
  } catch (error) {
    showError(error);
  } finally {
    $('capture').disabled = false;
  }
});

async function motorCommand(command) {
  const task = await api('motor/commands', 'POST', command);
  await waitTask(task);
  await refresh();
  activity(command.action === 'stop' ? '电机停止命令已执行' : '电机移动命令已执行');
}

$('motor-form').addEventListener('submit', async (event) => {
  event.preventDefault();
  const command = readForm(event.currentTarget);
  command.direction = Number(command.direction);
  command.action = 'move';
  try {
    await motorCommand(command);
  } catch (error) {
    showError(error);
  }
});

$('motor-stop').addEventListener('click', () => {
  motorCommand({ action: 'stop' }).catch(showError);
});

$('password-form').addEventListener('submit', async (event) => {
  event.preventDefault();
  const form = event.currentTarget;
  const values = readForm(form);
  const button = form.querySelector('button');
  if (values.new_password !== values.confirm_password) {
    notify('两次输入的新密码不一致', true);
    return;
  }
  button.disabled = true;
  try {
    await api('auth/password', 'PUT', {
      current_password: values.current_password,
      new_password: values.new_password,
    });
    loggedOut();
    text('login-error', '密码已更新，请重新登录。');
  } catch (error) {
    showError(error);
  } finally {
    button.disabled = false;
  }
});

api('auth/session')
  .then(enterConsole)
  .catch((error) => {
    if (error instanceof SessionChangedError) {
      return;
    }
    loggedOut();
    if (error.status !== 401) {
      text('login-error', error.message);
    }
  });
