/* A version taken off the site (tools/assemble-site.sh, RETIRED). A device
 * that had it keeps its worker, which would go on serving the old version
 * from its cache. The browser finds this file in its place, installs it as
 * an update, and it removes the version: its cache emptied, the worker
 * unregistered, and its open pages reloaded -- from the network now, where
 * index.html sends them to the newest version. */
self.addEventListener('install', function () { self.skipWaiting(); });

self.addEventListener('activate', function (e) {
  /* rolling-rob|<scope>| and, for the 3D page of that version, rolling-rob-3d|<scope>| */
  var scope = '|' + new URL(self.registration.scope).pathname + '|';
  e.waitUntil(self.clients.claim().then(function () {
    return caches.keys();
  }).then(function (keys) {
    return Promise.all(keys.filter(function (k) { return /^rolling-rob(-3d)?\|/.test(k) && k.indexOf(scope) > 0; })
      .map(function (k) { return caches.delete(k); }));
  }).then(function () {
    return self.registration.unregister();
  }).then(function () {
    return self.clients.matchAll({ type: 'window' });
  }).then(function (clients) {
    clients.forEach(function (c) { c.navigate(c.url); });
  }));
});
