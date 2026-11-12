// The figures of Rolling Rob 3D (Rob, slime, mine, UFO, ...), three.js.
// ---------------- materials and small helpers ----------------

// Tileable value noise on a canvas, for bumps and grain.
function noiseCanvas(n, cells, seed = 1) {
  const c = document.createElement('canvas');
  c.width = c.height = n;
  const ctx = c.getContext('2d');
  const img = ctx.createImageData(n, n);
  let s = seed;
  const rnd = () => ((s = (s * 1664525 + 1013904223) >>> 0) / 4294967296);
  const octs = [];
  for (let o = 0, cc = cells; o < 4; o++, cc *= 2) {
    const g = new Float32Array(cc * cc);
    for (let i = 0; i < g.length; i++) g[i] = rnd();
    octs.push({ cc, g });
  }
  for (let y = 0; y < n; y++)
    for (let x = 0; x < n; x++) {
      let v = 0, amp = 0.5;
      for (const { cc, g } of octs) {
        const fx = x / n * cc, fy = y / n * cc, x0 = Math.floor(fx), y0 = Math.floor(fy);
        const tx = fx - x0, ty = fy - y0, sx = tx * tx * (3 - 2 * tx), sy = ty * ty * (3 - 2 * ty);
        const at = (i, j) => g[((j % cc + cc) % cc) * cc + ((i % cc + cc) % cc)];
        const a = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * sx;
        const b = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * sx;
        v += (a + (b - a) * sy) * amp;
        amp *= 0.5;
      }
      const o = (y * n + x) * 4, k = Math.max(0, Math.min(255, v * 255 / 0.94));
      img.data[o] = img.data[o + 1] = img.data[o + 2] = k;
      img.data[o + 3] = 255;
    }
  ctx.putImageData(img, 0, 0);
  return c;
}
function noiseTex(n, cells, seed, repeat = 1) {
  const t = new THREE.CanvasTexture(noiseCanvas(n, cells, seed));
  t.wrapS = t.wrapT = THREE.RepeatWrapping;
  t.repeat.set(repeat, repeat);
  return t;
}

const std = (color, rough = 0.6, metal = 0, extra = {}) =>
  new THREE.MeshStandardMaterial({ color, roughness: rough, metalness: metal, ...extra });
const phys = (color, extra = {}) => new THREE.MeshPhysicalMaterial({ color, ...extra });

function mesh(geo, mat, shadows = true) {
  const m = new THREE.Mesh(geo, mat);
  m.castShadow = m.receiveShadow = shadows;
  return m;
}

// A point on the unit sphere facing direction d, as a vector.
const onSphere = (x, y, z, r = 1) => new THREE.Vector3(x, y, z).normalize().multiplyScalar(r);


// Points along a list of 3D points, as a tube.
const tube = (pts, r, mat, closed = false) =>
  mesh(new THREE.TubeGeometry(new THREE.CatmullRomCurve3(pts, closed), pts.length * 8, r, 10, closed), mat);

// A soft round glow on a card (the original's translucent halos), added
// behind or around a light; it does not cast or take shadows.
function glowCard(color, size, opacity = 0.5) {
  const c = document.createElement('canvas');
  c.width = c.height = 128;
  const ctx = c.getContext('2d');
  const g = ctx.createRadialGradient(64, 64, 0, 64, 64, 64);
  const col = new THREE.Color(color);
  const rgb = `${Math.round(col.r * 255)},${Math.round(col.g * 255)},${Math.round(col.b * 255)}`;
  g.addColorStop(0, `rgba(${rgb},1)`); g.addColorStop(0.45, `rgba(${rgb},0.45)`); g.addColorStop(1, `rgba(${rgb},0)`);
  ctx.fillStyle = g; ctx.fillRect(0, 0, 128, 128);
  const t = new THREE.CanvasTexture(c);
  t.colorSpace = THREE.SRGBColorSpace;
  const m = new THREE.Mesh(new THREE.PlaneGeometry(size, size),
    new THREE.MeshBasicMaterial({ map: t, transparent: true, opacity, depthWrite: false, toneMapped: false }));
  return m;
}

const FROM_FRONT = new THREE.Vector3(0, 0.22, 1); // a little from above

// A ground under a figure that only takes its shadow.
function shadowGround(size = 6) {
  if (LIGHT) return contactShadow();
  const p = mesh(new THREE.PlaneGeometry(size, size), new THREE.ShadowMaterial({ opacity: 0.35 }));
  p.rotation.x = -Math.PI / 2;
  p.castShadow = false;
  return p;
}

// ---------------- Rob ----------------

