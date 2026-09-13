/** chat.js — minimal chat client for the Local AI web UI. */
(() => {
  "use strict";
  const log = document.querySelector("#chat-log");
  const form = document.querySelector("#chat-form");
  const input = document.querySelector("#chat-input");
  const send = document.querySelector("#chat-send");

  const addMsg = (text, cls) => {
    const div = document.createElement("div");
    div.className = "msg " + cls;
    div.textContent = text;
    log.appendChild(div);
    log.scrollTop = log.scrollHeight;
  };

  form.addEventListener("submit", async (e) => {
    e.preventDefault();
    const message = input.value.trim();
    if (!message) return;

    addMsg(message, "user");
    input.value = "";
    send.disabled = true;

    try {
      const resp = await fetch("/api/chat", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ message }),
      });
      const data = await resp.json();
      if (data.ok) {
        addMsg(`${data.reply}\n\n— via ${data.provider}`, "bot");
      } else {
        addMsg(data.error || "Terjadi kesalahan.", "err");
      }
    } catch (err) {
      addMsg("Tidak bisa menghubungi server Local AI.", "err");
    } finally {
      send.disabled = false;
      input.focus();
    }
  });

  input.focus();
})();
