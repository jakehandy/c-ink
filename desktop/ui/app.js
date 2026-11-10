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
$("#smart-every").onchange = (e) => { settings.smart_every_min = +e.target.value; save(); };
$("#quick-cal").onchange = (e) => { settings.quick_calendar = e.target.checked; save(); };
function renderSmart() {
  $("#smart-every").value = String(settings.smart_every_min ?? 60);
  $("#quick-cal").checked = settings.quick_calendar !== false;
  const st = state.status || {};
  $("#smart-last").textContent = st.smart_at
    ? `Last smart run ${new Date(st.smart_at * 1000).toLocaleTimeString([], { hour: "numeric", minute: "2-digit" })}${st.last_pass ? `, last pass: ${st.last_pass}` : ""}${st.unchanged ? ", nothing changed since" : ""}.`
    : "No smart run yet today.";
}
// ------------------------------------------------------------ marketplace
let marketFilter = "all";
const CONNECTORS_URL = "https://claude.ai/settings/connectors";
const mark = (icon, big) => el("span", { class: "mark" + (big ? " big" : ""), style: `--icon: url(icons/${icon || "claude"}.svg)` });
const src = (id) => state.sources.find((s) => s.id === id);

function stateLabel(s) {
  if (s.how === "connector" && s.accounts > 1 && ["gmail", "gdrive", "gcal"].includes(s.id)) return `${s.accounts} accounts`;
  if (s.id === "cloudflare" && s.connected) return settings.cloudflare_account_name || "Connected";
  if (s.id === "weather" && s.connected) return settings.weather_place?.name || "Set";
  if (s.id === "github" && s.connected && !s.accounts) return "GitHub CLI";
  if (s.how === "local" || s.id === "news") return s.connected ? "Ready" : "Not found";
  return s.connected ? "Connected" : s.how === "builtin" ? "Set up" : "Connect";
}

function renderMarket() {
  const list = state.sources.filter((s) => marketFilter === "all" || (marketFilter === "on") === s.connected);
  const on = state.sources.filter((s) => s.connected).length;
  $("#market-count").textContent = `${on} of ${state.sources.length} connected`;
  const tile = (s) => {
    const live = s.screens.filter((k) => settings.screens[k.key]).length;
    return el("button", { class: "tile" + (s.connected ? "" : " off"), onclick: () => openSheet(s.id) },
      el("div", { class: "top" }, mark(s.icon), el("span", { class: "name" }, s.name)),
      el("div", { class: "blurb" }, s.blurb),
      el("div", { class: "foot" },
        el("span", { class: "state" + (s.connected ? " ok" : "") }, stateLabel(s)),
        el("span", { class: "count" }, `${live}/${s.screens.length} screen${s.screens.length === 1 ? "" : "s"} on`)));
  };
  // One section per type, in the bridge's order; empty sections are left out.
  const cats = state.categories || [{ id: "", name: "" }];
  $("#market").replaceChildren(
    ...cats.flatMap((c) => {
      const inCat = list.filter((s) => !c.id || s.category === c.id);
      return inCat.length ? [el("section", { class: "market-group" },
        c.name ? el("h3", {}, c.name) : "",
        el("div", { class: "grid" }, ...inCat.map(tile)))] : [];
    })
  );
}
$$("#market-filter button").forEach((b) =>
  b.addEventListener("click", () => {
    $$("#market-filter button").forEach((x) => x.classList.toggle("on", x === b));
    marketFilter = b.dataset.f;
    renderMarket();
  })
);

function screenToggle(k) {
  const box = el("input", { type: "checkbox", checked: settings.screens[k.key] });
  box.onchange = () => { settings.screens[k.key] = box.checked; save(); renderMarket(); };
  return el("div", { class: "item" },
    el("div", { class: "grow" },
      el("div", { class: "name" }, k.name, " ", k.shared ? el("span", { class: "tag" }, "shared") : ""),
      el("div", { class: "blurb" }, k.blurb)),
    el("label", { class: "switch", title: "Show this screen and its Up next row" }, box, el("span")));
}

function connectorFlow(s, steps) {
  const ok = s.connected;
  return el("div", { class: "connect-box" },
    el("ol", { class: "steps" }, ...steps.map((t) => el("li", {}, t))),
    el("div", { class: "actions" },
      el("button", { class: ok ? "" : "primary", "data-open": CONNECTORS_URL }, ok ? "Manage on claude.ai" : `Connect ${s.name} on claude.ai`),
      el("button", { onclick: async (e) => { await recheck(e.target); openSheet(s.id); } }, ok ? "Recheck" : "I connected it")),
    el("div", { class: "result" }, el("span", { class: "state" + (ok ? " ok" : "") }, ok ? `Claude Code can see ${s.name}${s.accounts > 1 ? ` (${s.accounts} accounts)` : ""}.` : "Not visible to Claude Code yet.")));
}

