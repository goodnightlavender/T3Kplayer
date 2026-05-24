// auth.ts — Bearer-token → TONE3000 user_id resolver with a 5-min
// in-Worker cache. Used by the Hono middleware in index.ts to gate
// every request before it touches D1.
//
// Why server-side resolution: the row key in D1 is the user_id, and
// we MUST NOT trust a client-provided value. A signed-in TONE3000
// user can forge any X-User-Id header they want and pollute / read
// other users' libraries. The canonical answer is whatever
// https://www.tone3000.com/api/v1/user returns for the supplied
// Bearer token.

export interface Env {
  DB: D1Database;
  TONE3000_USER_URL: string;
}

export interface ResolvedUser {
  user_id: string;
  username: string;
}

const CACHE_TTL_SEC = 300; // 5 minutes

// Stable SHA-256 hex prefix of the token, used as the cache key
// (raw tokens shouldn't appear in URLs even for a private cache).
async function tokenCacheKey(token: string): Promise<string> {
  const bytes = new TextEncoder().encode(token);
  const buf = await crypto.subtle.digest('SHA-256', bytes);
  const hex = Array.from(new Uint8Array(buf))
    .map(b => b.toString(16).padStart(2, '0')).join('');
  // 32 chars is plenty of collision resistance for an auth cache;
  // shortened to keep the synthetic cache URL readable.
  return hex.slice(0, 32);
}

export async function resolveBearer(token: string, env: Env): Promise<ResolvedUser | null> {
  if (!token) return null;

  const key = await tokenCacheKey(token);
  // CloudFlare's edge cache uses requests as keys. We synthesize a
  // never-fetched URL; the cache treats it as opaque.
  const cacheReq = new Request(`https://cache.local/auth/${key}`);
  const cache = caches.default;

  const cached = await cache.match(cacheReq);
  if (cached) {
    try {
      return await cached.json() as ResolvedUser;
    } catch {
      // Cached body corrupt — fall through to a fresh lookup.
    }
  }

  const resp = await fetch(env.TONE3000_USER_URL, {
    headers: {
      'Authorization': `Bearer ${token}`,
      'Accept': 'application/json',
      'User-Agent': 'tone3000-library-sync/0.1',
    },
  });
  if (!resp.ok) return null;

  let user: { id?: string; username?: string };
  try {
    user = await resp.json();
  } catch {
    return null;
  }
  if (!user.id) return null;

  const resolved: ResolvedUser = {
    user_id: String(user.id),
    username: user.username ?? '',
  };

  // Cache the resolved user for CACHE_TTL_SEC seconds. The Response
  // is what the cache stores; we set Cache-Control so CloudFlare
  // honors the TTL.
  const cacheResp = new Response(JSON.stringify(resolved), {
    headers: {
      'Content-Type': 'application/json',
      'Cache-Control': `max-age=${CACHE_TTL_SEC}`,
    },
  });
  await cache.put(cacheReq, cacheResp);

  return resolved;
}
