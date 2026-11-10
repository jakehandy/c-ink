// C-Ink Settings UI. Plain JS on Tauri's global API; every change saves.
const tauri = window.__TAURI__;
const invoke = (cmd, args) => tauri.core.invoke(cmd, args);
const openUrl = (url) => tauri.opener.openUrl(url);
const $ = (s) => document.querySelector(s);
const $$ = (s) => [...document.querySelectorAll(s)];

let state = null;      // from get_state
let settings = null;   // working copy
let view = { screen: 0, index: 0 };
let saveTimer = null;

// ---------------------------------------------------------------- helpers
function el(tag, attrs = {}, ...kids) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (k === "class") e.className = v;
    else if (k.startsWith("on")) e.addEventListener(k.slice(2), v);
    else if (v !== false && v != null) e.setAttribute(k, v === true ? "" : v);
  }
  for (const k of kids.flat()) e.append(k instanceof Node ? k : document.createTextNode(k ?? ""));
  return e;
}
function ago(ts) {
  if (!ts) return "never";
  const s = Date.now() / 1000 - ts;
  if (s < 90) return "just now";
  if (s < 3600) return `${Math.round(s / 60)} min ago`;
  if (s < 86400) return `${Math.round(s / 3600)} h ago`;
  return `${Math.round(s / 86400)} d ago`;
}
function clock(ts) {
  return ts ? new Date(ts * 1000).toLocaleTimeString([], { hour: "numeric", minute: "2-digit" }) : "-";
}
function note(sel, text, ok) {
  const n = $(sel);
  n.hidden = !text;
  n.textContent = text || "";
  n.className = "note " + (ok ? "ok" : "bad");
}
function save() {
  clearTimeout(saveTimer);
  saveTimer = setTimeout(async () => {
    try { await invoke("save_settings", { settings }); } catch (e) { console.error(e); }
    renderPreview();
  }, 350);
}
async function busy(btn, label, fn) {
  const old = btn.textContent;
  btn.disabled = true;
  btn.textContent = label;
  try { return await fn(); } finally { btn.disabled = false; btn.textContent = old; }
}

// ------------------------------------------------------------- navigation
$$(".nav button").forEach((b) =>
  b.addEventListener("click", () => {
    $$(".nav button").forEach((x) => x.classList.toggle("active", x === b));
    $$(".page").forEach((p) => p.classList.toggle("active", p.id === "page-" + b.dataset.page));
    if (b.dataset.page === "claude") loadClaude();
    if (b.dataset.page === "device") loadPorts();
  })
);
document.addEventListener("click", (e) => {
  const a = e.target.closest("[data-open]");
  if (a) { e.preventDefault(); openUrl(a.dataset.open); }
});

// ------------------------------------------------------------------ today
async function renderPreview() {
  try {
    const r = await invoke("render_preview", { screen: view.screen === 0 ? (view.index ? 1 : 0) : view.screen, index: view.index, paper: $("#paper").checked });
    $("#frame").src = r.image;
    view.cards = r.cards;
    view.nextChange = r.next_change;
    renderFacts();
  } catch (e) {
    console.error(e);
  }
}
function renderFacts() {
  const st = state.status;
  const f = [
    [st.running ? "Syncing..." : ago(st.generated), "last digest"],
    [st.cards ?? 0, "cards"],
    [st.running ? "-" : clock(st.next_run), "next sync"],
    [view.nextChange || "-", "screen changes"],
  ];
  $("#facts").replaceChildren(...f.map(([b, s]) => el("div", { class: "fact" }, el("b", {}, String(b)), el("span", {}, s))));
  $("#headline").textContent = st.headline || "Your TICKEY shows the most relevant card. Use the arrows to see the rest.";
  const errBox = $("#run-error");
  errBox.hidden = !st.last_error;
  errBox.textContent = st.last_error ? "Last sync failed: " + st.last_error : "";
  $("#sync-btn").disabled = st.running;
  $("#sync-btn").textContent = st.running ? "Syncing..." : "Sync now";
  const devs = settings.devices.length;
  $("#nav-status").replaceChildren(
    el("div", {}, el("span", { class: "dot " + (st.last_error ? "bad" : st.generated ? "ok" : "") }), st.last_error ? "Sync failing" : st.generated ? `Synced ${ago(st.generated)}` : "Not synced yet"),
    el("div", {}, el("span", { class: "dot " + (st.serving ? "ok" : "bad") }), st.serving ? `Serving on ${state.host}` : "Not serving"),
    el("div", {}, el("span", { class: "dot " + (devs ? "ok" : "") }), devs ? `${devs} device${devs > 1 ? "s" : ""} paired` : "No device yet")
  );
}
$("#prev").onclick = () => { view.screen = 0; view.index -= 1; renderPreview(); };
$("#next").onclick = () => { view.screen = 0; view.index += 1; renderPreview(); };
$("#paper").onchange = renderPreview;
$$("#screen-seg button").forEach((b) =>
  b.addEventListener("click", () => {
    $$("#screen-seg button").forEach((x) => x.classList.toggle("on", x === b));
    view.screen = +b.dataset.screen;
    view.index = 0;
    renderPreview();
  })
);
$("#sync-btn").onclick = async () => {
  state.status.running = true;
  renderFacts();
  try {
    state.status = await invoke("sync_now");
  } catch (e) {
    state.status.last_error = String(e);
  }
  state.status.running = false;
  view.index = 0;
  await refreshState();
  renderPreview();
};

