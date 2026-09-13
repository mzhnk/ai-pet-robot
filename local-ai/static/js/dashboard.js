/**
 * dashboard.js — polling UI for the PetRobot V1 developer dashboard.
 *
 * All control traffic goes Browser -> Local AI -> WebSocket -> ESP32.
 * Nothing here talks to the robot directly.
 */
(() => {
  "use strict";

  const $ = (sel) => document.querySelector(sel);

  // ---------------- state ----------------
  let lastLogTs = 0;
  let logFilter = "all";
  const allLogs = [];          // {ts,type,message}
  const MAX_LOG_LINES = 400;

  // ---------------- helpers ----------------
  const fmtTime = (ts) => {
    const d = new Date(ts * 1000);
    return d.toLocaleTimeString("id-ID", { hour12: false }) +
      "." + String(d.getMilliseconds()).padStart(3, "0");
  };

  const setText = (el, text, cls) => {
    el.textContent = text;
    el.classList.remove("ok", "bad", "warn");
    if (cls) el.classList.add(cls);
  };

  async function post(url, body) {
    try {
      const resp = await fetch(url, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body || {}),
      });
      return await resp.json();
    } catch (err) {
      console.error("POST failed", url, err);
      return { ok: false, error: String(err) };
    }
  }

  // ---------------- status polling ----------------
  async function refreshStatus() {
    let data;
    try {
      const resp = await fetch("/api/robot/status");
      data = await resp.json();
    } catch {
      setPill(false, "server unreachable");
      return;
    }
    renderStatus(data);
  }

  function setPill(on, label) {
    const pill = $("#pill-connection");
    pill.textContent = label;
    pill.className = "pill " + (on ? "pill-on" : "pill-off");
  }

  function renderWorld(r) {
    const w = r.world || {};
    const hasData = Object.keys(w).length > 0;

    // --- Vision ---
    if (hasData) {
      setText($("#stat-vision"), w.vision ? "ONLINE" : "OFFLINE", w.vision ? "ok" : "bad");
      setText($("#stat-person"), w.person ? "YES" : "no", w.person ? "ok" : "");
      setText($("#stat-zone"), w.person ? (w.zone || "—") : "—", w.person ? "warn" : "");

      // --- Distance ---
      if (typeof w.distance === "number") {
        setText($("#stat-distance"), `${w.distance.toFixed(1)} cm`,
          w.close ? "bad" : w.obstacle ? "warn" : "ok");
      } else {
        setText($("#stat-distance"), "no data", "warn");
      }
      setText($("#stat-obstacle"), w.obstacle ? (w.close ? "CLOSE!" : "YES") : "clear",
        w.close ? "bad" : w.obstacle ? "warn" : "ok");

      // --- IMU ---
      setText($("#stat-orient"), w.upright ? "UPRIGHT" : "TILTED/FALLEN",
        w.upright ? "ok" : "bad");
      setText($("#stat-motion"), w.motion || "—", w.motion === "shock" ? "warn" : "");
      setText($("#stat-tilt"), "—", "");

      // --- World summary ---
      const parts = [
        `person: ${w.person ? "yes" : "no"}`,
        `zone: ${w.zone || "-"}`,
        w.distance != null ? `distance: ${Number(w.distance).toFixed(1)} cm` : null,
        `obstacle: ${w.obstacle ? (w.close ? "close!" : "yes") : "no"}`,
        `upright: ${w.upright ? "yes" : "NO"}`,
        `motion: ${w.motion || "-"}`,
        `vision: ${w.vision ? "online" : "offline"}`,
      ].filter(Boolean);
      $("#world-summary").textContent = parts.join("  ·  ");
    } else {
      setText($("#stat-vision"), "NO DATA", "warn");
      setText($("#stat-person"), "—", "");
      setText($("#stat-zone"), "—", "");
      setText($("#stat-distance"), "—", "");
      setText($("#stat-obstacle"), "—", "");
      setText($("#stat-orient"), "—", "");
      setText($("#stat-motion"), "—", "");
      setText($("#stat-tilt"), "—", "");
    }
  }

  function renderStatus(data) {
    const r = data.robot || {};
    const b = data.brain || {};

    // WebSocket + robot status
    const wsUp = !!r.connected;
    const alive = !!r.alive;
    setText($("#stat-ws"), wsUp ? "CONNECTED" : "OFFLINE", wsUp ? "ok" : "bad");
    setPill(wsUp && alive, wsUp && alive ? "WS: ONLINE" : "WS: OFFLINE");
    setText($("#stat-alive"), alive ? "ALIVE" : "STALE", alive ? "ok" : "warn");

    // Wi-Fi + RSSI
    setText($("#stat-wifi"), r.wifi ? "CONNECTED" : "OFFLINE", r.wifi ? "ok" : "bad");
    const rssiEl = $("#stat-rssi");
    if (typeof r.rssi === "number" && r.wifi) {
      setText(rssiEl, `${r.rssi} dBm`, r.rssi > -60 ? "ok" : r.rssi > -75 ? "warn" : "bad");
    } else {
      setText(rssiEl, "—", "");
    }

    // Brain
    const brainEl = $("#stat-brain");
    if (!b.configured) {
      setText(brainEl, "NO API KEY", "warn");
    } else if (b.ok) {
      setText(brainEl, "READY", "ok");
    } else if (b.last_error) {
      setText(brainEl, "DEGRADED", "warn");
    } else {
      setText(brainEl, "IDLE", "");
    }
    const detail = [];
    if (b.last_provider) detail.push(`provider: ${b.last_provider}`);
    if (b.last_error) detail.push(`error: ${b.last_error.slice(0, 120)}`);
    detail.push(`models: ${(b.models || []).length}`);
    $("#brain-detail").textContent = detail.join("  ·  ");

    // Uptime + behavior
    const up = r.uptime_s || 0;
    const hh = Math.floor(up / 3600), mm = Math.floor((up % 3600) / 60), ss = up % 60;
    setText($("#stat-uptime"), `${hh}h ${mm}m ${ss}s`);
    setText($("#stat-fsm"), r.behavior_state || "—");
    setText($("#stat-emotion"), r.emotion || "—");

    const cmdEl = $("#last-command");
    if (r.last_command) {
      const c = r.last_command;
      cmdEl.textContent =
        `Last command: ${c.emotion || "-"}` +
        (c.animation ? ` / ${c.animation}` : "") +
        (c.movement ? ` / ${c.movement}` : "") +
        (c.speech ? ` — "${c.speech}"` : "");
      cmdEl.title = cmdEl.textContent;
    }

    renderWorld(r);
    updateCameraStream(r);
  }

  // ---------------- V2: camera stream proxy ----------------
  let cameraStreamActive = false;

  function updateCameraStream(r) {
    const img = $("#camera-stream");
    const offlineNote = $("#camera-offline");
    const wanted = !!(r.camera_ip && r.world && r.world.vision);

    if (wanted && !cameraStreamActive) {
      cameraStreamActive = true;
      offlineNote.hidden = true;
      img.hidden = false;
      img.src = `/api/robot/camera/stream?t=${Date.now()}`;
      img.onerror = () => {
        cameraStreamActive = false;
        img.hidden = true;
        img.src = "";
        offlineNote.hidden = false;
      };
    } else if (!wanted && cameraStreamActive) {
      cameraStreamActive = false;
      img.onerror = null;
      img.hidden = true;
      img.src = "";
      offlineNote.hidden = false;
    }
  }

  // ---------------- logs ----------------
  async function refreshLogs() {
    let data;
    try {
      const resp = await fetch(`/api/robot/logs?since=${lastLogTs}`);
      data = await resp.json();
    } catch {
      return;
    }
    for (const entry of data.logs || []) {
      allLogs.push(entry);
      if (entry.ts > lastLogTs) lastLogTs = entry.ts;
    }
    while (allLogs.length > MAX_LOG_LINES) allLogs.shift();
    renderLogs();
  }

  function renderLogs() {
    const list = $("#log-list");
    const visible = logFilter === "all" ? allLogs : allLogs.filter((e) => e.type === logFilter);
    list.innerHTML = visible.slice(-200).map((e) => (
      `<div class="log-line">` +
      `<span class="log-ts">${fmtTime(e.ts)}</span>` +
      `<span class="log-${e.type}">[${e.type}]</span> ` +
      `${escapeHtml(e.message)}` +
      `</div>`
    )).join("");
    list.scrollTop = list.scrollHeight;
  }

  function escapeHtml(s) {
    return s.replace(/[&<>"']/g, (c) => ({
      "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;",
    }[c]));
  }

  // ---------------- controls ----------------
  const speedPercent = () => Number($("#speed-range").value);

  function bindMovement() {
    document.querySelectorAll("button.mv").forEach((btn) => {
      btn.addEventListener("click", async () => {
        btn.disabled = true;
        await post("/api/robot/command", {
          movement: btn.dataset.mv,
          speed: speedPercent(),
        });
        setTimeout(() => (btn.disabled = false), 250);
      });
    });
    $("#speed-range").addEventListener("input", (e) => {
      $("#speed-value").textContent = `${e.target.value}%`;
    });
  }

  function bindEmotions() {
    document.querySelectorAll("button.emo").forEach((btn) => {
      btn.addEventListener("click", () =>
        post("/api/robot/command", { emotion: btn.dataset.emotion }));
    });
  }

  function bindAnimations() {
    document.querySelectorAll("button.ani").forEach((btn) => {
      btn.addEventListener("click", () =>
        post("/api/robot/command", { emotion: "neutral", animation: btn.dataset.anim }));
    });
  }

  function bindEvents() {
    document.querySelectorAll("button.evt").forEach((btn) => {
      btn.addEventListener("click", () =>
        post("/api/robot/event", { type: btn.dataset.event }));
    });
  }

  function bindLogFilters() {
    document.querySelectorAll(".chip").forEach((chip) => {
      chip.addEventListener("click", () => {
        document.querySelectorAll(".chip").forEach((c) => c.classList.remove("active"));
        chip.classList.add("active");
        logFilter = chip.dataset.filter;
        renderLogs();
      });
    });
  }

  // ---------------- boot ----------------
  bindMovement();
  bindEmotions();
  bindAnimations();
  bindEvents();
  bindLogFilters();
  refreshStatus();
  refreshLogs();
  setInterval(refreshStatus, 2000);
  setInterval(refreshLogs, 2000);
})();