const FLOWS = {
  gmail: (s) => connectorFlow(s, ["Open claude.ai connectors and add Gmail.", "For each extra inbox, add Gmail again and sign in with that account. Each one shows up as its own section on the Inbox screen.", "Come back and recheck."]),
  outlook: (s) => connectorFlow(s, ["Open claude.ai connectors and add Microsoft 365, signing in with your work or school account.", "Come back and recheck. Outlook mail joins the Inbox screen as its own section, and Outlook meetings join Next up and Commute."]),
  m365: (s) => connectorFlow(s, ["Open claude.ai connectors and add Microsoft 365.", "Come back and recheck. Teams gets its own screen, and Claude reads SharePoint and OneDrive files to brief you before meetings."]),
  intercom: (s) => connectorFlow(s, ["Open claude.ai connectors and add Intercom.", "Come back and recheck. The Customer inbox screen shows conversations waiting on you or your team, longest wait first."]),
  gdrive: (s) => connectorFlow(s, ["Open claude.ai connectors and add Google Drive.", "For each extra Google account, add Google Drive again and sign in with that account.", "Come back and recheck. Claude reads the docs attached to or named in your meetings so Next up can tell you what to know going in."]),
  once: (s) => el("div", {},
    el("p", { class: "hint" }, "ONCE (once.app) is a music release platform. Artists and labels use it to send songs and albums to Spotify, Apple Music and the other stores, manage their catalog, and see how each release performs."),
    connectorFlow(s, ["Open claude.ai connectors and add ONCE with your ONCE account.", "Come back and recheck. Releases shows streams and stores for your latest release, flags anything stuck in review, and new releases count toward This week's wins."])),
  tavern: (s) => el("div", {},
    el("p", { class: "hint" }, "Tavern keeps a tabletop roleplaying group's campaign in one place: dated sessions with everyone's notes, a wiki of people and places, maps with pins, character sheets with live hit points and spell slots, and a combat tracker."),
    connectorFlow(s, ["Open claude.ai connectors and add Tavern.", "Come back and recheck. Game night shows your next session, a line on where the last one left off, and your character's hit points."])),
  shop: (s) => connectorFlow(s, ["Open claude.ai connectors, search for Shop and connect it with the Shop app's sign-in.", "Come back and recheck. The Deliveries screen uses Shop's orders and tracking."]),
  github: (s) => connectorFlow(s, ["Connect GitHub on claude.ai, or just sign in to the gh command line tool on this computer: C-Ink uses it when there is no connector.", "Come back and recheck."]),
  gcal: (s) => {
    const box = connectorFlow(s, ["Connect Google Calendar on claude.ai (one per calendar account).", "Come back and recheck."]);
    const from = el("input", { placeholder: "Home in the Mission, SF", value: settings.commute_from || "" });
    from.oninput = () => { settings.commute_from = from.value; save(); };
    const mode = el("select", {}, ...["drive", "transit", "walk", "bike"].map((m) => el("option", { value: m }, m[0].toUpperCase() + m.slice(1))));
    mode.value = settings.commute_mode || "drive";
    mode.onchange = () => { settings.commute_mode = mode.value; save(); };
    const buf = el("input", { type: "number", min: 0, max: 60, step: 5, value: settings.commute_buffer_min ?? 5, style: "width:4.5em" });
    buf.oninput = () => { const n = parseInt(buf.value, 10); if (n >= 0 && n <= 60) { settings.commute_buffer_min = n; save(); } };
    return el("div", {}, box, el("h4", {}, "Commute"),
      el("div", { class: "connect-box" },
        el("p", { class: "hint" }, "Where you usually start the day and how you get around. A neighborhood is enough; it only goes to Claude with the digest prompt."),
        el("div", { class: "row" }, from),
        el("div", { class: "row" }, mode, el("label", { class: "check" }, "Buffer ", buf, " min"))));
  },
  weather: () => {
    const q = el("input", { placeholder: "Zip code, city, or lat,lon", value: settings.weather_location || "" });
    let f = settings.weather_fahrenheit !== false;
    const unit = el("div", { class: "seg" },
      ...[["°F", true], ["°C", false]].map(([l, v]) => {
        const b = el("button", { class: f === v ? "on" : "" }, l);
        b.onclick = () => { f = v; unit.querySelectorAll("button").forEach((x) => x.classList.toggle("on", x === b)); };
        return b;
      }));
    const out = el("div", { class: "result" }, settings.weather_place ? el("span", { class: "state ok" }, `Showing weather for ${settings.weather_place.name}.`) : "");
    const go = el("button", { class: "primary" }, "Look up");
    go.onclick = () => busy(go, "Looking up...", async () => {
      try {
        const r = await invoke("weather_lookup", { query: q.value, fahrenheit: f });
        Object.assign(settings, { weather_location: q.value.trim(), weather_place: r.place, weather_fahrenheit: f });
        out.replaceChildren(el("span", { class: "state ok" }, `${r.place.name}: ${r.card.big} and ${r.card.short.split(" ").slice(1).join(" ")}. ${r.card.title}.`));
        await refreshState(); renderMarket(); renderPreview();
      } catch (e) { out.replaceChildren(el("span", { class: "state" }, String(e))); }
    });
    q.onkeydown = (e) => { if (e.key === "Enter") go.click(); };
    return el("div", { class: "connect-box" },
      el("p", { class: "hint" }, "Forecasts come from Open-Meteo, free and with no account. Only this place is sent to it, and Claude is not involved, so weather costs no Claude run."),
      el("div", { class: "row" }, q),
      el("div", { class: "row" }, unit, go), out);
  },
  cloudflare: (s) => {
    const tok = el("input", { type: "password", placeholder: "Paste the API token", value: settings.cloudflare_token || "" });
    const acct = el("input", { placeholder: "Account ID (optional, found automatically)", value: settings.cloudflare_account || "" });
    const out = el("div", { class: "result" }, s.connected ? el("span", { class: "state ok" }, `Connected to ${settings.cloudflare_account_name || settings.cloudflare_account}.`) : "");
    // Which site the Traffic screen follows.
    const zoneBox = el("div", {});
    const fillZones = (zones) => {
      if (zones == null) {
        zoneBox.replaceChildren(el("p", { class: "hint" }, "Traffic needs Zone > Zone > Read and Zone > Analytics > Read on this token."));
        return;
      }
      if (!zones.length) return zoneBox.replaceChildren();
      const pick = el("select", {}, el("option", { value: "" }, "The busiest site today"),
        ...zones.map((z) => el("option", { value: z.id }, z.name)));
      pick.value = zones.some((z) => z.id === settings.cloudflare_zone) ? settings.cloudflare_zone : "";
      pick.onchange = () => { settings.cloudflare_zone = pick.value; save(); };
      zoneBox.replaceChildren(el("p", { class: "hint", style: "margin:14px 0 6px" }, "Traffic follows"), el("div", { class: "row" }, pick));
    };
    if (s.connected) invoke("cloudflare_zones").then(fillZones).catch(() => {});
    const go = el("button", { class: "primary" }, s.connected ? "Save" : "Connect");
    go.onclick = () => busy(go, "Checking...", async () => {
      try {
        const r = await invoke("cloudflare_connect", { token: tok.value, account: acct.value });
        Object.assign(settings, { cloudflare_token: tok.value.trim(), cloudflare_account: r.account, cloudflare_account_name: r.name });
        acct.value = r.account;
        out.replaceChildren(el("span", { class: "state ok" }, `Connected to ${r.name || r.account}. ${r.alerts == null ? "Could not read alert history: check the Notifications permission." : `${r.alerts} alert${r.alerts === 1 ? "" : "s"} in the last day.`}`));
        fillZones(r.zones);
        await refreshState(); renderMarket();
      } catch (e) { out.replaceChildren(el("span", { class: "state" }, String(e))); }
    });
    const off = el("button", { class: "ghost", onclick: async () => {
      await invoke("cloudflare_disconnect");
      Object.assign(settings, { cloudflare_token: "", cloudflare_account: "", cloudflare_account_name: "", cloudflare_zone: "" });
      await refreshState(); renderMarket(); openSheet("cloudflare");
    } }, "Disconnect");
    return el("div", { class: "connect-box" },
      el("ol", { class: "steps" },
        el("li", {}, "Create a custom API token: ", el("a", { href: "#", "data-open": "https://dash.cloudflare.com/profile/api-tokens" }, "dash.cloudflare.com/profile/api-tokens"), "."),
        el("li", {}, "Give it Account > Notifications > Read and Account > Account Settings > Read for alerts, plus Zone > Zone > Read and Zone > Analytics > Read for the Traffic screen. Nothing else; C-Ink only reads."),
        el("li", {}, "Paste it here. It is stored only in C-Ink's settings file on this computer.")),
      el("div", { class: "row" }, tok), el("div", { class: "row" }, acct),
      el("div", { class: "actions" }, go, s.connected ? off : ""), out, zoneBox);
  },
  news: () => {
    const t = el("textarea", { rows: 3, placeholder: "Local news for my zip code, progressive political news, and general world news" }, settings.news_prompt || "");
    t.oninput = () => { settings.news_prompt = t.value; save(); };
    return el("div", { class: "connect-box" },
      el("p", { class: "hint" }, "Tell Claude what your morning news should cover, in your own words. Up to three sections. Claude researches it on the web once a morning, between 5 and 11 AM, and the card stays until noon."),
      t,
      el("p", { class: "hint", style: "margin:10px 0 0" }, "That run can only search and read the web. None of your connectors are loaded in it, so nothing from the open web can reach your mail or calendar."));
  },
  claudecode: (s) => {
    const out = el("div", { class: "result" });
    const go = el("button", {}, "Show what it sees");
    go.onclick = () => busy(go, "Reading...", async () => {
      const c = await invoke("claude_code_preview");
      if (!c) return out.replaceChildren("No Claude Code activity in the last day.");
      out.replaceChildren(el("b", {}, c.title), el("div", { class: "hint" }, c.subtitle || c.meta || ""),
        el("ul", { class: "rows" }, ...(c.rows || []).map((r) => el("li", {}, el("b", {}, r.label), el("span", {}, r.detail)))));
    });
    return el("div", { class: "connect-box" },
      el("p", { class: "hint" }, "C-Ink reads the session logs Claude Code keeps on this computer for timing, token counts, the project folder and branch, plus how many commits you made today. It never reads what was said in a session, and none of it is sent to Claude."),
      el("div", { class: "result" }, el("span", { class: "state" + (s.connected ? " ok" : "") }, s.connected ? "Found Claude Code sessions on this computer." : "No Claude Code sessions found yet.")),
      el("div", { class: "actions" }, go), out);
  },
  claude: () => el("div", { class: "connect-box" }, el("p", { class: "hint", style: "margin:0" }, "Nothing to connect. Claude builds these from everything else you have connected.")),
};