// ----------------------------------------------------------------- claude
async function loadClaude() {
  const card = $("#claude-card");
  const st = await invoke("claude_status");
  if (!st.installed) {
    card.replaceChildren(
      el("h3", {}, "Install Claude Code"),
      el("p", { class: "hint" }, "C-Ink needs the Claude Code command line app. Install it, then come back here."),
      el("div", { class: "actions" },
        el("button", { class: "primary", "data-open": "https://docs.claude.com/en/docs/claude-code/setup" }, "Open install guide"),
        el("button", { onclick: loadClaude }, "Check again"))
    );
    return;
  }
  card.replaceChildren(
    el("div", { class: "status-line" },
      el("span", { class: "dot " + (st.logged_in ? "ok" : "bad") }),
      el("div", { class: "grow" },
        el("b", {}, st.logged_in ? "Signed in to Claude" : "Not signed in"),
        el("div", { class: "hint" }, `${st.version || "Claude Code"}  ·  ${st.path}`)),
      st.logged_in
        ? el("button", { onclick: loadClaude }, "Refresh")
        : el("button", { class: "primary", onclick: signIn }, "Sign in with Claude"))
  );
}
async function signIn(e) {
  await invoke("claude_login");
  e.target.textContent = "Finish in your browser...";
  e.target.disabled = true;
  for (let i = 0; i < 90; i++) {
    await new Promise((r) => setTimeout(r, 2000));
    const st = await invoke("claude_status");
    if (st.logged_in) { loadClaude(); recheck(); return; }
  }
  loadClaude();
}
$$("#model-seg button").forEach((b) =>
  b.addEventListener("click", () => {
    settings.model = b.dataset.model;
    $$("#model-seg button").forEach((x) => x.classList.toggle("on", x === b));
    save();
  })
);
$("#about").oninput = (e) => { settings.about = e.target.value; save(); };
$("#commute-from").oninput = (e) => { settings.commute_from = e.target.value; save(); };
$("#commute-mode").onchange = (e) => { settings.commute_mode = e.target.value; save(); };
$("#commute-buffer").oninput = (e) => {
  const n = parseInt(e.target.value, 10);
  if (n >= 0 && n <= 60) { settings.commute_buffer_min = n; save(); }
};

// ---------------------------------------------------------------- screens
function renderScreens() {
  const list = $("#screen-list");
  list.replaceChildren(
    ...state.screens.map((s) => {
      let pill;
      if (s.derived) pill = el("span", { class: "pill" }, "Built from the others");
      else if (s.connected.length) pill = el("span", { class: "pill ok" }, s.connected.join(", "));
      else if (s.gh_fallback) pill = el("span", { class: "pill ok" }, "GitHub CLI");
      else pill = el("a", { class: "pill warn", href: "#", "data-open": "https://claude.ai/settings/connectors" }, `Connect ${s.needs[0]}`);
      const box = el("input", { type: "checkbox", checked: settings.screens[s.key] });
      box.onchange = () => { settings.screens[s.key] = box.checked; save(); };
      return el("div", { class: "item" },
        el("div", { class: "grow" }, el("div", { class: "name" }, s.name), el("div", { class: "blurb" }, s.blurb)),
        pill,
        el("label", { class: "switch" }, box, el("span")));
    })
  );
}
async function recheck() {
  await busy($("#recheck"), "Checking...", async () => {
    try { await invoke("refresh_connections"); } catch (e) { alert("Could not check connections: " + e); }
    await refreshState();
    renderScreens();
  });
}
$("#recheck").onclick = recheck;