// The original's Rob (draw.c, draw_rob), radius 30 units = 1 here, facing
// us: a glossy black helmet of a ball, a black cloak showing below it,
// yellow eyes with red cross-hairs, a gold grin with white teeth, a grille
// at the chin, a cigarette to the right with its smoke.
function makeRob() {
  const rob = new THREE.Group(), u = 1 / 30;
  // the 2D picture's (x, y) (y down) on the ball's front, lifted a little
  const face = (x, y, lift = 0) => {
    const X = x * u, Y = -y * u, Z = Math.sqrt(Math.max(0, 1 - X * X - Y * Y));
    return new THREE.Vector3(X, Y, Z).multiplyScalar(1 + lift);
  };
  const onFace = (m, x, y, lift = 0) => {
    const p = face(x, y, lift);
    m.position.copy(p);
    m.lookAt(p.clone().multiplyScalar(3));
    return m;
  };
  // the cape: in the original a filled arc of radius r + 5 from 45 to 135 degrees, so seen
  // from the front a black cap under the chin. Here the same outline as a cloth cowl round
  // the bottom of the ball: it grows smoothly out of the helmet and cups the underside
  const cowl = [];
  const polar = (deg, r) => new THREE.Vector2(Math.max(Math.sin(deg * Math.PI / 180) * r, 1e-4), -Math.cos(deg * Math.PI / 180) * r);
  for (let d = 45; d >= 0; d -= 1.5) { // close to the ball at the edge, full height from about 26 degrees in
    const t = Math.min(1, Math.max(0, (d - 26) / 19)), s = t * t * (3 - 2 * t);
    cowl.push(polar(d, 0.992 + (35 * u + 0.01 - 0.992) * (1 - s)));
  }
  const cowlGeo = new THREE.LatheGeometry(cowl, 96), cp = cowlGeo.attributes.position, cv = new THREE.Vector3();
  for (let i = 0; i < cp.count; i++) { // soft folds, strongest towards the rim
    cv.fromBufferAttribute(cp, i);
    const a = Math.atan2(cv.z, cv.x), rim = Math.hypot(cv.x, cv.z) / 0.72;
    cv.multiplyScalar(1 + rim * rim * (0.012 * Math.sin(a * 11) + 0.007 * Math.sin(a * 23 + 1)));
    cp.setXYZ(i, cv.x, cv.y, cv.z);
  }
  cowlGeo.computeVertexNormals();
  rob.add(mesh(cowlGeo, phys(0x101012, { roughness: 0.82, sheen: 1, sheenColor: new THREE.Color(0x4a4a5c), sheenRoughness: 0.45, side: THREE.DoubleSide })));

  // the helmet
  rob.add(mesh(new THREE.SphereGeometry(1, 96, 64),
    phys(0x161618, { roughness: 0.34, metalness: 0.3, clearcoat: 0.75, clearcoatRoughness: 0.22 })));

  // eyes: yellow lenses, a red cross-hair over each, a black pupil
  const yellow = phys(0xffd700, { emissive: 0xffb800, emissiveIntensity: 0.55, roughness: 0.2, clearcoat: 1 });
  const red = std(0xff1a10, 0.4, 0, { emissive: 0xc00000, emissiveIntensity: 0.4 });
  for (const x of [-8, 8]) {
    const lens = onFace(mesh(new THREE.SphereGeometry(5 * u, 32, 20), yellow), x, -5, -0.02);
    lens.scale.z = 0.45; rob.add(lens);
    const cross = new THREE.Group();
    cross.add(mesh(new THREE.BoxGeometry(4.4 * u, 0.9 * u, 0.6 * u), red));
    cross.add(mesh(new THREE.BoxGeometry(0.9 * u, 4.4 * u, 0.6 * u), red));
    const pupil = mesh(new THREE.SphereGeometry(1.9 * u, 16, 12), std(0x050505, 0.15));
    pupil.scale.z = 0.4; pupil.position.z = 0.3 * u;
    cross.add(pupil);
    rob.add(onFace(cross, x, -5, 0.075));
  }
  // the grin: gold, an arc of radius 12 round (0, 3), and the teeth in it
  const gold = phys(0xffc820, { metalness: 0.95, roughness: 0.25, clearcoat: 0.6 });
  const grin = [];
  for (let i = 0; i <= 16; i++) {
    const a = 0.1 + (Math.PI - 0.2) * i / 16;
    grin.push(face(12 * Math.cos(a), 3 + 12 * Math.sin(a), 0.02));
  }
  rob.add(tube(grin, 1.1 * u, gold));
  const white = phys(0xfaf8f0, { roughness: 0.3, clearcoat: 0.8 });
  for (let i = -8; i <= 8; i += 4)
    rob.add(onFace(mesh(new RoundedBoxGeometry(3 * u, 4 * u, 1.4 * u, 2, 0.5 * u), white), i + 1.5, 10, 0.005));
  // the breathing apparatus at the chin: x -10..10, y 15..22 of the original, here a plate
  // formed to the ball with rounded edges, its ribs standing on it
  const plateP = [], plateI = [], NX = 44, NY = 18;
  for (let j = 0; j <= NY; j++) for (let i = 0; i <= NX; i++) {
    const fx = i / NX, fy = j / NY, e = Math.min(fx * 20, (1 - fx) * 20, fy * 7, (1 - fy) * 7) / 1.4; // distance from the edge, in 1.4 px
    const t = Math.min(1, e), lift = 0.006 + 0.058 * Math.sqrt(t * (2 - t));
    const p = face(-10.8 + fx * 21.6, 14.2 + fy * 8.6, lift);
    plateP.push(p.x, p.y, p.z);
    if (i < NX && j < NY) { const a = j * (NX + 1) + i, c = a + NX + 1; plateI.push(a, c, a + 1, a + 1, c, c + 1); }
  }
  const plate = new THREE.BufferGeometry();
  plate.setAttribute('position', new THREE.Float32BufferAttribute(plateP, 3));
  plate.setIndex(plateI);
  plate.computeVertexNormals();
  rob.add(mesh(plate, phys(0x2a2a2c, { metalness: 0.85, roughness: 0.38, clearcoat: 0.3 })));
  const rib = phys(0x7a7a80, { metalness: 0.9, roughness: 0.3 });
  for (let i = -8; i <= 8; i += 4) {
    const pts = [];
    for (let k = 0; k <= 6; k++) pts.push(face(i, 16 + k * 5 / 6, 0.066));
    rob.add(tube(pts, 0.65 * u, rib));
  }
  // the cigarette, sticking out to the right, its tip glowing, its smoke
  const cig = new THREE.Group();
  const paper = mesh(new THREE.CylinderGeometry(1.6 * u, 1.6 * u, 18 * u, 16), phys(0xf6f4ee, { roughness: 0.6 }));
  paper.rotation.z = Math.PI / 2; paper.position.x = 9 * u; cig.add(paper);
  const filter = mesh(new THREE.CylinderGeometry(1.65 * u, 1.65 * u, 5 * u, 16), phys(0xd89a48, { roughness: 0.7 }));
  filter.rotation.z = Math.PI / 2; filter.position.x = 2.5 * u; cig.add(filter);
  const ember = mesh(new THREE.SphereGeometry(2.2 * u, 16, 12), phys(0xff4500, { emissive: 0xff3000, emissiveIntensity: 2.2 }));
  ember.position.x = 18.5 * u; ember.scale.x = 0.7; cig.add(ember);
  const eg = glowCard(0xff6020, 12 * u, 0.6);
  eg.position.set(18.5 * u, 0, 1.5 * u); cig.add(eg);
  cig.position.set(12 * u, -6.5 * u, 0.62);
  rob.add(cig);
  // smoke: soft flat puffs that always face the camera
  const sc = document.createElement('canvas'); sc.width = sc.height = 64;
  const sg = sc.getContext('2d'), grad = sg.createRadialGradient(32, 32, 2, 32, 32, 32);
  grad.addColorStop(0, 'rgba(255,255,255,1)'); grad.addColorStop(0.5, 'rgba(255,255,255,.45)'); grad.addColorStop(1, 'rgba(255,255,255,0)');
  sg.fillStyle = grad; sg.fillRect(0, 0, 64, 64);
  const smokeTex = new THREE.CanvasTexture(sc); smokeTex.colorSpace = THREE.SRGBColorSpace;
  for (let i = 0; i < 5; i++) {
    const o = 0.5 + i * 0.5;
    const puff = new THREE.Sprite(new THREE.SpriteMaterial({ map: smokeTex, color: 0xd8d8d8, transparent: true, opacity: 0.6 - i * 0.1, depthWrite: false }));
    puff.scale.setScalar((3 + o * 2) * 2.4 * u);
    puff.position.set((30 + o * 8) * u, -(6.5 - o * 12) * u, 0.62);
    rob.add(puff);
  }
  return rob;
}

