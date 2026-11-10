// C-Ink relay: lets a TICKEY sync from any Wi-Fi. Stores one encrypted blob
// per device in Workers KV. It never sees a key, so it cannot read anything.
//
//   PUT /v1/d/:id   Authorization: Bearer <WRITE_TOKEN>   (C-Ink Settings)
//                   X-Cink-Token-Hash: sha256(device token), X-Cink-ETag
//   GET /v1/d/:id   Authorization: Bearer <device token>  (the TICKEY)
//                   If-None-Match supported, answers 304

const MAX_BLOB = 32 * 1024;

async function sha256hex(s) {
  const d = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(s));
  return [...new Uint8Array(d)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

function timingSafeEqual(a, b) {
  if (a.length !== b.length) return false;
  let r = 0;
  for (let i = 0; i < a.length; i++) r |= a.charCodeAt(i) ^ b.charCodeAt(i);
  return r === 0;
}

export default {
  async fetch(req, env) {
    const url = new URL(req.url);
    const m = url.pathname.match(/^\/v1\/d\/([A-Za-z0-9-]{4,24})$/);
    if (!m) return new Response("not found", { status: 404 });
    const id = m[1];
    const auth = (req.headers.get("Authorization") || "").replace(/^Bearer /, "");

    if (req.method === "PUT") {
      if (!env.WRITE_TOKEN || !timingSafeEqual(auth, env.WRITE_TOKEN)) return new Response("unauthorized", { status: 401 });
      const body = await req.arrayBuffer();
      if (body.byteLength === 0 || body.byteLength > MAX_BLOB) return new Response("bad size", { status: 413 });
      const th = req.headers.get("X-Cink-Token-Hash") || "";
      const etag = req.headers.get("X-Cink-ETag") || "";
      if (!/^[0-9a-f]{64}$/.test(th)) return new Response("missing token hash", { status: 400 });
      await env.BLOBS.put(`d:${id}`, body, { metadata: { th, etag, at: Date.now() }, expirationTtl: 60 * 60 * 48 });
      return new Response(null, { status: 204 });
    }

    if (req.method === "GET") {
      const { value, metadata } = await env.BLOBS.getWithMetadata(`d:${id}`, "arrayBuffer");
      if (!value || !metadata) return new Response("no digest", { status: 404 });
      if (!timingSafeEqual(await sha256hex(auth), metadata.th)) return new Response("unauthorized", { status: 401 });
      if (metadata.etag && req.headers.get("If-None-Match") === metadata.etag) return new Response(null, { status: 304 });
      return new Response(value, {
        headers: { "Content-Type": "application/octet-stream", ETag: metadata.etag || "", "Cache-Control": "no-store" },
      });
    }
    return new Response("method not allowed", { status: 405 });
  },
};