// ----------------------------------------------------------------- device
const hours = [...Array(24).keys()];
const hourLabel = (h) => `${h % 12 || 12} ${h < 12 ? "AM" : "PM"}`;
for (const id of ["#quiet-a", "#quiet-b"]) $(id).replaceChildren(...hours.map((h) => el("option", { value: h }, hourLabel(h))));

function renderDeviceSettings() {
  $("#refresh").value = settings.refresh_min;
  $("#refresh-val").textContent = `${settings.refresh_min} min`;
  $("#quiet-on").checked = !!settings.quiet;
  const [qa, qb] = settings.quiet || [23, 7];
  $("#quiet-a").value = qa;
  $("#quiet-b").value = qb;
  $("#quiet-a").disabled = $("#quiet-b").disabled = !settings.quiet;
  $("#mono").checked = settings.mono;
  $("#flip").checked = settings.flip;
  const devs = $("#devices");
  devs.replaceChildren(
    ...(settings.devices.length
      ? settings.devices.map((d) =>
          el("div", { class: "item" },
            el("div", { class: "grow" },
              el("div", { class: "name" }, d.name),
              el("div", { class: "blurb" }, `${d.id}  ·  firmware ${d.firmware || "?"}  ·  last fetched ${ago(state.status.last_seen?.[d.id])}`)),
            el("button", { class: "ghost", onclick: async () => {
              if (!confirm(`Forget ${d.name}? It will stop receiving digests until you set it up again.`)) return;
              await invoke("forget_device", { id: d.id });
              await refreshState();
              renderDeviceSettings();
            } }, "Forget")))
      : [el("p", { class: "hint" }, "None yet. Plug one in and use Set up above.")])
  );
}
$("#refresh").oninput = (e) => { settings.refresh_min = +e.target.value; $("#refresh-val").textContent = `${e.target.value} min`; save(); };
$("#quiet-on").onchange = (e) => { settings.quiet = e.target.checked ? [+$("#quiet-a").value, +$("#quiet-b").value] : null; renderDeviceSettings(); save(); };
$("#quiet-a").onchange = $("#quiet-b").onchange = () => { settings.quiet = [+$("#quiet-a").value, +$("#quiet-b").value]; save(); };
$("#mono").onchange = (e) => { settings.mono = e.target.checked; save(); };
$("#flip").onchange = (e) => { settings.flip = e.target.checked; save(); };
$("#autostart").onchange = async (e) => {
  try { await invoke("set_autostart", { on: e.target.checked }); } catch (err) { e.target.checked = !e.target.checked; alert(err); }
};

function wifiRow(ssid = "", pass = "") {
  const row = el("div", { class: "row wifi" },
    el("label", {}, "Wi-Fi"),
    el("input", { placeholder: "Network name", value: ssid, class: "ssid" }),
    el("input", { type: "password", placeholder: "Password", value: pass, class: "pass" }),
    el("button", { class: "ghost", onclick: () => row.remove() }, "Remove"));
  return row;
}
$("#wifi-rows").append(wifiRow());
$("#wifi-add").onclick = () => { if ($$("#wifi-rows .wifi").length < 5) $("#wifi-rows").append(wifiRow()); };

async function loadPorts() {
  const ports = await invoke("list_ports");
  const sel = $("#port");
  sel.replaceChildren(
    ...(ports.length
      ? ports.map((p) => el("option", { value: p.name }, `${p.likely_tickey ? "TICKEY? " : ""}${p.label} (${p.name})`))
      : [el("option", { value: "" }, "No USB device found. Plug in your TICKEY")])
  );
  try { $("#autostart").checked = await invoke("get_autostart"); } catch (_) {}
}
$("#ports-refresh").onclick = loadPorts;
const port = () => $("#port").value;