// 64 frames, 8 x 8, 128 px cells: Rob turned clockwise by k/64 of a turn
// (the game's rotation), the light staying where it is. The ball's radius
// is ROB3D_R of a cell (art.c), leaving room for the smoke.
const ROB3D_R = 0.25;
async function renderRob() {
  const cell = 128, n = 64, cols = 8;
  const scene = new THREE.Scene();
  addLights(scene, { radius: 2.5 });
  const rob = makeRob();
  scene.add(rob);
  const sheet = document.createElement('canvas');
  sheet.width = cell * cols; sheet.height = cell * (n / cols);
  const sctx = sheet.getContext('2d');
  const hh = 0.5 / ROB3D_R;
  for (let k = 0; k < n; k++) {
    rob.rotation.z = -2 * Math.PI * k / n;
    sctx.drawImage(shoot(scene, { w: cell, h: cell, hh }), (k % cols) * cell, Math.floor(k / cols) * cell);
  }
  await save('rob-3d.png', sheet);
}

// ---------------- the slime (level 1's monster) ----------------

// draw.c: a green ellipse 18 x 15 round (0, 5), black eyes 6 x 8, all at
// 1.7x. Here: a glossy green blob sitting on the ground, black eyes.
function makeSlime() {
  const g = new THREE.Group(), u = 1.7 / 25; // units of the 2D picture
  const rx = 18 * u, ry = 15 * u;
  // a lump of gel: slack and spreading out at the foot, a little uneven
  const bg = new THREE.SphereGeometry(1, 96, 64), bp = bg.attributes.position, bv = new THREE.Vector3();
  for (let i = 0; i < bp.count; i++) {
    bv.fromBufferAttribute(bp, i);
    const a = Math.atan2(bv.z, bv.x), low = Math.max(0, Math.min(1, (0.15 - bv.y) / 1.05));
    const spread = 1 + low * low * (0.16 + 0.05 * Math.sin(a * 5 + 1) + 0.03 * Math.sin(a * 9));
    const lump = 1 + 0.018 * Math.sin(bv.x * 4 + bv.y * 3) + 0.014 * Math.sin(bv.z * 5 - bv.y * 4);
    bp.setXYZ(i, bv.x * spread * lump, bv.y * lump, bv.z * spread * lump);
  }
  bg.computeVertexNormals();
  // light gets into gel: thin and bright at the edge, deep and dark in the middle
  const gel = phys(0x45b048, { roughness: 0.3, clearcoat: 1, clearcoatRoughness: 0.2, envMapIntensity: 0.55,
                               bumpMap: noiseTex(256, 5, 3, 2), bumpScale: 0.035 });
  gel.onBeforeCompile = (sh) => {
    sh.fragmentShader = sh.fragmentShader.replace('#include <emissivemap_fragment>', `#include <emissivemap_fragment>
 { float nv=clamp(dot(normalize(normal),normalize(vViewPosition)),0.,1.), rim=pow(1.-nv,2.);
   diffuseColor.rgb*=mix(.42,1.1,rim);
   totalEmissiveRadiance+=vec3(.2,.72,.14)*rim*.75+vec3(.008,.05,.012); }`);
  };
  gel.customProgramCacheKey = () => 'gel';
  const body = mesh(bg, gel);
  body.scale.set(rx, ry, rx * 0.85);
  body.position.y = ry * 0.93; // sits, a little squashed into the ground
  g.add(body);
  const black = phys(0x080808, { roughness: 0.15, clearcoat: 1 });
  // the eyes' centres in the picture: (-5, -1) and (5, -1); the ellipse's at (0, 5)
  for (const x of [-5, 5]) {
    const X = x * u, Y = (5 - -1) * u; // above the centre
    const nx = X / rx, ny = Y / ry, nz = Math.sqrt(Math.max(0, 1 - nx * nx - ny * ny));
    const e = mesh(new RoundedBoxGeometry(6 * u, 8 * u, 2 * u, 3, 1.4 * u), black);
    const p = new THREE.Vector3(X, body.position.y + Y, nz * rx * 0.85);
    e.position.copy(p);
    e.lookAt(p.x * 0.3, p.y, p.z + 5);
    g.add(e);
  }
  return g;
}

