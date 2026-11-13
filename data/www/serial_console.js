(function () {
  "use strict";

  const endpoint = "/api/serial-console";
  const lines = [];
  const history = [];
  let panel, screen, connection, input, sendButton, pauseButton;
  let historyIndex = 0, draft = "";
  let after = 0, boot = 0, timer = null, delay = 1000;
  let paused = false, reading = false, sending = false;
  let clockFormat;

  function active() {
    return panel && panel.isConnected && page === "diagnostic" && !document.hidden;
  }

  function stamp(row) {
    if (row.epochMs) {
      if (!clockFormat) {
        clockFormat = new Intl.DateTimeFormat("fr-FR", {
          hour: "2-digit", minute: "2-digit", second: "2-digit", hour12: false
        });
      }
      return clockFormat.format(new Date(row.epochMs)) + "." + String(row.epochMs % 1000).padStart(3, "0");
    }
    return "+" + (Number(row.ms) / 1000).toFixed(3) + " s";
  }

  function append(text) {
    lines.push(text);
    if (lines.length > 240) lines.splice(0, lines.length - 240);
  }

  function draw() {
    screen.textContent = lines.join("\n");
    screen.scrollTop = screen.scrollHeight;
  }

  function schedule(ms) {
    if (timer !== null) clearTimeout(timer);
    timer = null;
    if (!active() || paused || reading) return;
    timer = setTimeout(poll, ms);
  }

  async function request(url, options, decode) {
    const abort = new AbortController();
    const timeout = setTimeout(() => abort.abort(), 3500);
    try {
      const response = await fetch(url, {
        ...options, credentials: "same-origin", cache: "no-store", signal: abort.signal
      });
      if (!response.ok) {
        const message = (await response.text()).trim();
        throw new Error(message || "HTTP " + response.status);
      }
      return await decode(response);
    } finally {
      clearTimeout(timeout);
    }
  }

  async function poll() {
    timer = null;
    if (!active() || paused || reading) return;
    reading = true;
    try {
      const batch = await request(endpoint + "?boot=" + boot + "&after=" + after, {}, response => response.json());
      if (!active() || paused) return;
      if (!Number.isInteger(batch.boot) || !Number.isInteger(batch.next) || !Array.isArray(batch.lines)) {
        throw new Error("Réponse console invalide.");
      }
      if (boot && boot !== batch.boot) append("── Redémarrage de l’ESP32 ──");
      if (batch.lost) append("── " + batch.lost + " lignes sorties de l’historique ESP32 ──");
      for (const row of batch.lines) {
        append("[" + stamp(row) + "] " + row.text + (row.cut ? " … [tronqué]" : ""));
      }
      boot = batch.boot;
      after = batch.next;
      connection.textContent = "Connectée";
      delay = batch.more === true || batch.lines.length >= 4 ? 240 : 1000;
      draw();
    } catch (error) {
      if (!paused) connection.textContent = "Reconnexion · " + error.message;
      delay = Math.min(Math.max(delay, 1000) * 2, 8000);
    } finally {
      reading = false;
      // Separate retry times when several browsers reconnect together.
      schedule(delay + (delay > 1000 ? Math.floor(Math.random() * 400) : 0));
    }
  }

  async function submit(event) {
    event.preventDefault();
    const command = input.value.trim();
    if (!command || command.length > 47 || sending) return;
    history.push(command);
    if (history.length > 32) history.shift();
    historyIndex = history.length;
    draft = "";
    input.value = "";
    sending = true;
    sendButton.disabled = true;
    append("> " + command);
    draw();
    try {
      const reply = await request(endpoint + "/command", {
        method: "POST",
        headers: {"Content-Type": "text/plain", "X-Serial-Console": "1"},
        body: command
      }, response => response.text());
      append(reply);
    } catch (error) {
      append("! " + error.message);
    } finally {
      sending = false;
      sendButton.disabled = false;
      draw();
      schedule(240);
    }
  }

  function makePanel() {
    panel = document.createElement("section");
    panel.className = "panel serialConsole";
    panel.innerHTML =
      '<h2>Console série</h2>' +
      '<div class="toolbar"><button type="button" data-pause aria-pressed="false">Pause</button>' +
      '<button type="button" data-clear>Effacer l’écran</button>' +
      '<span class="muted" role="status" data-connection>Connexion…</span></div>' +
      '<pre tabindex="0" aria-label="Journal série" data-screen></pre>' +
      '<form class="serialConsoleForm"><input type="text" maxlength="47" autocomplete="off" ' +
      'autocapitalize="none" spellcheck="false" aria-label="Commande série" placeholder="help">' +
      '<button type="submit">Envoyer</button></form>' +
      '<p class="muted">Commandes USB/Web : help, status, heap, uptime, version, debug help. Wi-Fi : wifi et wifi history. Bilans : debug status, debug peers, debug events ; debug on / off.</p>'; 
    screen = panel.querySelector("[data-screen]");
    connection = panel.querySelector("[data-connection]");
    pauseButton = panel.querySelector("[data-pause]");
    input = panel.querySelector("input");
    sendButton = panel.querySelector("button[type=submit]");
    panel.querySelector("form").addEventListener("submit", submit);
    panel.querySelector("[data-clear]").addEventListener("click", () => {
      lines.length = 0;
      draw();
    });
    pauseButton.addEventListener("click", () => {
      paused = !paused;
      pauseButton.textContent = paused ? "Reprendre" : "Pause";
      pauseButton.setAttribute("aria-pressed", String(paused));
      connection.textContent = paused ? "Affichage en pause" : "Connexion…";
      schedule(0);
    });
    input.addEventListener("keydown", event => {
      if (!history.length || (event.key !== "ArrowUp" && event.key !== "ArrowDown")) return;
      event.preventDefault();
      if (historyIndex === history.length) draft = input.value;
      historyIndex = Math.max(0, Math.min(history.length, historyIndex + (event.key === "ArrowUp" ? -1 : 1)));
      input.value = historyIndex === history.length ? draft : history[historyIndex];
    });
  }

  window.mountSerialConsole = function (slot) {
    if (!slot) return;
    if (!panel) makePanel();
    slot.appendChild(panel);
    schedule(0);
  };
  document.addEventListener("visibilitychange", () => schedule(0));
})();
