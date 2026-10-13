/* Rolling Rob offline: every file of the site in one cache per version.
 * tools/build-web.sh writes the version (a hash of the files) into this file,
 * so a new build is a new sw.js -- the browser notices that by itself on
 * every start and installs the new version beside the old one. The page then
 * offers "Update" (shell.html); the old version keeps running until then.
 *
 * The site holds every release too, each in its own folder (v/<tag>/) with
 * its own copy of this worker. Each worker keeps its cache under its own
 * scope and only ever removes older caches of that same scope, so going back
 * to an older version and forward again never throws one away. */
var VERSION = '__VERSION__';
var BUILT = '__BUILT__'; /* shown on the start screen */
var SCOPE = new URL(self.registration.scope).pathname;
var PREFIX = 'rolling-rob|' + SCOPE + '|';
var CACHE = PREFIX + VERSION;
/* the first releases named their one cache rolling-rob-<version> */
var OLD_NAME = /^rolling-rob-[0-9a-f]{12}$/;
var FILES = ['./', 'index.html', 'index.js', 'index.wasm', 'index.data',
             'icon-192.png', 'icon-512.png', 'manifest.webmanifest'];

self.addEventListener('install', function (e) {
  e.waitUntil(caches.open(CACHE).then(function (c) {
    /* past the HTTP cache: GitHub Pages lets browsers keep files 10 minutes */
    return c.addAll(FILES.map(function (f) { return new Request(f, { cache: 'reload' }); }));
  }));
});

self.addEventListener('activate', function (e) {
  var root = !/\/v\/[^/]+\/$/.test(SCOPE);
  e.waitUntil(caches.keys().then(function (keys) {
    return Promise.all(keys.filter(function (k) {
      return (k.indexOf(PREFIX) === 0 && k !== CACHE) || (root && OLD_NAME.test(k));
    }).map(function (k) { return caches.delete(k); }));
  }).then(function () { return self.clients.claim(); }));
});

self.addEventListener('message', function (e) {
  if (e.data === 'update') self.skipWaiting();
  if (e.data === 'version' && e.source) e.source.postMessage({ version: VERSION, built: BUILT });
});

/* From the cache, the network only for what is not in it. The query string
 * (?scale=, ?music=) does not choose a different file. Other versions'
 * folders and the list of versions are not this worker's: straight to the
 * network (their own worker takes over once they have been opened). */
self.addEventListener('fetch', function (e) {
  if (e.request.method !== 'GET') return;
  var url = new URL(e.request.url);
  if (url.origin !== location.origin || url.pathname.indexOf(SCOPE) !== 0) return;
  var rest = url.pathname.slice(SCOPE.length);
  if (rest.indexOf('v/') === 0 || rest === 'versions.json') return;
  e.respondWith(caches.open(CACHE).then(function (c) {
    return c.match(e.request, { ignoreSearch: true }).then(function (hit) {
      return hit || fetch(e.request);
    });
  }));
});