async function renderSlime() {
  const scene = new THREE.Scene();
  addLights(scene, { radius: 2.5 });
  scene.add(makeSlime());
  scene.add(shadowGround());
  await save('enemy-0.png', shoot(scene, { w: 256, h: 256, hh: 1.45, target: new THREE.Vector3(0, 0.95, 0), dir: FROM_FRONT }));
}

// ---------------- the other levels' monsters ----------------

// draw.c, draw_monster: every level has its own monster, drawn round the
// obstacle's centre at 1.7x. Same units as the slime (u = 1.7/25 per unit of
// the 2D picture) and the same frame, each standing on the ground (y = 0)
// with its lowest point in the picture (`bottom`) there. P(y) = height of
// picture row y.
const MU = 1.7 / 25;
const P = (bottom, y) => (bottom - y) * MU;

function makeCactus() { // 0x2E7D32: trunk 16 x 30 (y -15..15), arms 10 x 10 at y -5..5
  const g = new THREE.Group(), u = MU, B = 15;
  const skin = phys(0x2e7d32, { roughness: 0.45, clearcoat: 0.3, bumpMap: noiseTex(256, 18, 11, 2), bumpScale: 0.1 });
  // ribbed trunk: a lathe whose radius ripples with the angle
  const trunk = new THREE.CylinderGeometry(8 * u, 8 * u, 30 * u - 8 * u, 48, 8);
  const pos = trunk.attributes.position;
  for (let i = 0; i < pos.count; i++) {
    const x = pos.getX(i), z = pos.getZ(i), a = Math.atan2(z, x), k = 1 + 0.07 * Math.cos(a * 8);
    pos.setX(i, x * k); pos.setZ(i, z * k * 0.8);
  }
  trunk.computeVertexNormals();
  const t = mesh(trunk, skin); t.position.y = P(B, 0); g.add(t);
  const cap = mesh(new THREE.SphereGeometry(8 * u, 48, 24, 0, Math.PI * 2, 0, Math.PI / 2), skin);
  cap.scale.z = 0.8; cap.position.y = P(B, -15) - 4 * u; g.add(cap);
  for (const sx of [-1, 1]) {
    const arm = mesh(new RoundedBoxGeometry(10 * u, 10 * u, 8 * u, 4, 3 * u), skin);
    arm.position.set(sx * 10 * u, P(B, 0), 0); g.add(arm);
  }
  const spine = std(0xf2ecd0, 0.5);
  for (let i = 0; i < 40; i++) { // little pale spines
    const a = (i * 2.399) % (Math.PI * 2), y = P(B, -13 + (i * 7.3) % 26), r = 8.3 * u;
    if (Math.cos(a) < -0.3) continue;
    const c = mesh(new THREE.ConeGeometry(0.25 * u, 2.2 * u, 5), spine, false);
    const d = new THREE.Vector3(Math.sin(a), 0, Math.cos(a));
    c.position.set(d.x * r, y, d.z * r * 0.8);
    c.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), d);
    g.add(c);
  }
  return g;
}

