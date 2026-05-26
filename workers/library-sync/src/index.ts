// index.ts — TONE3000 library-sync CloudFlare Worker entry point.
//
// Endpoints (all require `Authorization: Bearer <token>`):
//   GET    /v1/library                            — list user's entries
//   PUT    /v1/library/entry/:tone_id/:model_id   — upsert one entry
//   DELETE /v1/library/entry/:tone_id/:model_id   — soft-delete one entry
//
// Auth: every request runs through middleware that resolves the
// Bearer token to a TONE3000 user_id (cached 5 min). The Worker
// ignores any client-supplied user_id — only the resolved one
// from auth.ts is trusted.

import { Hono } from 'hono';
import type { Env } from './auth';
import { resolveBearer } from './auth';

interface AppVariables {
  user_id: string;
}

const app = new Hono<{ Bindings: Env; Variables: AppVariables }>();

// ── Auth middleware ──────────────────────────────────────────────
app.use('*', async (c, next) => {
  // /health is open so deploy smoke tests don't need a token.
  if (c.req.path === '/health') {
    await next();
    return;
  }

  const auth = c.req.header('Authorization') ?? '';
  const m = /^Bearer\s+(\S+)$/i.exec(auth);
  if (!m) {
    return c.json({ error: 'missing or malformed Authorization header' }, 401);
  }
  const user = await resolveBearer(m[1], c.env);
  if (!user) {
    return c.json({ error: 'invalid TONE3000 token' }, 401);
  }
  c.set('user_id', user.user_id);
  await next();
});

// ── Routes ───────────────────────────────────────────────────────

// Health check — handy for `wrangler dev` smoke tests without
// authenticating. Returns 200 with a small body.
app.get('/health', c => c.json({ ok: true, service: 'tone3000-library-sync' }));

// GET /v1/library — return all non-deleted entries for the user.
app.get('/v1/library', async c => {
  const user_id = c.get('user_id');
  const result = await c.env.DB.prepare(
    `SELECT tone_id, model_id, tone_title, display_name_override,
            creator, gear_type, platform, image_url,
            sync_version, synced_at
       FROM library_entries
      WHERE user_id = ?1 AND deleted = 0
      ORDER BY synced_at DESC`
  ).bind(user_id).all();

  return c.json({
    entries: result.results ?? [],
  });
});

// PUT /v1/library/entry/:tone_id/:model_id — upsert.
// Request body: JSON with any of tone_title, display_name_override,
// creator, gear_type, platform, image_url, sync_version.
// Returns 409 with {server_version} if client's sync_version is
// behind — client should pull, merge, and retry.
app.get('/v1/presets', async c => {
  const user_id = c.get('user_id');
  const result = await c.env.DB.prepare(
    `SELECT preset_id, name, state_json, sort_order, sync_version, synced_at
       FROM presets
      WHERE user_id = ?1 AND deleted = 0
      ORDER BY sort_order ASC, name COLLATE NOCASE`
  ).bind(user_id).all();

  return c.json({ presets: result.results ?? [] });
});

app.put('/v1/presets/:preset_id', async c => {
  const user_id = c.get('user_id');
  const preset_id = c.req.param('preset_id');
  if (preset_id === '1') return c.json({ error: 'default preset is immutable' }, 400);

  let body: Record<string, unknown> = {};
  try {
    body = await c.req.json();
  } catch {
    return c.json({ error: 'missing preset body' }, 400);
  }

  const name = typeof body.name === 'string' ? body.name : '';
  const state_json = typeof body.state_json === 'string' ? body.state_json : '';
  if (!name || !state_json) return c.json({ error: 'name and state_json are required' }, 400);

  const incomingVersion = Number(body.sync_version) || 1;
  const current = await c.env.DB.prepare(
    `SELECT sync_version FROM presets WHERE user_id = ?1 AND preset_id = ?2`
  ).bind(user_id, preset_id).first<{ sync_version: number }>();

  if (current && current.sync_version > incomingVersion) {
    return c.json({ error: 'stale', server_version: current.sync_version }, 409);
  }

  const newVersion = (current?.sync_version ?? 0) + 1;
  const now = Date.now();
  await c.env.DB.prepare(
    `INSERT INTO presets (
        user_id, preset_id, name, state_json, sort_order,
        sync_version, synced_at, deleted
     ) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, 0)
     ON CONFLICT(user_id, preset_id) DO UPDATE SET
        name         = excluded.name,
        state_json   = excluded.state_json,
        sort_order   = excluded.sort_order,
        sync_version = excluded.sync_version,
        synced_at    = excluded.synced_at,
        deleted      = 0`
  ).bind(user_id, preset_id, name, state_json, Number(body.sort_order) || 0,
         newVersion, now).run();

  return c.json({ ok: true, sync_version: newVersion, synced_at: now });
});