$("#setup").onclick = (e) =>
  busy(e.target, "Talking to TICKEY...", async () => {
    if (!port()) return note("#setup-msg", "Plug in your TICKEY and press Refresh first.", false);
    const wifi = $$("#wifi-rows .wifi").map((r) => ({ ssid: r.querySelector(".ssid").value, pass: r.querySelector(".pass").value })).filter((w) => w.ssid.trim());
    if (!wifi.length && !settings.relay?.url) return note("#setup-msg", "Add at least one Wi-Fi network.", false);
    try {
      const r = await invoke("setup_device", { port: port(), name: $("#dev-name").value, wifi });
      note("#setup-msg", `${r.name} is set up.${r.pushed ? " Today's digest is already on it." : " It will sync as soon as it joins Wi-Fi."}`, true);
      $$("#wifi-rows .pass").forEach((p) => (p.value = ""));
      await refreshState();
      renderDeviceSettings();
    } catch (err) {
      note("#setup-msg", String(err), false);
    }
  });
$("#update-dev").onclick = (e) =>
  busy(e.target, "Updating...", async () => {
    try {
      const st = await invoke("update_device", { port: port() });
      note("#setup-msg", `Updated. Battery ${st.battery < 0 ? "unknown" : st.battery + "%"}, ${st.networks} Wi-Fi network(s) saved, ${st.cards} cards on device.`, true);
    } catch (err) {
      note("#setup-msg", String(err), false);
    }
  });
// ---- hardware bring-up
function hwProfile() {
  const t = $("#hw-profile").value.trim();
  if (!t) return { panel: { type: "none" } };
  return JSON.parse(t); // throws with a readable message on bad JSON
}
function hwShow(p) { $("#hw-profile").value = JSON.stringify(p, null, 1); }
$("#hw-load").onclick = () => $("#hw-file").click();
$("#hw-file").onchange = async (e) => {
  const f = e.target.files[0];
  if (!f) return;
  try {
    const p = JSON.parse(await f.text());
    hwShow(p.profile || p); // accepts probe.py's full report or a bare profile
    note("#hw-msg", "Loaded " + f.name + ". Check it, then Send to TICKEY.", true);
  } catch (err) { note("#hw-msg", "That file is not a profile: " + err, false); }
  e.target.value = "";
};
$("#hw-read").onclick = (e) =>
  busy(e.target, "Reading...", async () => {
    try {
      const r = await invoke("device_board_get", { port: port() });
      note("#hw-msg", "Running the " + r.backend + " display backend. Pins: " + JSON.stringify(r.profile), true);
    } catch (err) { note("#hw-msg", String(err), false); }
  });
$("#hw-learn").onclick = (e) =>
  busy(e.target, "Press Up, Down, Back...", async () => {
    const activeLow = !$("#hw-active-high").checked;
    note("#hw-msg", "Press Up, then Down, then Back on the TICKEY, once each, within 15 seconds.", true);
    try {
      const r = await invoke("device_learn", { port: port(), secs: 15, activeLow });
      if (r.up == null) throw new Error("No button presses seen. Try again, or tick 'Buttons pull the line high'.");
      let p;
      try { p = hwProfile(); } catch (_) { p = { panel: { type: "none" } }; }
      p.buttons = { up: r.up, down: r.down ?? -1, back: r.back ?? -1, active_low: activeLow };
      hwShow(p);
      const missing = ["down", "back"].filter((k) => r[k] == null);
      const found = "Up is GPIO " + r.up + (r.down != null ? ", Down " + r.down : "") + (r.back != null ? ", Back " + r.back : "");
      note("#hw-msg", found + (missing.length ? ". Did not catch " + missing.join(" and ") + "; run it again." : ". Send to TICKEY to keep them."), !missing.length);
    } catch (err) { note("#hw-msg", String(err), false); }
  });
$("#hw-send").onclick = (e) =>
  busy(e.target, "Sending...", async () => {
    try {
      const profile = hwProfile();
      const r = await invoke("device_board_send", { port: port(), profile });
      settings.board_profile = profile;
      note("#hw-msg", "Saved on the TICKEY. Display backend: " + r.backend + ".", true);
    } catch (err) { note("#hw-msg", String(err), false); }
  });
$("#hw-test").onclick = (e) =>
  busy(e.target, "Refreshing...", async () => {
    try {
      const r = await invoke("device_panel_test", { port: port() });
      let msg;
      if (r.backend === "headless") msg = "No panel profile is active, so nothing was drawn. Send a profile first.";
      else if (!r.busy_seen) msg = "The panel never signalled BUSY. Check the busy pin and its polarity, then the init script.";
      else if (r.timeouts) msg = "The panel stayed BUSY until the timeout (" + r.busy_ms + " ms). The refresh command or power-on step is likely wrong.";
      else msg = "The panel took the frame: BUSY for " + (r.busy_ms / 1000).toFixed(1) + " s, " + (r.total_ms / 1000).toFixed(1) + " s in all. Check the screen.";
      note("#hw-msg", msg, r.busy_seen && !r.timeouts);
    } catch (err) { note("#hw-msg", String(err), false); }
  });