function makeYeti() { // 0xE0E0E0 body 20 x 25 (y -10..15), black eyes 4 x 4, grey mouth 6 x 3
  const g = new THREE.Group(), u = MU, B = 15;
  const fur = phys(0xe8e8ec, { roughness: 0.9, sheen: 1, sheenColor: 0xffffff, sheenRoughness: 0.6,
                               bumpMap: noiseTex(256, 40, 21, 2), bumpScale: 0.25 });
  const body = mesh(new RoundedBoxGeometry(20 * u, 25 * u, 16 * u, 6, 5 * u), fur);
  body.position.y = P(B, 2.5); g.add(body);
  const black = phys(0x080808, { roughness: 0.15, clearcoat: 1 });
  for (const x of [-6, 6]) {
    const e = mesh(new RoundedBoxGeometry(4 * u, 4 * u, 2 * u, 2, 1 * u), black);
    e.position.set(x * u, P(B, -6), 8 * u); g.add(e);
  }
  const m = mesh(new RoundedBoxGeometry(6 * u, 3 * u, 2 * u, 2, 0.8 * u), phys(0x555555, { roughness: 0.4 }));
  m.position.set(0, P(B, 1.5), 8 * u); g.add(m);
  return g;
}

// Lava crust: dark rock, orange cracks (emissive: only the cracks, lit).
function crackTex(glowOnly) {
  const n = 512, c = document.createElement('canvas');
  c.width = c.height = n;
  const ctx = c.getContext('2d');
  ctx.fillStyle = glowOnly ? '#000' : '#3a2622';
  ctx.fillRect(0, 0, n, n);
  let seed = 7;
  const rnd = () => (seed = (seed * 16807) % 2147483647) / 2147483647;
  if (!glowOnly) for (let i = 0; i < 400; i++) { // grain
    ctx.fillStyle = `rgba(${90 + rnd() * 40},${60 + rnd() * 30},${55 + rnd() * 25},0.35)`;
    ctx.fillRect(rnd() * n, rnd() * n, 3 + rnd() * 10, 3 + rnd() * 10);
  }
  ctx.lineCap = 'round';
  for (let i = 0; i < 26; i++) {
    let x = rnd() * n, y = rnd() * n, a = rnd() * Math.PI * 2;
    ctx.beginPath(); ctx.moveTo(x, y);
    for (let k = 0; k < 9; k++) { a += (rnd() - 0.5) * 1.6; x += Math.cos(a) * 22; y += Math.sin(a) * 22; ctx.lineTo(x, y); }
    ctx.strokeStyle = '#ff7a20'; ctx.lineWidth = 2 + rnd() * 4; ctx.shadowColor = '#ff4000'; ctx.shadowBlur = 10;
    ctx.stroke();
  }
  const t = new THREE.CanvasTexture(c);
  t.colorSpace = THREE.SRGBColorSpace;
  t.wrapS = t.wrapT = THREE.RepeatWrapping;
  return t;
}

function makeLava() { // 0xFF4500: three spikes, peaks at x -10, 0, 10 (y -15), bases y 15
  const g = new THREE.Group(), u = MU, B = 15;
  // dark crust with glowing cracks, like the lava spikes in the painted
  // levels; the silhouette stays the original's three spikes
  const lava = phys(0xffffff, { map: crackTex(false), emissiveMap: crackTex(true), emissive: 0xff5a10, emissiveIntensity: 1.4,
                                roughness: 0.7, bumpMap: noiseTex(256, 14, 32, 2), bumpScale: 0.25, flatShading: true });
  for (const x of [-10, 0, 10]) {
    const c = mesh(new THREE.ConeGeometry(5.5 * u, 30 * u, 7, 4), lava);
    c.position.set(x * u, P(B, 0), 0); c.rotation.y = x * 0.37; g.add(c);
  }
  const halo = glowCard(0xff5010, 50 * u, 0.35); halo.position.set(0, P(B, 2), -6 * u); g.add(halo);
  const glow = new THREE.PointLight(0xff4010, 3, 40 * u, 2); glow.position.set(0, P(B, 8), 10 * u); g.add(glow);
  return g;
}

function makePlant() { // 0x1B5E20 stem 10 x 20 (y -5..15), red rings r 15..3 round (0, -10)
  const g = new THREE.Group(), u = MU, B = 15;
  const leaf = phys(0x1b5e20, { roughness: 0.5, clearcoat: 0.4 });
  const stem = mesh(new THREE.CylinderGeometry(4 * u, 5 * u, 20 * u, 24), leaf);
  stem.position.y = P(B, 5); g.add(stem);
  const head = mesh(new THREE.SphereGeometry(3 * u, 24, 16), phys(0x8a0000, { roughness: 0.3, clearcoat: 1 }));
  head.position.y = P(B, -10); head.position.z = 1 * u; g.add(head);
  const red = phys(0xe01010, { roughness: 0.3, clearcoat: 0.8, emissive: 0x400000, emissiveIntensity: 0.4 });
  for (let i = 0; i < 5; i++) {
    const r = (15 - i * 3) * u;
    const ring = mesh(new THREE.TorusGeometry(r, 0.7 * u, 10, 64), red);
    ring.position.set(0, P(B, -10), (i - 2) * 0.6 * u); g.add(ring);
  }
  return g;
}