function openSheet(id) {
  const s = src(id);
  if (!s) return;
  $("#sheet-icon").replaceWith(Object.assign(mark(s.icon, true), { id: "sheet-icon" }));
  $("#sheet-name").textContent = s.name;
  $("#sheet-blurb").textContent = s.blurb;
  const flow = FLOWS[id] || ((x) => connectorFlow(x, [`Open claude.ai connectors and connect ${x.name}.`, "Come back and recheck."]));
  $("#sheet-connect").replaceChildren(flow(s));
  $("#sheet-screens").replaceChildren(...s.screens.map(screenToggle));
  if (!$("#sheet").open) $("#sheet").showModal();
}
$("#sheet-close").onclick = () => $("#sheet").close();
$("#sheet").addEventListener("click", (e) => { if (e.target === $("#sheet")) $("#sheet").close(); });

async function recheck(btn = $("#recheck")) {
  await busy(btn, "Checking...", async () => {
    try { await invoke("refresh_connections"); } catch (e) { alert("Could not check connections: " + e); }
    await refreshState();
    renderMarket();
  });
}
$("#recheck").onclick = () => recheck();

// ----------------------------------------------------------------- device
const hours = [...Array(24).keys()];
const hourLabel = (h) => `${h % 12 || 12} ${h < 12 ? "AM" : "PM"}`;
for (const id of ["#quiet-a", "#quiet-b"]) $(id).replaceChildren(...hours.map((h) => el("option", { value: h }, hourLabel(h))));

function renderDeviceSettings() {
  $("#refresh").value = settings.refresh_min;
  $("#refresh-val").textContent = `${settings.refresh_min} min`;
  $("#quiet-on").checked = !!settings.quiet;
  const [qa, qb] = settings.quiet || [22, 7];
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
  renderSmart();
  if (settings.board_profile) hwShow(settings.board_profile);
  $("#instructions").value = settings.instructions;
  chips("#private", "private_terms", "Add a name or topic, press Enter");
  chips("#vips", "vips", "Add a person, press Enter");
  renderMarket();
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
