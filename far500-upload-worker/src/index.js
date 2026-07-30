// FAR-500 upload-relay: neemt een meting (.xlsx) van de laptop-UI aan en zet
// 'm door naar een GitHub Release-asset. Bestaat omdat uploads.github.com
// geen CORS ondersteunt (directe browser-upload wordt geblokkeerd) en de
// GitHub Actions dispatch-triggers een payloadlimiet van 64KB hebben -- te
// klein voor de "Geschiedenis"-export. Het echte GitHub-token blijft hier
// server-side (Worker-secret); de pagina kent alleen een gedeeld wachtwoord
// om dit endpoint tegen misbruik te beschermen.

const OWNER = "DynteqBV";
const REPO = "far-500";
const TAG = "recordings";

const CORS = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Methods": "POST, OPTIONS",
  "Access-Control-Allow-Headers": "Content-Type, X-Upload-Secret, X-Filename",
};

export default {
  async fetch(request, env) {
    if (request.method === "OPTIONS") return new Response(null, { headers: CORS });
    if (request.method !== "POST") return json({ ok: false, error: "method not allowed" }, 405);

    const secret = request.headers.get("X-Upload-Secret") || "";
    if (!env.UPLOAD_SECRET || secret !== env.UPLOAD_SECRET) {
      return json({ ok: false, error: "unauthorized" }, 401);
    }

    const filename = (request.headers.get("X-Filename") || "recording.xlsx").replace(/[\\/]/g, "_");
    const contentType = request.headers.get("Content-Type") || "application/octet-stream";
    const body = await request.arrayBuffer();
    if (!body || body.byteLength === 0) return json({ ok: false, error: "empty body" }, 400);

    const ghHeaders = {
      "Authorization": `token ${env.GH_TOKEN}`,
      "Accept": "application/vnd.github+json",
      "X-GitHub-Api-Version": "2022-11-28",
      "User-Agent": "far500-upload-worker",
    };

    try {
      let release = await getRelease(ghHeaders);
      if (!release) release = await createRelease(ghHeaders);

      let asset = await uploadAsset(release.upload_url, filename, contentType, body, ghHeaders);
      if (asset.status === 422) {
        asset = await uploadAsset(release.upload_url, uniqueName(filename), contentType, body, ghHeaders);
      }
      if (!asset.ok) {
        const text = await asset.text().catch(() => "");
        return json({ ok: false, error: `upload failed (${asset.status}): ${text.slice(0, 300)}` }, 502);
      }
      const data = await asset.json();
      return json({ ok: true, url: data.browser_download_url }, 200);
    } catch (e) {
      return json({ ok: false, error: String((e && e.message) || e) }, 500);
    }
  },
};

function json(obj, status) {
  return new Response(JSON.stringify(obj), { status, headers: { ...CORS, "Content-Type": "application/json" } });
}

async function getRelease(headers) {
  const res = await fetch(`https://api.github.com/repos/${OWNER}/${REPO}/releases/tags/${TAG}`, { headers });
  if (res.status === 404) return null;
  if (!res.ok) throw new Error(`get release failed (${res.status}): ${(await res.text().catch(() => "")).slice(0, 300)}`);
  return res.json();
}

async function createRelease(headers) {
  const res = await fetch(`https://api.github.com/repos/${OWNER}/${REPO}/releases`, {
    method: "POST",
    headers: { ...headers, "Content-Type": "application/json" },
    body: JSON.stringify({
      tag_name: TAG,
      name: "Metingen (recordings)",
      body: "Tijdelijke opslag van FAR-500 metingen, geupload vanaf de laptop-UI. Losse assets kunnen zonder gevolgen verwijderd worden.",
      prerelease: true,
    }),
  });
  if (!res.ok) throw new Error(`create release failed (${res.status}): ${(await res.text().catch(() => "")).slice(0, 300)}`);
  return res.json();
}

async function uploadAsset(uploadUrlTemplate, filename, contentType, body, headers) {
  const uploadUrl = uploadUrlTemplate.replace(/\{.*\}$/, "") + `?name=${encodeURIComponent(filename)}`;
  return fetch(uploadUrl, { method: "POST", headers: { ...headers, "Content-Type": contentType }, body });
}

function uniqueName(filename) {
  const dot = filename.lastIndexOf(".");
  const suffix = Math.random().toString(36).slice(2, 7);
  return dot > 0 ? `${filename.slice(0, dot)}_${suffix}${filename.slice(dot)}` : `${filename}_${suffix}`;
}