function makeAlien() { // 0x00FF00 ellipse 12 x 15 round (0, -5), black eyes r 4 at (±5, -8)
  const g = new THREE.Group(), u = MU, B = 10;
  const rx = 12 * u, ry = 15 * u;
  const body = mesh(new THREE.SphereGeometry(1, 96, 64),
    phys(0x00ff00, { roughness: 0.25, clearcoat: 0.8, emissive: 0x003300, emissiveIntensity: 0.4, bumpMap: noiseTex(256, 6, 41, 2), bumpScale: 0.06 }));
  body.scale.set(rx, ry, rx * 0.85); body.position.y = ry; g.add(body);
  const black = phys(0x050505, { roughness: 0.05, clearcoat: 1 });
  for (const x of [-5, 5]) {
    const X = x * u, Y = (-5 - -8) * u, nx = X / rx, ny = Y / ry, nz = Math.sqrt(Math.max(0, 1 - nx * nx - ny * ny));
    const e = mesh(new THREE.SphereGeometry(4 * u, 32, 24), black);
    e.scale.z = 0.45; e.position.set(X, ry + Y, nz * rx * 0.85); g.add(e);
  }
  return g;
}

function makeCandy() { // 0xFF69B4 ball r 15, white dots r 3 at (-5, -5) and (5, 5)
  const g = new THREE.Group(), u = MU, B = 15;
  const ball = mesh(new THREE.SphereGeometry(15 * u, 96, 64), phys(0xff69b4, { roughness: 0.15, clearcoat: 1, clearcoatRoughness: 0.05 }));
  ball.position.y = 15 * u; g.add(ball);
  const white = phys(0xffffff, { roughness: 0.3, clearcoat: 1 });
  for (const [x, y] of [[-5, -5], [5, 5]]) {
    const d = onSphere(x, -y, Math.sqrt(225 - x * x - y * y), 15 * u);
    const s = mesh(new THREE.SphereGeometry(3 * u, 24, 16), white);
    s.scale.set(1, 1, 0.35); s.position.copy(d).add(new THREE.Vector3(0, 15 * u, 0)); s.lookAt(d.clone().multiplyScalar(3).add(new THREE.Vector3(0, 15 * u, 0)));
    g.add(s);
  }
  return g;
}

function makeRobot() { // 0x607D8B body 20 x 24 (y -12..12), cyan eyes 5 x 5 at (-7|2, -8), mouth 10 x 3 at (-5, 2)
  const g = new THREE.Group(), u = MU, B = 12;
  const body = mesh(new RoundedBoxGeometry(20 * u, 24 * u, 16 * u, 4, 2 * u),
    phys(0x607d8b, { metalness: 0.85, roughness: 0.3, clearcoat: 0.5, bumpMap: noiseTex(256, 30, 51, 2), bumpScale: 0.03 }));
  body.position.y = P(B, 0); g.add(body);
  const cyan = phys(0x00ffff, { emissive: 0x00ffff, emissiveIntensity: 1.6, roughness: 0.1 });
  const parts = [[-7, -8, 5, 5], [2, -8, 5, 5], [-5, 2, 10, 3]];
  for (const [x, y, w, h] of parts) {
    const p = mesh(new RoundedBoxGeometry(w * u, h * u, 1.5 * u, 2, 0.5 * u), cyan, false);
    p.position.set((x + w / 2) * u, P(B, y + h / 2), 8 * u); g.add(p);
    const gl = glowCard(0x00ffff, Math.max(w, h) * 2.2 * u, 0.4); gl.position.set((x + w / 2) * u, P(B, y + h / 2), 8.9 * u); g.add(gl);
  }
  for (const sx of [-1, 1]) { // bolts
    const b = mesh(new THREE.CylinderGeometry(1.2 * u, 1.2 * u, 1 * u, 12), phys(0x9aaab4, { metalness: 1, roughness: 0.25 }));
    b.rotation.x = Math.PI / 2; b.position.set(sx * 7.5 * u, P(B, 8), 8.2 * u); g.add(b);
  }
  return g;
}

function makeDemon() { // 0x8B0000 cube 20 x 20 (y -10..10), red horns (-12,-12)-(-8,-18)-(-4,-12) and mirrored
  const g = new THREE.Group(), u = MU, B = 10;
  const body = mesh(new RoundedBoxGeometry(20 * u, 20 * u, 16 * u, 5, 3 * u),
    phys(0x8b0000, { roughness: 0.4, clearcoat: 0.5, bumpMap: noiseTex(256, 16, 61, 2), bumpScale: 0.12 }));
  body.position.y = P(B, 0); g.add(body);
  const horn = phys(0xff1a1a, { roughness: 0.25, clearcoat: 1 });
  for (const sx of [-1, 1]) {
    const h = mesh(new THREE.ConeGeometry(4 * u, 7 * u, 24), horn);
    h.position.set(sx * 8 * u, P(B, -14.5), 0); h.rotation.z = -sx * 0.15; g.add(h);
  }
  return g;
}

