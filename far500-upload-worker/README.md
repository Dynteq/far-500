# far500-upload-worker

Cloudflare Worker die de "Naar GitHub"-knoppen in `FAR-500.html` bedient:
neemt het geüploade .xlsx-bestand aan en zet het door naar een Release-asset
(tag `recordings`) op `dynteq/far-500`. Bestaat omdat een directe
browser-upload naar GitHub niet kan (zie README.md in de projectroot).

Sinds 2026-08-11 ook de omgekeerde richting: `GET /download?name=<asset>`
geeft de inhoud van een eerder geüploade asset terug (voor de "oude meting
laden"-dropdown in de UI). Ook dat kan niet rechtstreeks vanuit de browser —
de asset-download redirect eindigt op `release-assets.githubusercontent.com`
(Azure Blob), en die respons stuurt geen `Access-Control-Allow-Origin` mee
(bevestigd met `curl -D-`), dus blokkeert de browser het lezen ervan net als
bij uploads. Dit endpoint vereist bewust geen `X-Upload-Secret` — het geeft
alleen assets terug die al publiek in deze ene release staan.

## Deployen

Vereist een Cloudflare-account en [wrangler](https://developers.cloudflare.com/workers/wrangler/).

```
cd far500-upload-worker
npx wrangler login
npx wrangler secret put GH_TOKEN
```
Plak hier een fine-grained GitHub PAT, alleen scope `dynteq/far-500`,
permissie **Contents: Read and write**.

```
npx wrangler secret put UPLOAD_SECRET
```
Kies zelf een wachtwoord — dit is het "code"-veld dat je straks ook in de
FAR-500-pagina invult. Dit beschermt het endpoint tegen misbruik door
willekeurige bezoekers (het GitHub-token zelf komt nooit clientside).

```
npx wrangler deploy
```

Kopieer de uitkomst-URL (`https://far500-upload-worker.<jouw-subdomain>.workers.dev`)
naar de `GH_RELAY_URL`-constante bovenin `FAR-500.html`.

## Testen

```
curl -X POST https://far500-upload-worker.<jouw-subdomain>.workers.dev \
  -H "X-Upload-Secret: <jouw-secret>" \
  -H "X-Filename: test.xlsx" \
  -H "Content-Type: application/octet-stream" \
  --data-binary "@test.xlsx"
```
Verwacht een JSON-antwoord `{"ok":true,"url":"..."}` en een nieuwe asset onder
[Releases](https://github.com/dynteq/far-500/releases/tag/recordings).

Download-proxy testen:
```
curl -o test.xlsx "https://far500-upload-worker.<jouw-subdomain>.workers.dev/download?name=<bestandsnaam>.xlsx"
```
