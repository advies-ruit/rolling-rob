/* Rolling Rob offline: every file of the site in one cache per version.
 * tools/build-web.sh writes the version (a hash of the files) into this file,
 * so a new build is a new sw.js -- the browser notices that by itself on
 * every start and installs the new version beside the old one. The page then
 * offers "Update" (shell.html); the old version keeps running until then. */
var VERSION = '2311946109c9';
var BUILT = '6 Oct 2026 22:39'; /* shown on the start screen */
var CACHE = 'rolling-rob-' + VERSION;
var FILES = ['./', 'index.html', 'index.js', 'index.wasm', 'index.data',
             'icon-192.png', 'icon-512.png', 'manifest.webmanifest'];

self.addEventListener('install', function (e) {
  e.waitUntil(caches.open(CACHE).then(function (c) {
    /* past the HTTP cache: GitHub Pages lets browsers keep files 10 minutes */
    return c.addAll(FILES.map(function (f) { return new Request(f, { cache: 'reload' }); }));
  }));
});

self.addEventListener('activate', function (e) {
  e.waitUntil(caches.keys().then(function (keys) {
    return Promise.all(keys.filter(function (k) {
      return k.indexOf('rolling-rob-') === 0 && k !== CACHE;
    }).map(function (k) { return caches.delete(k); }));
  }).then(function () { return self.clients.claim(); }));
});

self.addEventListener('message', function (e) {
  if (e.data === 'update') self.skipWaiting();
  if (e.data === 'version' && e.source) e.source.postMessage({ version: VERSION, built: BUILT });
});

/* From the cache, the network only for what is not in it. The query string
 * (?scale=, ?music=) does not choose a different file. */
self.addEventListener('fetch', function (e) {
  if (e.request.method !== 'GET' || new URL(e.request.url).origin !== location.origin) return;
  e.respondWith(caches.open(CACHE).then(function (c) {
    return c.match(e.request, { ignoreSearch: true }).then(function (hit) {
      return hit || fetch(e.request);
    });
  }));
});