function makeCrystal() { // 0x9C27B0 (0,-15) (-10,5) (-3,15) (3,15) (10,5), white outline
  const g = new THREE.Group(), u = MU, B = 15;
  // the outline turned round its axis in six facets: bottom (±3, 15), widest (±10, 5), tip (0, -15)
  const geo = new THREE.LatheGeometry([new THREE.Vector2(0, 0), new THREE.Vector2(3 * u, 0),
    new THREE.Vector2(10 * u, P(B, 5)), new THREE.Vector2(0, P(B, -15))], 6);
  const gem = mesh(geo, phys(0x9c27b0, { roughness: 0.05, metalness: 0, clearcoat: 1, transmission: 0.35, thickness: 0.5, ior: 1.7,
                                         emissive: 0x3a0050, emissiveIntensity: 0.6, flatShading: true }));
  gem.scale.z = 0.7; gem.rotation.y = Math.PI / 6; g.add(gem);
  const edges = new THREE.LineSegments(new THREE.EdgesGeometry(geo, 20), new THREE.LineBasicMaterial({ color: 0xffffff }));
  edges.scale.z = 0.7; edges.rotation.y = Math.PI / 6; g.add(edges);
  const glow = glowCard(0xd070ff, 40 * u, 0.25); glow.position.set(0, P(B, 0), -8 * u); g.add(glow);
  return g;
}

const MONSTERS = [makeSlime, makeCactus, makeYeti, makeLava, makePlant, makeAlien, makeCandy, makeRobot, makeDemon, makeCrystal];

async function renderMonster(i) {
  const scene = new THREE.Scene();
  addLights(scene, { radius: 2.5 });
  scene.add(MONSTERS[i]());
  scene.add(shadowGround());
  // the plant's rings reach higher than the frame: it is framed higher
  // and so stands a little lower, its stem rooted in the ground
  const ty = i === 4 ? 1.3 : 0.95;
  await save(`enemy-${i}.png`, shoot(scene, { w: 256, h: 256, hh: 1.45, target: new THREE.Vector3(0, ty, 0), dir: FROM_FRONT }));
}

async function renderMonsters() {
  for (let i = 1; i < MONSTERS.length; i++) await renderMonster(i);
}

// ---------------- the mine ----------------

// draw.c: a dark ball r 20, eight grey spokes to 1.5 r, a pulsing red core
// r/2 and a red halo 1.5 r. Radius 20 = 1 here.
function makeMine() {
  const g = new THREE.Group();
  g.add(mesh(new THREE.SphereGeometry(1, 64, 48),
    phys(0x333333, { metalness: 0.8, roughness: 0.4, clearcoat: 0.4, bumpMap: noiseTex(256, 10, 5, 2), bumpScale: 0.25 })));
  const steel = phys(0x8a8a8a, { metalness: 0.95, roughness: 0.3 });
  for (let k = 0; k < 8; k++) {
    const a = k / 8 * Math.PI * 2, d = new THREE.Vector3(Math.cos(a), -Math.sin(a), 0);
    const rod = mesh(new THREE.CylinderGeometry(0.09, 0.12, 0.7, 12), steel);
    rod.position.copy(d.clone().multiplyScalar(1.15));
    rod.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), d);
    g.add(rod);
    const tip = mesh(new THREE.SphereGeometry(0.11, 12, 8), steel);
    tip.position.copy(d.clone().multiplyScalar(1.5));
    g.add(tip);
    const bolt = mesh(new THREE.CylinderGeometry(0.17, 0.17, 0.1, 16), steel);
    bolt.position.copy(d.clone().multiplyScalar(0.98));
    bolt.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), d);
    g.add(bolt);
  }
  const core = mesh(new THREE.SphereGeometry(0.5, 48, 32),
    phys(0xff1a10, { emissive: 0xff1000, emissiveIntensity: 1.6, roughness: 0.1, clearcoat: 1 }));
  core.position.z = 0.88; core.scale.z = 0.5; g.add(core);
  const halo = glowCard(0xff2010, 3.4, 0.45);
  halo.position.z = -1.3; g.add(halo);
  const glow = new THREE.PointLight(0xff2010, 4, 2.5, 2);
  glow.position.set(0, 0, 1.4); g.add(glow);
  return g;
}

// names: level 1 calls it mine-0, the other levels mine
async function renderMine(names = ['mine-0.png', 'mine.png']) {
  const scene = new THREE.Scene();
  addLights(scene, { radius: 2.5 });
  scene.add(makeMine());
  const pic = shoot(scene, { w: 192, h: 192, hh: 1.75 });
  for (const n of names) await save(n, pic); // the same design on every level
}

// ---------------- the saucer ----------------