$("#hw-reset").onclick = (e) =>
  busy(e.target, "Forgetting...", async () => {
    if (!confirm("Forget the board profile on this TICKEY? It goes back to the built-in pins and the headless display.")) return;
    try {
      await invoke("device_board_reset", { port: port() });
      note("#hw-msg", "Profile removed from the TICKEY.", true);
    } catch (err) { note("#hw-msg", String(err), false); }
  });

$$("[data-press]").forEach((b) => (b.onclick = () => invoke("device_press", { port: port(), button: b.dataset.press }).catch((e) => alert(e))));
$("#grab").onclick = (e) =>
  busy(e.target, "Reading...", async () => {
    try {
      $("#grabbed").src = await invoke("device_frame", { port: port(), paper: true });
      $("#grabbed").hidden = false;
    } catch (err) {
      alert(err);
    }
  });

// ---------------------------------------------------------------- privacy
function chips(sel, key, placeholder) {
  const box = $(sel);
  const draw = () => {
    const input = el("input", { placeholder });
    input.onkeydown = (e) => {
      if ((e.key === "Enter" || e.key === ",") && input.value.trim()) {
        e.preventDefault();
        settings[key] = [...settings[key], input.value.trim()];
        save();
        draw();
        box.querySelector("input").focus();
      } else if (e.key === "Backspace" && !input.value && settings[key].length) {
        settings[key] = settings[key].slice(0, -1);
        save();
        draw();
        box.querySelector("input").focus();
      }
    };
    box.replaceChildren(
      ...settings[key].map((t, i) =>
        el("span", { class: "chip" }, t, el("button", { title: "Remove", onclick: () => { settings[key].splice(i, 1); save(); draw(); } }, "×"))),
      input
    );
  };
  draw();
}
$("#instructions").oninput = (e) => { settings.instructions = e.target.value; save(); };
$("#show-prompt").onclick = async () => {
  const pre = $("#prompt");
  pre.hidden = !pre.hidden;
  if (!pre.hidden) pre.textContent = await invoke("preview_prompt");
};

// ------------------------------------------------------------------- away
function renderAway() {
  $("#relay-url").value = settings.relay?.url || "";
  $("#relay-token").value = settings.relay?.write_token || "";
  const ok = state.status.relay_ok;
  if (ok == null) note("#relay-status", "", true);
  else note("#relay-status", ok ? "Last upload to the relay worked." : "The last upload to the relay failed. Check the URL and token.", ok);
}
const relayChanged = () => {
  const url = $("#relay-url").value.trim(), tok = $("#relay-token").value.trim();
  settings.relay = url ? { url, write_token: tok } : null;
  save();
};
$("#relay-url").oninput = $("#relay-token").oninput = relayChanged;

// ------------------------------------------------------------------- boot
async function refreshState() {
  state = await invoke("get_state");
  if (!settings) settings = state.settings;
  else settings.devices = state.settings.devices; // devices change from the backend
  settings.tools = state.settings.tools;
}
async function boot() {
  await refreshState();
  $$("#model-seg button").forEach((x) => x.classList.toggle("on", x.dataset.model === settings.model));
  $("#about").value = settings.about;
  $("#commute-from").value = settings.commute_from || "";
  $("#commute-mode").value = settings.commute_mode || "drive";
  $("#commute-buffer").value = settings.commute_buffer_min ?? 5;
  if (settings.board_profile) hwShow(settings.board_profile);
  $("#instructions").value = settings.instructions;
  chips("#private", "private_terms", "Add a name or topic, press Enter");
  chips("#vips", "vips", "Add a person, press Enter");
  renderScreens();
  renderDeviceSettings();
  renderAway();
  renderPreview();
  // Keep the status fresh while the window is open.
  setInterval(async () => {
    const prev = state.status.generated;
    await refreshState();
    renderFacts();
    if (state.status.generated !== prev) renderPreview();
  }, 10000);
  // First run: nudge toward Claude sign-in.
  const st = await invoke("claude_status");
  if (!st.installed || !st.logged_in) $('[data-page="claude"]').click();
}
boot();
