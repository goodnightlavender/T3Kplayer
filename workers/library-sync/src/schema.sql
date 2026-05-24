-- TONE3000 library-sync D1 schema (Phase 8).
--
-- Run once at deployment time:
--   npm run init-db          (against the live D1 database)
--   npm run init-db-local    (against the local wrangler-managed sqlite)
--
-- The `user_id` column is the TONE3000 user.id returned by
-- https://www.tone3000.com/api/v1/user — the Worker resolves the
-- incoming Bearer token to a user_id and uses it as the row key.
-- Clients cannot forge identity by claiming a different user_id;
-- the Worker ignores any client-provided value here.

CREATE TABLE IF NOT EXISTS library_entries (
  user_id               TEXT NOT NULL,
  tone_id               TEXT NOT NULL,
  model_id              TEXT NOT NULL,
  tone_title            TEXT,
  display_name_override TEXT,
  creator               TEXT,
  gear_type             TEXT,
  platform              TEXT,
  image_url             TEXT,

  -- Last-write-wins concurrency control. Client sends its current
  -- sync_version on PUT; if server's is higher the PUT is rejected
  -- with 409 so the client can pull-then-merge. Incremented by the
  -- Worker on every accepted write.
  sync_version          INTEGER NOT NULL DEFAULT 1,
  synced_at             INTEGER NOT NULL,

  -- Soft delete so a DELETE on one device doesn't cause the other
  -- device to re-create the entry on its next push (without this
  -- the row would just reappear with a higher sync_version).
  deleted               INTEGER NOT NULL DEFAULT 0,

  PRIMARY KEY (user_id, tone_id, model_id)
);

-- The dominant read pattern: "give me everything for this user,
-- newest first" (GET /v1/library). Index covers that.
CREATE INDEX IF NOT EXISTS idx_library_entries_user
  ON library_entries(user_id, synced_at DESC);