// draw.c: a grey lens 40 x 16, a cyan dome 20 x 14 on it, red, green and
// blue lights below, an antenna with a red ball. 40 units = 1 here.
function makeUfo() {
  const g = new THREE.Group(), u = 1 / 40;
  const lens = mesh(new THREE.SphereGeometry(1, 96, 48), phys(0xb8b8bc, { metalness: 0.8, roughness: 0.3, clearcoat: 0.8, clearcoatRoughness: 0.2 }));
  lens.scale.set(1, 16 * u, 1); g.add(lens);
  const rim = mesh(new THREE.TorusGeometry(1.0, 0.035, 12, 96), phys(0x606060, { metalness: 1, roughness: 0.35 }));
  rim.rotation.x = Math.PI / 2; g.add(rim);
  const dome = mesh(new THREE.SphereGeometry(1, 64, 32, 0, Math.PI * 2, 0, Math.PI / 2),
    phys(0x00c0d8, { roughness: 0.05, metalness: 0.1, clearcoat: 1, envMapIntensity: 0.6, emissive: 0x007a8a, emissiveIntensity: 0.7 }));
  dome.scale.set(20 * u, 14 * u, 20 * u); dome.position.y = 10 * u; g.add(dome);
  const ant = mesh(new THREE.CylinderGeometry(0.02, 0.025, 8 * u, 8), phys(0x909090, { metalness: 0.9, roughness: 0.3 }));
  ant.position.y = 28 * u; g.add(ant);
  const tip = mesh(new THREE.SphereGeometry(3 * u, 16, 12), phys(0xff0000, { emissive: 0xff0000, emissiveIntensity: 1.2 }));
  tip.position.y = 34 * u; g.add(tip);
  const lc = [0xff2020, 0x20ff40, 0x3050ff];
  for (let i = 0; i < 3; i++) {
    const x = (-24 + 24 * i) * u, y = -5 * u, z = Math.sqrt(Math.max(0, 1 - x * x - (y / (16 * u)) ** 2)) * 0.98;
    const l = mesh(new THREE.SphereGeometry(5 * u, 20, 14), phys(lc[i], { emissive: lc[i], emissiveIntensity: 1.8, roughness: 0.2 }));
    l.position.set(x, y, z); l.scale.z = 0.6; g.add(l);
    const h = glowCard(lc[i], 22 * u, 0.6);
    h.position.set(x, y, z + 0.05); g.add(h);
  }
  return g;
}

async function renderUfo(names = ['ufo-0.png', 'ufo.png']) {
  const scene = new THREE.Scene();
  addLights(scene, { radius: 2.5 });
  scene.add(makeUfo());
  const pic = shoot(scene, { w: 256, h: 160, hh: 0.68, target: new THREE.Vector3(0, 0.22, 0), dir: new THREE.Vector3(0, 0.3, 1) });
  for (const n of names) await save(n, pic);
}

// ---------------- Julian ----------------

// draw.c: a dark red block 100 x 120 with three darker bands, gold eyes
// with red pupils, a black grin, white teeth, two small horns. 60 = 1 here.
function makeJulian() {
  const g = new THREE.Group(), u = 1 / 60;
  const body = mesh(new RoundedBoxGeometry(100 * u, 120 * u, 80 * u, 6, 8 * u),
    phys(0x780000, { roughness: 0.55, clearcoat: 0.4, clearcoatRoughness: 0.4, envMapIntensity: 0.5, bumpMap: noiseTex(256, 12, 9, 2), bumpScale: 0.3 }));
  body.position.y = 60 * u; g.add(body);
  const front = 40 * u, y0 = 60 * u; // the front face; the centre's height
  const band = phys(0x420000, { roughness: 0.6, clearcoat: 0.3, envMapIntensity: 0.5 });
  for (const cy of [-40, 0, 40]) {
    const b = mesh(new RoundedBoxGeometry(90 * u, 30 * u, 4 * u, 3, 1.5 * u), band);
    b.position.set(0, y0 - cy * u, front + 0.5 * u); g.add(b);
  }
  const gold = phys(0xffd700, { metalness: 0.9, roughness: 0.25, clearcoat: 0.6 });
  for (const x of [-20, 20]) {
    const e = mesh(new THREE.SphereGeometry(8 * u, 32, 20), gold);
    e.position.set(x * u, y0 + 40 * u, front + 3 * u); e.scale.z = 0.6; g.add(e);
    const p = mesh(new THREE.SphereGeometry(4 * u, 20, 14), phys(0xff0000, { emissive: 0xcc0000, emissiveIntensity: 0.8, clearcoat: 1 }));
    p.position.set(x * u, y0 + 40 * u, front + 7 * u); p.scale.z = 0.5; g.add(p);
  }
  // the grin: an arc of radius 15 round (0, -20), the teeth along it
  const grin = [];
  for (let i = 0; i <= 14; i++) {
    const a = 0.2 + (Math.PI - 0.4) * i / 14;
    grin.push(new THREE.Vector3(15 * Math.cos(a) * u, y0 - (-20 + 15 * Math.sin(a)) * u, front + 3 * u));
  }
  g.add(tube(grin, 1.6 * u, phys(0x050505, { roughness: 0.3 })));
  const white = phys(0xfdfdf6, { roughness: 0.3, clearcoat: 0.8 });
  for (let i = -12; i <= 12; i += 6) {
    const t = mesh(new THREE.ConeGeometry(3 * u, 5 * u, 12), white);
    t.position.set(i * u, y0 + 7.5 * u, front + 3 * u); g.add(t);
  }
  // horns from the top corners, out and up
  for (const s of [-1, 1]) {
    const h = mesh(new THREE.ConeGeometry(8 * u, 26 * u, 20), phys(0x4d0000, { roughness: 0.4, clearcoat: 0.6 }));
    h.position.set(s * 42 * u, 128 * u, 0);
    h.rotation.z = -s * 0.45;
    g.add(h);
  }
  return g;
}

async function renderJulian() {
  const scene = new THREE.Scene();
  addLights(scene, { radius: 3 });
  scene.add(makeJulian());
  scene.add(shadowGround(8));
  await save('boss.png', shoot(scene, { w: 256, h: 288, hh: 1.38, target: new THREE.Vector3(0, 1.18, 0), dir: FROM_FRONT }));
}

// ---------------- level 1: no trees on the ridge ----------------
