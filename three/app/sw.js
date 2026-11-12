/* Rolling Rob 3D offline: the page, three.js and the icons in one cache per version.
 * build.py writes the version (a hash of the files) into this file, so a new build is
 * a new sw.js: the browser notices that by itself and installs the new version beside
 * the old one. The menu then offers "update"; the old version runs until then.
 * Music (music/*.mp3, if the site has it) is kept the first time it plays, in a cache
 * of its own that a new version keeps. Range requests (Safari plays audio that way)
 * are answered from the whole file in the cache. */
var VERSION = '__VERSION__';
var BUILT = '__BUILT__';
var THREE_URL = new URL('__THREE__', self.registration.scope).href; /* a CDN, or the site's own copy */
var SCOPE = new URL(self.registration.scope).pathname;
var PREFIX = 'rolling-rob-3d|' + SCOPE + '|';
var CACHE = PREFIX + VERSION;
var MEDIA = PREFIX + 'music';
var FILES = ['./', 'index.html', 'manifest.webmanifest', 'icon-192.png', 'icon-512.png', 'icon-maskable-192.png', 'icon-maskable-512.png'];

self.addEventListener('install', function (e) {
  e.waitUntil(caches.open(CACHE).then(function (c) {
    /* past the HTTP cache: GitHub Pages lets browsers keep files 10 minutes */
    var own = FILES.map(function (f) { return new Request(f, { cache: 'reload' }); });
    return c.addAll(own.concat([new Request(THREE_URL, { mode: 'cors' })]));
  }));
});

self.addEventListener('activate', function (e) {
  e.waitUntil(caches.keys().then(function (keys) {
    return Promise.all(keys.filter(function (k) { return k.indexOf(PREFIX) === 0 && k !== CACHE && k !== MEDIA; })
      .map(function (k) { return caches.delete(k); }));
  }).then(function () { return self.clients.claim(); }));
});

self.addEventListener('message', function (e) {
  if (e.data === 'update') self.skipWaiting();
  if (e.data === 'version' && e.source) e.source.postMessage({ version: VERSION, built: BUILT });
});

function ranged(req, res) { /* a 206 for "Range: bytes=a-b" from a whole cached file */
  var m = /bytes=(\d*)-(\d*)/.exec(req.headers.get('range') || '');
  if (!m) return res;
  return res.arrayBuffer().then(function (buf) {
    var n = buf.byteLength, a = m[1] === '' ? Math.max(0, n - (+m[2])) : +m[1], b = m[1] !== '' && m[2] !== '' ? Math.min(+m[2], n - 1) : n - 1;
    if (a >= n || a > b) return new Response(null, { status: 416, headers: { 'Content-Range': 'bytes */' + n } });
    return new Response(buf.slice(a, b + 1), { status: 206, headers: {
      'Content-Type': res.headers.get('Content-Type') || 'audio/mpeg', 'Content-Range': 'bytes ' + a + '-' + b + '/' + n,
      'Content-Length': String(b - a + 1), 'Accept-Ranges': 'bytes' } });
  });
}

self.addEventListener('fetch', function (e) {
  if (e.request.method !== 'GET') return;
  var url = new URL(e.request.url);
  if (url.href === THREE_URL) { e.respondWith(caches.match(THREE_URL).then(function (hit) { return hit || fetch(e.request); })); return; }
  if (url.origin !== location.origin || url.pathname.indexOf(SCOPE) !== 0) return;
  var rest = url.pathname.slice(SCOPE.length);
  if (/^music\/[^/]+\.mp3$/.test(rest)) { /* kept once heard */
    e.respondWith(caches.open(MEDIA).then(function (c) {
      return c.match(url.pathname).then(function (hit) {
        if (hit) return ranged(e.request, hit);
        return fetch(url.pathname).then(function (res) {
          if (res.ok && res.status === 200) c.put(url.pathname, res.clone());
          return res.ok ? ranged(e.request, res) : res;
        }).catch(function () { return fetch(e.request); });
      });
    }));
    return;
  }
  /* the page and its files from the cache; the query string (?view=, #run=) does not
   * choose another file; anything else from the network */
  e.respondWith(caches.open(CACHE).then(function (c) {
    return c.match(e.request, { ignoreSearch: true }).then(function (hit) { return hit || fetch(e.request); });
  }));
});
