const fragment = location.hash;
if (fragment.startsWith("#pair="))
  history.replaceState(null, "", location.pathname + location.search);
const hubTicket = /^#pair=[0-9a-f]{64}$/.test(fragment)
  ? fragment.slice(6)
  : "";
let revision,
  busy = false;
const storageKey = "pico-api-key";

async function api(path, body, publicRead = false) {
  const headers = publicRead ? {} : { Authorization: "Bearer " + key.value };
  if (body) headers["Content-Type"] = "application/json";
  const response = await fetch("/api/v1/" + path, {
    method: body ? "POST" : "GET",
    headers,
    body: body && JSON.stringify(body),
    cache: "no-store",
    credentials: "omit",
    redirect: "error",
  });
  if (!response.ok) throw response.status;
  return response.json();
}
async function overview() {
  try {
    const status = await api("overview", null, true);
    info.value =
      (status.busy ? "Program running" : "Monitoring") +
      (status.wort_f ? " / " + status.wort_f + " °F" : "");
  } catch {
    info.value = "Brewer unavailable.";
  }
}
async function show(action) {
  if (busy) return;
  busy = true;
  key.readOnly = true;
  try {
    if (action)
      await api("commands", {
        command: "encoder",
        display_revision: revision,
        ...action,
      });
    let frame = "";
    let mode;
    for (let offset = 0; offset < 1024; offset += 128) {
      const page = await api(
        "display" + (offset ? "/" + offset + "?revision=" + revision : ""),
      );
      if (!offset) {
        revision = page.revision;
        mode = page.dial;
      }
      frame += page.data;
    }
    if (!key.value) return;
    const ctx = oled.getContext("2d");
    for (let i = 0; i < 1024; i++) {
      const bits = parseInt(frame.slice(2 * i, 2 * i + 2), 16);
      for (let bit = 0; bit < 8; bit++) {
        ctx.fillStyle = bits & (1 << bit) ? "white" : "black";
        ctx.fillRect(i % 128, (i >> 7) * 8 + bit, 1, 1);
      }
    }
    up.disabled = down.disabled = mode !== "menu";
    press.disabled = mode === "off";
    try {
      sessionStorage.setItem(storageKey, key.value);
    } catch {}
    owner.hidden = false;
    message.value = "";
  } catch (e) {
    owner.hidden = true;
    message.value =
      e === 401
        ? "Check the device key."
        : e === 409
          ? "Refresh the changed screen."
          : "Panel unavailable.";
  } finally {
    busy = false;
    key.readOnly = false;
  }
}
key.oninput = () => {
  owner.hidden = list.hidden = true;
  list.replaceChildren();
  samples.textContent = "";
};
form.onsubmit = (e) => {
  e.preventDefault();
  return show();
};
up.onclick = () => show({ delta: -1 });
down.onclick = () => show({ delta: 1 });
press.onclick = () => show({ click: true });
copy.onclick = async () => {
  try {
    await navigator.clipboard.writeText(key.value);
    message.value = "Device key copied.";
  } catch {
    key.type = "text";
    key.focus();
    key.select();
    message.value = "Copy the selected key.";
    key.onblur = () => {
      key.type = "password";
    };
  }
};
forget.onclick = () => {
  try {
    sessionStorage.removeItem(storageKey);
  } catch {}
  key.value = "";
  key.type = "password";
  key.oninput();
  message.value = "";
};
overview();
setInterval(() => {
  if (!document.hidden && !busy) overview();
}, 15000);
try {
  key.value = sessionStorage.getItem(storageKey) || "";
} catch {}
if (key.value && !hubTicket) show();
async function pairDevice(nonce) {
  pair.disabled = true;
  try {
    if (!nonce) {
      const bytes = crypto.getRandomValues(new Uint8Array(32));
      nonce = Array.from(bytes, (b) => b.toString(16).padStart(2, "0")).join(
        "",
      );
      const pending = await api("pairing/request", { nonce }, true);
      if (pending.code !== nonce.slice(0, 8)) throw Error();
      message.value =
        "Open About Zymatic and approve code " + pending.code + ".";
    }
    for (let i = 0; i < 90; i++) {
      const reply = await api("pairing/claim", { nonce }, true);
      if (/^[0-9a-f]{64}$/.test(reply.api_key || "")) {
        key.value = reply.api_key;
        await show();
        return;
      }
      if (reply.state !== "pending") break;
      await new Promise((resolve) => setTimeout(resolve, 1000));
    }
    throw Error();
  } catch {
    message.value = "Pairing unavailable. Start again.";
  } finally {
    pair.disabled = false;
  }
}
pair.onclick = () => pairDevice("");
if (hubTicket) pairDevice(hubTicket);
journal.onclick = async () => {
  try {
    const credential = key.value;
    const { sessions } = await api("history");
    if (credential !== key.value || owner.hidden) return;
    list.replaceChildren(
      new Option(sessions.length ? "Choose a brew" : "No saved brews", ""),
    );
    for (const session of sessions)
      list.add(
        new Option(
          session.recipe_name +
            " · " +
            session.state +
            (session.truncated ? " (partial)" : ""),
          session.id,
        ),
      );
    list.hidden = false;
    samples.textContent = "";
  } catch {
    message.value = "Brew history unavailable.";
  }
};
list.onchange = async () => {
  const id = list.value;
  samples.textContent = "";
  if (!id) return;
  let offset = 0,
    rows = "Temperatures in °F\nSeconds\tWort\tHeater\tTarget\tRoute\tStep\n";
  try {
    do {
      const page = await api("history/samples/" + offset + "?id=" + id);
      if (list.value !== id || owner.hidden) return;
      for (const [ms, wort, heat, target, route, step] of page.samples) {
        rows +=
          [
            ms / 1000,
            wort,
            heat,
            target === 0 ? "Off" : target,
            ["Keg", "Mash", "Hop 1", "Hop 2", "Hop 3", "Hop 4", "Pause"][route],
            step == null ? null : step + 1,
          ]
            .map((v) => v ?? "—")
            .join("\t") + "\n";
      }
      offset = page.next_offset;
    } while (offset != null);
    samples.textContent = rows;
  } catch {
    if (list.value === id) message.value = "Samples unavailable.";
  }
};
