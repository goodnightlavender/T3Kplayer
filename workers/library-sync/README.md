# tone3000-library-sync (CloudFlare Worker)

Stores TONE3000 Player library entries in a CloudFlare D1 database keyed by
TONE3000 user id. The plug-in pushes/pulls via this Worker so signing in on a
new machine restores your library.

This Worker is **self-hosted** per fork operator — each user (or fork) deploys
their own. There is no central sync service.

## Prerequisites

- A CloudFlare account (free tier is fine for personal use).
- `node` 18+ and `npm`.
- The `wrangler` CLI authenticated against your CloudFlare account:
  ```bash
  npm install
  npx wrangler login
  ```

## One-time deploy

```bash
cd workers/library-sync
npm install

# 1. Create the D1 database. CloudFlare prints a database_id at the end.
npx wrangler d1 create tone3000-library

# 2. Paste that database_id into wrangler.toml (replace
#    REPLACE_WITH_YOUR_D1_DB_ID).

# 3. Initialise the schema on the live database.
npm run init-db

# 4. Deploy the Worker. wrangler prints the live URL (looks like
#    https://tone3000-library-sync.<your-subdomain>.workers.dev).
npm run deploy

# 5. Smoke-test the health endpoint:
curl https://tone3000-library-sync.<your-subdomain>.workers.dev/health
# → {"ok":true,"service":"tone3000-library-sync"}
```

## Wire the URL into the plug-in

Open `nam-fork/NeuralAmpModeler/cloud/SyncConfig.h` and replace `REPLACE_ME`
with your Worker URL (no trailing slash):

```cpp
constexpr const char* kLibrarySyncUrl =
    "https://tone3000-library-sync.<your-subdomain>.workers.dev";
```

Rebuild the plug-in (`MSBuild … NeuralAmpModeler-vst3.vcxproj` — the
post-commit hook also does this). The plug-in now pushes on every
`ModelAdded`/`ModelUpdated` and pulls on every sign-in. With the placeholder
in place, `LibrarySync` is a no-op and the plug-in behaves identically to
Phase 7.

## Local development

```bash
# Initialise the LOCAL sqlite copy wrangler manages for dev.
npm run init-db-local

# Start the Worker locally — listens on http://127.0.0.1:8787 by default.
npm run dev

# Smoke test:
curl http://127.0.0.1:8787/health
```

Point `SyncConfig::kLibrarySyncUrl` at `http://127.0.0.1:8787` for a fully
local round-trip.

## Endpoints

All endpoints (except `/health`) require an `Authorization: Bearer <token>`
header where `<token>` is a TONE3000 access token. The Worker resolves the
token to a user id via `https://www.tone3000.com/api/v1/user` (cached 5 min
in the CloudFlare edge cache).

| Method | Path | Description |
|---|---|---|
| `GET` | `/health` | Open — returns `{ok:true,service:"…"}`. |
| `GET` | `/v1/library` | Returns all non-deleted entries for the user. |
| `PUT` | `/v1/library/entry/:tone_id/:model_id` | Upsert one entry. Returns 409 + `server_version` on stale `sync_version`. |
| `DELETE` | `/v1/library/entry/:tone_id/:model_id` | Soft-delete one entry. |

## Cost notes

CloudFlare's free tier covers:
- 100,000 Worker requests / day
- 5,000,000 D1 row reads / day
- 100,000 D1 row writes / day

For a single user with a few hundred downloaded tones this is dramatically
over-provisioned. If you operate this Worker for a community fork the limits
still cover several hundred users; beyond that move to the Workers Paid plan
($5/mo flat).

## Tearing down

```bash
# Remove the Worker. D1 database stays so existing data isn't lost.
npx wrangler delete tone3000-library-sync

# Remove the D1 database too (DESTRUCTIVE).
npx wrangler d1 delete tone3000-library
```
