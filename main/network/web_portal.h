#pragma once

static const char PAGE_PORTAL_HTML[] = R"rawliteral(<!DOCTYPE html>
<html lang="vi">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>Cấu hình Wi-Fi</title>
  <style>
    :root {
      --canvas: #F7F6F3;
      --card: #FFFFFF;
      --subtle: #FBFBFA;
      --border: #EAEAEA;
      --border-dark: #D4D4D0;
      --text: #111111;
      --muted: #787774;
      --badge-green-bg: #EDF3EC;
      --badge-green-tx: #346538;
      --badge-yellow-bg: #FBF3DB;
      --badge-yellow-tx: #956400;
      --badge-red-bg: #FDEBEC;
      --badge-red-tx: #9F2F2D;
      --font-serif: Georgia, Cambria, 'Times New Roman', serif;
      --font-sans: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;
      --font-mono: SFMono-Regular, Consolas, 'Liberation Mono', Menlo, Courier, monospace;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      background: var(--canvas);
      color: var(--text);
      font-family: var(--font-sans);
      min-height: 100vh;
      display: flex;
      align-items: center;
      justify-content: center;
      padding: 24px 14px;
      line-height: 1.5;
      -webkit-font-smoothing: antialiased;
    }
    .bento-window {
      width: 100%;
      max-width: 440px;
      background: var(--card);
      border: 1px solid var(--border);
      border-radius: 8px;
      overflow: hidden;
      position: relative;
    }
    .window-header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      padding: 11px 16px;
      border-bottom: 1px solid var(--border);
      background: var(--subtle);
    }
    .window-dots { display: flex; gap: 5px; }
    .window-dot { width: 8px; height: 8px; border-radius: 50%; background: #D8D8D5; }
    .window-title {
      font-family: var(--font-mono);
      font-size: 11px;
      letter-spacing: 0.08em;
      text-transform: uppercase;
      color: var(--muted);
      font-weight: 600;
    }
    .badge {
      font-family: var(--font-mono);
      font-size: 10px;
      font-weight: 600;
      padding: 2px 7px;
      border-radius: 3px;
      letter-spacing: 0.05em;
    }
    .badge-yellow { background: var(--badge-yellow-bg); color: var(--badge-yellow-tx); }
    .badge-green { background: var(--badge-green-bg); color: var(--badge-green-tx); }
    .badge-red { background: var(--badge-red-bg); color: var(--badge-red-tx); }
    .bento-body { padding: 22px 18px; }
    .hero-title {
      font-family: var(--font-serif);
      font-size: 24px;
      font-weight: 600;
      letter-spacing: -0.02em;
      margin-bottom: 4px;
    }
    .hero-desc {
      font-size: 13px;
      color: var(--muted);
      margin-bottom: 20px;
    }
    .section-head {
      display: flex;
      align-items: center;
      justify-content: space-between;
      margin-bottom: 8px;
    }
    .section-label {
      font-family: var(--font-mono);
      font-size: 11px;
      text-transform: uppercase;
      letter-spacing: 0.06em;
      color: var(--muted);
    }
    .btn-action {
      background: var(--subtle);
      border: 1px solid var(--border);
      border-radius: 4px;
      color: var(--text);
      font-family: var(--font-mono);
      font-size: 11px;
      padding: 3px 8px;
      cursor: pointer;
    }
    .btn-action:active { background: #EEE; }
    .wifi-list {
      border: 1px solid var(--border);
      border-radius: 6px;
      background: var(--card);
      max-height: 160px;
      overflow-y: auto;
      margin-bottom: 18px;
    }
    .wifi-item {
      display: flex;
      align-items: center;
      justify-content: space-between;
      padding: 9px 12px;
      border-bottom: 1px solid var(--border);
      cursor: pointer;
      font-size: 13px;
    }
    .wifi-item:last-child { border-bottom: none; }
    .wifi-item:hover, .wifi-item.active { background: var(--subtle); }
    .wifi-item.active { font-weight: 600; border-left: 3px solid var(--text); }
    .wifi-rssi {
      font-family: var(--font-mono);
      font-size: 11px;
      color: var(--muted);
    }
    .field-group { margin-bottom: 14px; }
    .field-label {
      display: block;
      font-family: var(--font-mono);
      font-size: 11px;
      text-transform: uppercase;
      color: var(--muted);
      margin-bottom: 6px;
    }
    .field-input {
      display: flex;
      align-items: center;
      border: 1px solid var(--border-dark);
      border-radius: 5px;
      background: var(--card);
      padding: 0 10px;
    }
    .field-input:focus-within { border-color: var(--text); }
    .field-input input {
      width: 100%;
      border: none;
      outline: none;
      background: transparent;
      padding: 10px 0;
      font-family: var(--font-mono);
      font-size: 13px;
      color: var(--text);
    }
    .btn-toggle {
      background: var(--subtle);
      border: 1px solid var(--border);
      border-radius: 3px;
      font-family: var(--font-mono);
      font-size: 10px;
      padding: 3px 6px;
      cursor: pointer;
      color: var(--muted);
    }
    .btn-submit {
      width: 100%;
      background: var(--text);
      color: #FFF;
      border: none;
      border-radius: 5px;
      padding: 11px;
      font-family: var(--font-mono);
      font-size: 12px;
      font-weight: 600;
      letter-spacing: 0.04em;
      text-transform: uppercase;
      cursor: pointer;
      margin-top: 6px;
    }
    .btn-submit:active { opacity: 0.9; }
    .footer-meta {
      display: flex;
      justify-content: space-between;
      margin-top: 18px;
      padding-top: 12px;
      border-top: 1px solid var(--border);
      font-family: var(--font-mono);
      font-size: 10px;
      color: #A0A09C;
    }
    .curtain {
      position: absolute;
      top: 38px;
      left: 0;
      right: 0;
      bottom: 0;
      background: rgba(255, 255, 255, 0.96);
      display: none;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      padding: 24px;
      text-align: center;
      backdrop-filter: blur(2px);
    }
    .curtain.active { display: flex; }
    .spinner {
      width: 24px;
      height: 24px;
      border: 2px solid var(--border-dark);
      border-top-color: var(--text);
      border-radius: 50%;
      animation: spin 700ms linear infinite;
      margin-bottom: 14px;
    }
    @keyframes spin { 100% { transform: rotate(360deg); } }
    .curtain-title { font-family: var(--font-serif); font-size: 20px; font-weight: 600; margin-bottom: 6px; }
    .curtain-desc { font-size: 12px; color: var(--muted); margin-bottom: 12px; line-height: 1.4; }
    .ip-box {
      font-family: var(--font-mono);
      font-size: 12px;
      background: var(--subtle);
      border: 1px solid var(--border);
      padding: 4px 10px;
      border-radius: 4px;
      margin-top: 6px;
    }
  </style>
</head>
<body>
  <div class="bento-window">
    <div class="window-header">
      <div class="window-dots">
        <span class="window-dot"></span>
        <span class="window-dot"></span>
        <span class="window-dot"></span>
      </div>
      <span class="window-title">NFC School Auth</span>
      <span class="badge badge-yellow" id="badge-portal">PORTAL</span>
    </div>

    <div class="bento-body">
      <h1 class="hero-title">Cấu hình Wi-Fi</h1>
      <p class="hero-desc">Cài đặt kết nối Wi-Fi cho thiết bị điểm danh.</p>

      <div class="section-head">
        <span class="section-label">Mạng khả dụng</span>
        <button type="button" class="btn-action" id="btn-scan" onclick="scanWifi()">Quét lại</button>
      </div>

      <div class="wifi-list" id="wifi-list">
        <div style="padding: 14px; text-align: center; font-size: 12px; color: var(--muted); font-family: var(--font-mono);">
          Đang quét danh sách mạng...
        </div>
      </div>

      <form id="cfg-form" onsubmit="handleConnect(event)">
        <div class="field-group">
          <label class="field-label" for="ssid">Tên mạng (SSID)</label>
          <div class="field-input">
            <input type="text" id="ssid" placeholder="Nhập hoặc chọn mạng" required autocomplete="off">
          </div>
        </div>

        <div class="field-group">
          <label class="field-label" for="pass">Mật khẩu</label>
          <div class="field-input">
            <input type="password" id="pass" placeholder="Mật khẩu Wi-Fi">
            <button type="button" class="btn-toggle" onclick="togglePass()">HIỆN</button>
          </div>
        <div class="field-group" style="margin-top: 18px; padding-top: 14px; border-top: 1px dashed var(--border);">
          <div class="section-head" style="margin-bottom: 6px;">
            <span class="section-label">Máy chủ Backend</span>
            <span style="font-size: 10px; color: var(--muted); font-family: var(--font-mono);">mDNS / IP</span>
          </div>
          <div class="field-input" style="margin-bottom: 8px;">
            <input type="text" id="server_host" placeholder="VD: khang-ubuntu.local hoặc 192.168.0.222" autocomplete="off">
          </div>
          <div style="display: flex; gap: 8px;">
            <div class="field-input" style="flex: 1;">
              <input type="number" id="tcp_port" placeholder="TCP (1337)" min="1" max="65535">
            </div>
            <div class="field-input" style="flex: 2;">
              <input type="password" id="auth_token" placeholder="Token bảo mật">
            </div>
          </div>
        </div>

        <div class="field-group" style="margin-top: 14px; padding-top: 12px; border-top: 1px dashed var(--border);">
          <div class="section-head" style="margin-bottom: 6px;">
            <span class="section-label">Thời gian quẹt thẻ (NFC)</span>
            <span style="font-size: 10px; color: var(--muted); font-family: var(--font-mono);">Mili-giây (ms)</span>
          </div>
          <div style="display: flex; gap: 8px;">
            <div class="field-input" style="flex: 1;">
              <input type="number" id="scan_cooldown_ms" placeholder="Chờ quẹt lại (200)" min="50" max="5000">
            </div>
            <div class="field-input" style="flex: 1;">
              <input type="number" id="feedback_duration_ms" placeholder="LED / Còi (200)" min="50" max="5000">
            </div>
          </div>
        </div>

        <button type="submit" class="btn-submit" id="btn-submit">Lưu và kết nối</button>
      </form>

      <div class="footer-meta">
        <span>GATEWAY 192.168.4.1</span>
        <span>mDNS: khang-ubuntu.local</span>
      </div>
    </div>

    <!-- Status Curtain -->
    <div class="curtain" id="curtain">
      <div class="spinner" id="curtain-spinner"></div>
      <h2 class="curtain-title" id="curtain-title">Đang kết nối...</h2>
      <p class="curtain-desc" id="curtain-desc">Đang gửi cấu hình tới ESP32.</p>
      <div id="curtain-ip" style="display: none;" class="ip-box"></div>
      <button type="button" class="btn-action" id="btn-retry" style="display: none; margin-top: 12px; padding: 6px 14px;" onclick="closeCurtain()">Thử lại</button>
    </div>
  </div>

  <script>
    function togglePass() {
      const p = document.getElementById('pass');
      p.type = (p.type === 'password') ? 'text' : 'password';
    }

    function selectWifi(name) {
      document.getElementById('ssid').value = name;
      document.getElementById('pass').focus();
      document.querySelectorAll('.wifi-item').forEach(el => {
        el.classList.toggle('active', el.dataset.ssid === name);
      });
    }

    async function scanWifi() {
      const btn = document.getElementById('btn-scan');
      const box = document.getElementById('wifi-list');
      btn.innerText = 'Đang quét...';
      try {
        const res = await fetch('/api/scan');
        const data = await res.json();
        btn.innerText = 'Quét lại';
        if (!data || data.length === 0) {
          box.innerHTML = '<div style="padding: 14px; text-align: center; font-size: 12px; color: var(--muted); font-family: var(--font-mono);">Không tìm thấy mạng.</div>';
          return;
        }
        box.innerHTML = data.map(ap => `
          <div class="wifi-item" data-ssid="${ap.ssid}" onclick="selectWifi('${ap.ssid.replace(/'/g, "\\'")}')">
            <span>${ap.ssid}</span>
            <span class="wifi-rssi">${ap.rssi} dBm</span>
          </div>
        `).join('');
      } catch (e) {
        btn.innerText = 'Quét lại';
      }
    }

    async function handleConnect(e) {
      e.preventDefault();
      const ssid = document.getElementById('ssid').value.trim();
      const pass = document.getElementById('pass').value;
      if (!ssid) return;

      const c = document.getElementById('curtain');
      const sp = document.getElementById('curtain-spinner');
      const ti = document.getElementById('curtain-title');
      const de = document.getElementById('curtain-desc');
      const ip = document.getElementById('curtain-ip');
      const re = document.getElementById('btn-retry');

      c.classList.add('active');
      sp.style.display = 'block';
      ip.style.display = 'none';
      re.style.display = 'none';
      ti.innerText = 'Đang kết nối...';
      de.innerText = 'Gửi cấu hình tới chip và chờ nhận địa chỉ IP.';

      const serverHost = document.getElementById('server_host').value.trim();
      const tcpPort = document.getElementById('tcp_port').value.trim();
      const authToken = document.getElementById('auth_token').value.trim();
      const scanCooldown = document.getElementById('scan_cooldown_ms').value.trim();
      const feedbackDur = document.getElementById('feedback_duration_ms').value.trim();

      const bodyData = { ssid: ssid, password: pass };
      if (serverHost) bodyData.server_host = serverHost;
      if (tcpPort) bodyData.tcp_port = tcpPort;
      if (authToken) bodyData.auth_token = authToken;
      if (scanCooldown) bodyData.scan_cooldown_ms = scanCooldown;
      if (feedbackDur) bodyData.feedback_duration_ms = feedbackDur;

      try {
        await fetch('/api/connect', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(bodyData)
        });

        let count = 0;
        const timer = setInterval(async () => {
          count++;
          try {
            const sRes = await fetch('/api/status');
            const sData = await sRes.json();
            if (sData.connected) {
              clearInterval(timer);
              sp.style.display = 'none';
              ti.innerText = 'Kết nối thành công!';
              de.innerText = 'Đã kết nối vào ' + sData.ssid + '. ESP32 đang khởi động lại để kích hoạt hệ thống và tắt Wi-Fi cài đặt...';
              ip.style.display = 'block';
              ip.innerText = 'Địa chỉ IP: ' + sData.ip;
              document.getElementById('badge-portal').className = 'badge badge-green';
              document.getElementById('badge-portal').innerText = 'KHỞI ĐỘNG LẠI...';
            } else if (sData.status === 'failed') {
              clearInterval(timer);
              sp.style.display = 'none';
              ti.innerText = 'Kết nối thất bại';
              de.innerText = 'Mật khẩu sai hoặc không thể liên lạc với router.';
              re.style.display = 'inline-block';
            } else if (count > 14) {
              clearInterval(timer);
              sp.style.display = 'none';
              ti.innerText = 'Hết thời gian chờ';
              de.innerText = 'Vui lòng kiểm tra lại router hoặc thử lại.';
              re.style.display = 'inline-block';
            }
          } catch (err) {
            if (count > 12) {
              clearInterval(timer);
              sp.style.display = 'none';
              ti.innerText = 'Đã lưu cấu hình';
              de.innerText = 'ESP32 đang kết nối vào mạng chính.';
            }
          }
        }, 1200);

      } catch (err) {
        sp.style.display = 'none';
        ti.innerText = 'Lỗi gửi yêu cầu';
        de.innerText = err.message;
        re.style.display = 'inline-block';
      }
    }

    function closeCurtain() {
      document.getElementById('curtain').classList.remove('active');
    }

    async function loadCurrentConfig() {
      try {
        const res = await fetch('/api/config');
        const cfg = await res.json();
        if (cfg.wifi && cfg.wifi.ssid) {
          document.getElementById('ssid').value = cfg.wifi.ssid;
        }
        if (cfg.server) {
          if (cfg.server.host) document.getElementById('server_host').value = cfg.server.host;
          if (cfg.server.tcp_port) document.getElementById('tcp_port').value = cfg.server.tcp_port;
          if (cfg.server.auth_token) document.getElementById('auth_token').value = cfg.server.auth_token;
        }
        if (cfg.scan) {
          if (cfg.scan.cooldown_ms) document.getElementById('scan_cooldown_ms').value = cfg.scan.cooldown_ms;
          if (cfg.scan.feedback_duration_ms) document.getElementById('feedback_duration_ms').value = cfg.scan.feedback_duration_ms;
        }
      } catch (e) {}
    }

    window.addEventListener('load', () => {
      loadCurrentConfig();
      setTimeout(scanWifi, 1200);
    });
  </script>
</body>
</html>)rawliteral";