app.delete('/v1/presets/:preset_id', async c => {
  const user_id = c.get('user_id');
  const preset_id = c.req.param('preset_id');
  if (preset_id === '1') return c.json({ ok: true });

  await c.env.DB.prepare(
    `UPDATE presets
        SET deleted = 1,
            synced_at = ?3,
            sync_version = sync_version + 1
      WHERE user_id = ?1 AND preset_id = ?2`
  ).bind(user_id, preset_id, Date.now()).run();

  return c.json({ ok: true });
});

app.put('/v1/library/entry/:tone_id/:model_id', async c => {
  const user_id  = c.get('user_id');
  const tone_id  = c.req.param('tone_id');
  const model_id = c.req.param('model_id');

  let body: Record<string, unknown> = {};
  try {
    body = await c.req.json();
  } catch {
    // Empty / non-JSON body is OK — treat as upsert with no
    // optional fields set.
  }

  const incomingVersion = Number(body.sync_version) || 1;

  // Read current sync_version for last-write-wins check.
  const current = await c.env.DB.prepare(
    `SELECT sync_version FROM library_entries
      WHERE user_id = ?1 AND tone_id = ?2 AND model_id = ?3`
  ).bind(user_id, tone_id, model_id)
   .first<{ sync_version: number }>();

  if (current && current.sync_version > incomingVersion) {
    return c.json(
      { error: 'stale', server_version: current.sync_version },
      409,
    );
  }

  const newVersion = (current?.sync_version ?? 0) + 1;
  const now = Date.now();

  await c.env.DB.prepare(
    `INSERT INTO library_entries (
        user_id, tone_id, model_id,
        tone_title, display_name_override,
        creator, gear_type, platform, image_url,
        sync_version, synced_at, deleted
     ) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, 0)
     ON CONFLICT(user_id, tone_id, model_id) DO UPDATE SET
        tone_title             = excluded.tone_title,
        display_name_override  = excluded.display_name_override,
        creator                = excluded.creator,
        gear_type              = excluded.gear_type,
        platform               = excluded.platform,
        image_url              = excluded.image_url,
        sync_version           = excluded.sync_version,
        synced_at              = excluded.synced_at,
        deleted                = 0`
  ).bind(
    user_id, tone_id, model_id,
    (body.tone_title            as string | null) ?? null,
    (body.display_name_override as string | null) ?? null,
    (body.creator               as string | null) ?? null,
    (body.gear_type             as string | null) ?? null,
    (body.platform              as string | null) ?? null,
    (body.image_url             as string | null) ?? null,
    newVersion,
    now,
  ).run();

  return c.json({ ok: true, sync_version: newVersion, synced_at: now });
});

// DELETE /v1/library/entry/:tone_id/:model_id — soft-delete.
app.delete('/v1/library/entry/:tone_id/:model_id', async c => {
  const user_id  = c.get('user_id');
  const tone_id  = c.req.param('tone_id');
  const model_id = c.req.param('model_id');

  await c.env.DB.prepare(
    `UPDATE library_entries
        SET deleted      = 1,
            synced_at    = ?4,
            sync_version = sync_version + 1
      WHERE user_id  = ?1
        AND tone_id  = ?2
        AND model_id = ?3`
  ).bind(user_id, tone_id, model_id, Date.now()).run();

  return c.json({ ok: true });
});

export default app;
