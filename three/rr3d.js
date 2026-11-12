// Rolling Rob 3D: a three.js world behind the original 2D game.
//
// The perspective camera is placed so that the plane z = 0 matches the 2D
// canvas pixel for pixel: a point (x, y) of the game (y down) is (x, -y, 0)
// here, at any camera position. The original draws Rob, the monsters, mines,
// saucers, the finish and the HUD on its own canvas on top, unchanged, and
// its physics is untouched; this replaces only drawSky() and the fill of
// drawMountain(). Everything behind z = 0 moves slower as the camera pans:
// real parallax instead of the original's cameraX * 0.1 layers.
//
// It reads the original's globals (cameraX, cameraY, mountainPoints,
// finishLine, levels) and uses its own seeded random numbers, so the game's
// Math.random() sequence is as before.
(function () {
    'use strict';
    const T = THREE; // three.js 0.160 (module); without it the original draws as before

    const D = 1400;                                    // camera distance to z = 0
    const FOV = 2 * Math.atan(540 / D) * 180 / Math.PI; // 1080 high at z = 0

    let renderer, scene, camera, world, sky, pmrem, envRT = null, t0 = performance.now(), last = 0;
    let updaters = [], timed = [];

    // ---------- small helpers ----------
    // three's colour management turns sRGB hex into linear by itself
    const lin = (hex) => new T.Color(hex);
    const srgb = lin;
    let seed = 1;
    const rnd = () => (seed = (seed * 16807) % 2147483647, (seed - 1) / 2147483646);
    const rr = (a, b) => a + (b - a) * rnd();
    function hash2(x, y) { const s = Math.sin(x * 127.1 + y * 311.7) * 43758.5453; return s - Math.floor(s); }
    function vnoise(x, y) {
        const xi = Math.floor(x), yi = Math.floor(y), xf = x - xi, yf = y - yi;
        const u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf);
        const a = hash2(xi, yi), b = hash2(xi + 1, yi), c = hash2(xi, yi + 1), d = hash2(xi + 1, yi + 1);
        return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
    }
    function fbm(x, y, o = 4) { let s = 0, a = 0.5; for (let i = 0; i < o; i++) { s += a * vnoise(x, y); x = x * 2.03 + 1.7; y = y * 2.03 + 9.2; a *= 0.5; } return s; }

    const GLSL_NOISE = `
float hash(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
float noise(vec2 p){vec2 i=floor(p),f=fract(p);vec2 u=f*f*(3.-2.*f);
 return mix(mix(hash(i),hash(i+vec2(1.,0.)),u.x),mix(hash(i+vec2(0.,1.)),hash(i+vec2(1.,1.)),u.x),u.y);}
float fbm(vec2 p){float v=0.,a=.5;for(int i=0;i<5;i++){v+=a*noise(p);p=p*2.03+vec2(1.7,9.2);a*=.5;}return v;}
vec3 hue(float h){return .5+.5*cos(6.2831853*(h+vec3(0.,.33,.67)));}
`;

    // A shader material with fog and the output encoding, like three's own.
    // `decl`: the fragment's uniform declarations; `frag` sets vec4 outc.
    function shaderMat(decl, frag, uniforms, opt = {}) {
        const u = T.UniformsUtils.merge([T.UniformsLib.fog]);
        u.uTime = { value: 0 };
        Object.assign(u, uniforms);
        const m = new T.ShaderMaterial({
            uniforms: u,
            fog: opt.fog !== false,
            transparent: !!opt.transparent,
            depthWrite: opt.depthWrite !== undefined ? opt.depthWrite : !opt.transparent,
            blending: opt.additive ? T.AdditiveBlending : T.NormalBlending,
            side: opt.side || T.FrontSide,
            vertexShader: `varying vec2 vUv; varying vec3 vW;
#include <fog_pars_vertex>
void main(){ vUv=uv; vec4 w=modelMatrix*vec4(position,1.); vW=w.xyz;
 vec4 mvPosition=viewMatrix*w; gl_Position=projectionMatrix*mvPosition;
#include <fog_vertex>
}`,
            fragmentShader: `uniform float uTime; varying vec2 vUv; varying vec3 vW; ${decl}
${GLSL_NOISE}
#include <fog_pars_fragment>
void main(){ vec4 outc=vec4(0.,0.,0.,1.);
${frag}
 gl_FragColor=linearToOutputTexel(outc);
#include <fog_fragment>
}`,
        });
        if (opt.polygonOffset) { m.polygonOffset = true; m.polygonOffsetFactor = -2; m.polygonOffsetUnits = -2; }
        timed.push(m);
        return m;
    }

    // Soft textures drawn once.
    function canvasTex(w, h, draw) {
        const c = document.createElement('canvas'); c.width = w; c.height = h;
        draw(c.getContext('2d'), w, h);
        const t = new T.CanvasTexture(c); t.colorSpace = T.SRGBColorSpace; return t;
    }
    let TEX = null;
    function textures() {
        if (TEX) return TEX;
        TEX = {
            dot: canvasTex(64, 64, (g, w) => {
                const r = g.createRadialGradient(w / 2, w / 2, 0, w / 2, w / 2, w / 2);
                r.addColorStop(0, 'rgba(255,255,255,1)'); r.addColorStop(0.35, 'rgba(255,255,255,0.6)'); r.addColorStop(1, 'rgba(255,255,255,0)');
                g.fillStyle = r; g.fillRect(0, 0, w, w);
            }),
            cloud: canvasTex(256, 128, (g, w, h) => {
                for (let i = 0; i < 26; i++) {
                    const x = 40 + Math.random() * 176, y = 50 + Math.random() * 40, r = 18 + Math.random() * 34;
                    const rg = g.createRadialGradient(x, y, 0, x, y, r);
                    rg.addColorStop(0, 'rgba(255,255,255,0.55)'); rg.addColorStop(1, 'rgba(255,255,255,0)');
                    g.fillStyle = rg; g.fillRect(0, 0, w, h);
                }
            }),
            smoke: canvasTex(128, 128, (g, w) => {
                for (let i = 0; i < 18; i++) {
                    const x = 34 + Math.random() * 60, y = 34 + Math.random() * 60, r = 16 + Math.random() * 30;
                    const rg = g.createRadialGradient(x, y, 0, x, y, r);
                    rg.addColorStop(0, 'rgba(255,255,255,0.35)'); rg.addColorStop(1, 'rgba(255,255,255,0)');
                    g.fillStyle = rg; g.fillRect(0, 0, w, w);
                }
            }),
        };
        return TEX;
    }

    // Geometry pieces merged into one non-indexed geometry with vertex colours.
    function merge(parts) {
        const pos = [], nor = [], col = [];
        for (const p of parts) {
            let g = p.geo.index ? p.geo.toNonIndexed() : p.geo;
            if (p.m) g.applyMatrix4(p.m);
            g.computeVertexNormals();
            const P = g.attributes.position.array, N = g.attributes.normal.array;
            for (let i = 0; i < P.length; i += 3) {
                pos.push(P[i], P[i + 1], P[i + 2]); nor.push(N[i], N[i + 1], N[i + 2]);
                const c = typeof p.color === 'function' ? p.color(P[i], P[i + 1], P[i + 2], N[i], N[i + 1], N[i + 2]) : p.color;
                col.push(c.r, c.g, c.b);
            }
        }
        const g = new T.BufferGeometry();
        g.setAttribute('position', new T.Float32BufferAttribute(pos, 3));
        g.setAttribute('normal', new T.Float32BufferAttribute(nor, 3));
        g.setAttribute('color', new T.Float32BufferAttribute(col, 3));
        return g;
    }
    const M4 = (x, y, z, rx = 0, ry = 0, rz = 0, s = 1) =>
        new T.Matrix4().compose(new T.Vector3(x, y, z), new T.Quaternion().setFromEuler(new T.Euler(rx, ry, rz)), new T.Vector3(s, s, s));

    // A strip along points, facing the given normal; uv.x across, uv.y along.
    function ribbon(pts, widthAt, normalAt) {
        const n = pts.length, pos = [], uv = [], idx = [], lens = [0];
        for (let i = 1; i < n; i++) lens.push(lens[i - 1] + pts[i].distanceTo(pts[i - 1]));
        const L = lens[n - 1] || 1;
        const tng = new T.Vector3(), side = new T.Vector3();
        for (let i = 0; i < n; i++) {
            tng.subVectors(pts[Math.min(i + 1, n - 1)], pts[Math.max(i - 1, 0)]).normalize();
            const nr = normalAt ? normalAt(pts[i], i / (n - 1)) : new T.Vector3(0, 0, 1);
            side.crossVectors(tng, nr).normalize();
            const w = widthAt(i / (n - 1)) / 2;
            pos.push(pts[i].x - side.x * w, pts[i].y - side.y * w, pts[i].z - side.z * w,
                pts[i].x + side.x * w, pts[i].y + side.y * w, pts[i].z + side.z * w);
            uv.push(0, lens[i] / L, 1, lens[i] / L);
            if (i < n - 1) { const a = i * 2; idx.push(a, a + 1, a + 2, a + 1, a + 3, a + 2); }
        }
        const g = new T.BufferGeometry();
        g.setAttribute('position', new T.Float32BufferAttribute(pos, 3));
        g.setAttribute('uv', new T.Float32BufferAttribute(uv, 2));
        g.setIndex(idx);
        return { geo: g, length: L };
    }

    // ---------- textures that come with the page (window.RR_TEX, made by build.py) ----------
    // Without them (or with ?tex=0) a level looks as it always did. They are decoded once
    // and kept over level changes. Pixels go up as raw data, so see-through foliage keeps a
    // leaf colour under its alpha (no dark rim in the mipmaps); row 0 is v = 0.
    const TX = { on: false, tex: {}, level: false, set: null }; // level: the level on screen has them; set: its pictures and how they are used
    // what a theme's `tex` leaves out: level 1's values. surfA: the surface picture's mean (null: measured);
    // hum, sub, bed: tints of the top soil, the subsoil and the bedrock; top: how dark the soil is right under the edge
    const TX_D = { surface: 'forest-floor', rock: 'granite', earth: 'dark-earth', grass: 'wild-grass', bark: 'pine-bark', surfA: 0.18, surfB: [1.25, 1.25, 1.25], rockMono: 0, earthAmt: 1, stoneB: 1, sky: 'clouds', craters: 0,
                   hum: [0.62, 0.54, 0.48], sub: [1.75, 1.32, 0.92], bed: [0.86, 0.8, 0.74], root: 1, top: 0.3, lipF: [0.2475, 0.18, 0.135], lipB: 1.3, lipS: 0.55,
                   grassR: [0.42, 0.62], grassG: [0.5, 0.7], grassB: [0.18, 0.32], edgeUp: 1, edgeDown: 1, clods: 1, tufts: 1, patch: 0.4 };
    // pictures only one theme uses, toned down where they load: [saturation, tint]
    const TX_GRADE = { 'candy-surface': [0.3, [1.0, 0.93, 0.98]], 'candy-rock': [0.25, [0.95, 0.84, 0.94]], 'candy-earth': [0, [2.4, 1.8, 2.35]] };
    const txOn = (th) => TX.on && th.tex;
    function txNew(srgb, aniso) {
        const t = new T.DataTexture(null, 1, 1, T.RGBAFormat);
        t.wrapS = t.wrapT = T.RepeatWrapping; t.magFilter = T.LinearFilter; t.minFilter = T.LinearMipmapLinearFilter;
        t.generateMipmaps = true; t.anisotropy = aniso; if (srgb) t.colorSpace = T.SRGBColorSpace;
        return t;
    }
    function txSeam(d, w, h, B) { // meets itself at the edges: the outer B pixels fade into the opposite side
        const mix = (i, j, t) => { for (let c = 0; c < 3; c++) { const a = d[i + c], b = d[j + c]; d[i + c] = a + (b - a) * t; d[j + c] = b + (a - b) * t; } };
        for (let k = 0; k < B; k++) {
            const t = 0.5 * (1 - k / B);
            for (let y = 0; y < h; y++) mix((y * w + k) * 4, (y * w + w - 1 - k) * 4, t);
            for (let x = 0; x < w; x++) mix((k * w + x) * 4, ((h - 1 - k) * w + x) * 4, t);
        }
    }
    function txNormal(d, w, h, k) { // a normal map from the picture's brightness
        const H = new Float32Array(w * h), N = new Uint8Array(w * h * 4);
        for (let i = 0; i < w * h; i++) H[i] = (d[i * 4] * 0.3 + d[i * 4 + 1] * 0.59 + d[i * 4 + 2] * 0.11) / 255;
        for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
            const dx = (H[y * w + (x + 1) % w] - H[y * w + (x + w - 1) % w]) * k, dy = (H[((y + 1) % h) * w + x] - H[((y + h - 1) % h) * w + x]) * k;
            const l = 1 / Math.hypot(dx, dy, 1), i = (y * w + x) * 4;
            N[i] = (-dx * l + 1) * 127.5; N[i + 1] = (-dy * l + 1) * 127.5; N[i + 2] = (l + 1) * 127.5; N[i + 3] = 255;
        }
        return N;
    }
    // clouds come on flat pure blue: how far a pixel is from that blue says how much cloud
    // it holds, so soft edges stay soft. Turned over, as sprites and planes read v upwards
    function txSky(d, w, h) {
        const row = new Uint8Array(w * 4);
        for (let y = 0; y < h >> 1; y++) {
            const a = y * w * 4, b = (h - 1 - y) * w * 4;
            row.set(d.subarray(a, a + w * 4)); d.copyWithin(a, b, b + w * 4); d.set(row, b);
        }
        for (let i = 0; i < d.length; i += 4) {
            const r = d[i], g = d[i + 1], b = d[i + 2];
            let a = 1 - (b - Math.max(r, g)) / 255;
            a = a < 0.05 ? 0 : Math.min(1, (a - 0.05) / 0.95);
            const v = a > 0 ? Math.min(255, Math.max(r, g) / Math.max(a, 0.2) * 1.14 + 22) : 240; // grey: what is left once the blue is gone
            d[i] = d[i + 1] = v; d[i + 2] = Math.min(255, v * 1.02); d[i + 3] = a * 255;
        }
    }
    function txClouds([color, opacity, count], xmin, xmax) {
        const layer = (name, z, tile, y, op, v) => { // a band across the whole level, repeating sideways
            const n = Math.ceil((xmax - xmin + 50000) / tile), g = new T.PlaneGeometry(n * tile, tile * 2 / 3), u = g.attributes.uv;
            for (let i = 0; i < u.count; i++) u.setX(i, u.getX(i) * n);
            const m = new T.Mesh(g, new T.MeshBasicMaterial({ map: TX.tex[name].c, transparent: true, depthWrite: false, fog: false, opacity: op, color: srgb(color) }));
            const x0 = (xmin + xmax) / 2; m.position.set(x0, y, z); world.add(m);
            duskMats.push([m.material, m.material.color.clone(), DUSK_CLOUD_TX]);
            addTimed((t) => { m.position.x = x0 + (t * v) % tile; });
        };
        const smoke = TX.set.sky === 'smoke';
        layer(smoke ? 'smoke-far' : 'clouds-cirrus', -21000, smoke ? 15000 : 21000, smoke ? 6300 : 6900, opacity * 0.5, 6);
        layer(smoke ? 'smoke-far' : 'clouds-far', -15000, 9000, 5500, opacity * 0.9, 14);
        const names = smoke ? ['smoke-big', 'smoke-wide', 'smoke-small', 'smoke-wide'] : ['cloud-big', 'cloud-wide', 'cloud-small', 'cloud-torn'], n = Math.round(count * (xmax - xmin) / 24000), cl = [];
        for (let i = 0; i < n; i++) {
            const sp = new T.Sprite(new T.SpriteMaterial({ map: TX.tex[names[i % 4]].c, color: srgb(color), transparent: true, opacity: opacity * rr(0.82, 1), depthWrite: false, fog: false }));
            const z = rr(-13000, -8000), F = (D - z) / (D + 6000), s = rr(1000, 1900) * F;
            sp.scale.set(s, s, 1);
            sp.position.set(rr(xmin - 3000, xmax + 3000), rr(1400, 2200) * F, z);
            world.add(sp); duskMats.push([sp.material, sp.material.color.clone(), DUSK_CLOUD_TX]);
            cl.push({ sp, v: rr(18, 45) });
        }
        addTimed((t, dt) => { for (const c of cl) { c.sp.position.x += c.v * dt; if (c.sp.position.x > xmax + 4000) c.sp.position.x = xmin - 4000; } });
    }
    function txLoad() {
        const src = window.RR_TEX;
        if (!src) return;
        TX.have = true; TX.on = !/[?&]tex=0/.test(location.search);
        const aniso = renderer.capabilities.getMaxAnisotropy();
        for (const name of Object.keys(src)) {
            const e = src[name], t = TX.tex[name] = { c: txNew(true, aniso), n: e.leaf || e.sky ? null : txNew(false, aniso), avg: { value: 0.3 } };
            const img = new Image();
            img.onload = () => {
                const w = img.width, h = img.height, cv = document.createElement('canvas');
                cv.width = w; cv.height = h;
                const g = cv.getContext('2d', { willReadFrequently: true });
                g.drawImage(img, 0, 0);
                const d = new Uint8Array(g.getImageData(0, 0, w, h).data.buffer);
                const gr = TX_GRADE[name];
                if (gr) for (let i = 0; i < d.length; i += 4) {
                    const l = d[i] * 0.3 + d[i + 1] * 0.59 + d[i + 2] * 0.11;
                    for (let c = 0; c < 3; c++) d[i + c] = Math.min(255, (l + (d[i + c] - l) * gr[0]) * gr[1][c]);
                }
                if (e.sky) txSky(d, w, h);
                else if (e.leaf) {
                    let r = 0, gr = 0, b = 0, m = 0;
                    for (let i = 0; i < d.length; i += 4) if (d[i + 3] > 128) { r += d[i]; gr += d[i + 1]; b += d[i + 2]; m++; }
                    for (let i = 0; i < d.length && m; i += 4) if (d[i + 3] < 8) { d[i] = r / m; d[i + 1] = gr / m; d[i + 2] = b / m; }
                } else {
                    txSeam(d, w, h, Math.round(w * 0.05));
                    let sum = 0, cnt = 0; // the picture's mean brightness (linear): rock keeps the colour it is multiplied onto
                    for (let i = 0; i < d.length; i += 64) { sum += Math.pow((d[i] * 0.3 + d[i + 1] * 0.59 + d[i + 2] * 0.11) / 255, 2.2); cnt++; }
                    t.avg.value = sum / cnt;
                    t.n.image = { data: txNormal(d, w, h, 5), width: w, height: h }; t.n.needsUpdate = true;
                }
                t.c.image = { data: d, width: w, height: h }; t.c.needsUpdate = true;
            };
            img.src = e.url;
        }
    }
    // the ground: grass and earth from above, rock on the steep parts. Two scales of the
    // grass so it does not show as a repeat; the relief comes from the normal maps
    function txGround(mat) {
        const t = TX.tex, s = TX.set, F = t[s.surface], E = t[s.earth], G = t[s.rock], uTA = s.surfA === null ? F.avg : { value: s.surfA };
        mat.onBeforeCompile = (sh) => {
            Object.assign(sh.uniforms, { uF: { value: F.c }, uFN: { value: F.n }, uE: { value: E.c }, uEN: { value: E.n }, uG: { value: G.c }, uGN: { value: G.n }, uTA, uFB: { value: new T.Vector3(...s.surfB) }, uEA: { value: s.earthAmt }, uCr: { value: s.craters } });
            sh.vertexShader = 'attribute float aE; varying vec3 vTP,vTN; varying float vE;\n' + sh.vertexShader.replace('#include <begin_vertex>', '#include <begin_vertex>\n vTP=position; vTN=normal; vE=aE;');
            sh.fragmentShader = `uniform sampler2D uF,uFN,uE,uEN,uG,uGN; uniform float uTA,uEA,uCr; uniform vec3 uFB; varying vec3 vTP,vTN; varying float vE;
vec2 crH(vec2 p){return fract(sin(vec2(dot(p,vec2(127.1,311.7)),dot(p,vec2(269.5,183.3))))*43758.5453);}
vec3 crat(vec2 p,float S,float sd){ vec2 q=p/S,i=floor(q),f=fract(q); vec3 R=vec3(0.,0.,1.); // xy: the slope, z: how dark
 for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++){ vec2 g=vec2(float(x),float(y)),o=crH(i+g+sd); if(o.x>.45) continue;
  float r=.12+.3*o.y; vec2 v=(f-g-o)/r; float d=length(v); if(d>1.7) continue;
  float dh=(d<1.?1.4*d:0.)-15.*(d-1.)*exp(-pow((d-1.)*5.,2.));
  R.xy+=v/max(d,1e-3)*dh*.22; R.z*=d<1.?.82+.18*d*d:1.+.08*exp(-pow((d-1.)*4.,2.)); }
 return R; }
` + sh.fragmentShader
                .replace('#include <color_fragment>', `#include <color_fragment>
 vec2 tu=vTP.xz/520., tu2=mat2(.8,.6,-.6,.8)*vTP.xz/1370., tuG=vec2(vTP.x+vTP.z,vTP.y)/640.;
 vec3 tF=mix(texture2D(uF,tu).rgb,texture2D(uF,tu2).rgb,.42), tE=texture2D(uE,tu*1.3).rgb, tG=texture2D(uG,tuG).rgb;
 float tL=dot(tF,vec3(.33)), tEm=smoothstep(.42,.62,vE+(tL-uTA)*.6)*uEA, tRk=1.-smoothstep(.55,.85,normalize(vTN).y+(tL-uTA)*.25);
 diffuseColor.rgb=mix(mix(tF*uFB,tE,tEm),tG,tRk);
 vec3 cA=uCr>0.?crat(vTP.xz,760.,0.):vec3(0.,0.,1.), cB=uCr>0.?crat(vTP.xz,210.,7.):vec3(0.,0.,1.); vec2 cg=(cA.xy+cB.xy*.8)*uCr;
 diffuseColor.rgb*=mix(1.,cA.z*cB.z,uCr);`)
                .replace('#include <normal_fragment_maps>', `#include <normal_fragment_maps>
 { vec3 tn=mix(mix(texture2D(uFN,tu).xyz,texture2D(uEN,tu*1.3).xyz,tEm),texture2D(uGN,tuG).xyz,tRk)*2.-1.;
   normal=normalize((viewMatrix*vec4(normalize(normalize(vTN)+vec3(tn.x,0.,tn.y)*1.1-vec3(cg.x,0.,cg.y)),0.)).xyz); }`);
        };
        mat.customProgramCacheKey = () => 'tx-ground';
        return mat;
    }
    // rock on what keeps its own colour (mountains, cliffs): the colour times the granite's
    // light and dark, the relief from its normal map. S: world units per picture
    function txRock(mat, S, k) {
        const g = TX.tex[TX.set.rock];
        mat.flatShading = false;
        mat.onBeforeCompile = (sh) => {
            Object.assign(sh.uniforms, { uG: { value: g.c }, uGN: { value: g.n }, uA: g.avg, uS: { value: S }, uK: { value: k }, uMono: { value: TX.set.rockMono } });
            sh.vertexShader = 'varying vec3 vTP,vTN;\n' + sh.vertexShader.replace('#include <begin_vertex>', '#include <begin_vertex>\n vTP=(modelMatrix*vec4(position,1.)).xyz; vTN=mat3(modelMatrix)*normal;');
            sh.fragmentShader = 'uniform sampler2D uG,uGN; uniform float uA,uS,uK,uMono; varying vec3 vTP,vTN;\n' + sh.fragmentShader
                .replace('#include <color_fragment>', `#include <color_fragment>
 vec2 tr=vec2(vTP.x+vTP.z*.7,vTP.y)/uS;
 vec3 tgc=(texture2D(uG,tr).rgb+texture2D(uG,tr*.29+.37).rgb)*.5;
 diffuseColor.rgb*=clamp(pow(mix(tgc,vec3(dot(tgc,vec3(.3,.59,.11))),uMono)/uA,vec3(1.25)),.2,2.2);`)
                .replace('#include <normal_fragment_maps>', `#include <normal_fragment_maps>
 { vec3 tn=texture2D(uGN,tr).xyz*2.-1.; normal=normalize((viewMatrix*vec4(normalize(normalize(vTN)+vec3(tn.xy,0.)*uK),0.)).xyz); }`);
        };
        mat.customProgramCacheKey = () => 'tx-rock';
        return mat;
    }
    // the soil seen from the side. vUv.y is the depth below the surface at that x: dark
    // humus with roots, lighter subsoil in bands, a layer of gravel, bedrock further down
    const TX_SOIL_DECL = `uniform sampler2D uE,uG,uEN,uGN; uniform vec3 uHum,uSub,uBed; uniform float uRoot,uTop,uStB;
vec2 h2(vec2 p){return fract(sin(vec2(dot(p,vec2(127.1,311.7)),dot(p,vec2(269.5,183.3))))*43758.5453);}
float stoneDens(float d){return smoothstep(45.,100.,d)*(.07+.55*smoothstep(150.,230.,d)*(1.-smoothstep(320.,430.,d)));}
vec4 stones(vec2 p,float d,float S){ vec2 q=p/S,i=floor(q),f=fract(q); vec4 b=vec4(9.,0.,0.,0.);
 for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++){ vec2 g=vec2(float(x),float(y)),o=h2(i+g);
  if(fract(o.x*7.31)>stoneDens(d+p.y-(i.y+g.y+o.y)*S)) continue;
  float r=.2+.3*fract(o.y*5.13); vec2 v=g+o-f; float a=fract(o.x*3.7)*3.14,ca=cos(a),sa=sin(a); vec2 u=vec2(ca*v.x+sa*v.y,(-sa*v.x+ca*v.y)*1.5);
  float dd=length(u)/r+.14*sin(atan(u.y,u.x)*3.+o.x*40.)+.06*sin(atan(u.y,u.x)*7.+o.y*40.);
  if(dd<b.x) b=vec4(dd,o.x,v/max(length(v),1e-3)); }
 return b; }
void stoneOn(inout vec3 c,vec4 s,vec3 tg){ c*=mix(.5,1.,smoothstep(.98,1.32,s.x)); if(s.x>=1.) return;
 float h=sqrt(max(1.-s.x*s.x,0.)); vec3 n=normalize(vec3(s.zw*s.x,h)); float l=.48+.72*max(dot(n,normalize(vec3(.25,.8,.55))),0.);
 c=mix(c,tg*mix(vec3(.95,.9,.82),vec3(.72,.62,.5),s.y)*l*1.25,smoothstep(1.,.93,s.x)); }
`;
    const TX_SOIL = ` float d=vUv.y, dw=d+(fbm(p*vec2(.005,.011))-.5)*95.;
 vec3 te=mix(texture2D(uE,p/430.).rgb,texture2D(uE,p/1130.).rgb,.4), tg=texture2D(uG,p/760.).rgb;
 vec3 hum=te*uHum, sub=te*uSub*(.74+.5*noise(vec2(p.x*.0024,dw*.05)));
 c=mix(hum,sub,smoothstep(42.,110.,dw));
 float rk=smoothstep(480.,640.,dw+(st-.5)*150.);
 c=mix(c,tg*uBed*(.72+.5*noise(vec2(p.x*.0016,dw*.022))),rk);
 c*=1.-.6*rk*smoothstep(.022,.0,abs(noise(vec2(p.x*.0045,dw*.016))-.5));
 float rt=smoothstep(.016,.003,abs(fbm(vec2(p.x*.026+d*.008,d*.011))-.5))*(1.-smoothstep(55.,185.,d));
 c=mix(c,te*vec3(1.6,1.15,.75)*.85,rt*.85*uRoot);
 vec3 tn=mix(texture2D(uEN,p/430.).xyz,texture2D(uGN,p/760.).xyz,rk)*2.-1.;
 c*=.86+.6*(tn.y*.8+tn.x*.25);
 stoneOn(c,stones(p,d,50.),tg*uStB); stoneOn(c,stones(p+31.,d,19.),tg*uStB);
 c*=mix(uTop,1.,smoothstep(0.,64.,d+(noise(vec2(p.x*.045,2.))-.5)*30.));
`;
    function txFaceGeo(xs) { // one column per x, from the surface down: uv.y holds the depth
        const P = [], U = [], I = [];
        xs.forEach((x, i) => {
            const h = heightAt(x);
            let hs = 0; for (let k = -6; k <= 6; k++) hs += heightAt(x + k * 45) / 13; // smoothed, or the layers break off at a steep wall
            P.push(x, -h, 0, x, -1800, 0); U.push(x, 0, x, Math.max(1800 - hs, 300));
            if (i < xs.length - 1) { const a = i * 2; I.push(a, a + 1, a + 2, a + 2, a + 1, a + 3); }
        });
        const g = new T.BufferGeometry();
        g.setAttribute('position', new T.Float32BufferAttribute(P, 3));
        g.setAttribute('uv', new T.Float32BufferAttribute(U, 2));
        g.setIndex(I);
        return g;
    }
    // the edge of the turf: grass standing just behind the line, grass and clods hanging over
    function txEdge(x0, x1) {
        const m = new T.Matrix4(), q = new T.Quaternion(), s = new T.Vector3(), p = new T.Vector3(), c = new T.Color();
        const place = (im, i, x, y, z, rz, w, h, tint) => { p.set(x, y, z); q.setFromEuler(new T.Euler(0, rr(0, 6.28), rz)); s.set(w, h, w); im.setMatrixAt(i, m.compose(p, q, s)); im.setColorAt(i, tint); };
        const grass = (dir, nor, foot) => { const K = txCards(); for (const a of [0.5, 2.1]) K.add(K.v3(0, 0, 0), K.v3(0, dir, 0), K.v3(Math.cos(a), 0, Math.sin(a)), K.v3(...nor).normalize(), 1, foot); return K.done(); };
        const ts = TX.set, tint = (k) => c.setRGB(rr(...ts.grassR) * k, rr(...ts.grassG) * k, rr(...ts.grassB) * k);
        const len = x1 - x0, n1 = Math.floor(len / 2.6 * ts.edgeUp), n2 = Math.floor(len / 4.2 * ts.edgeDown), n3 = Math.floor(len / 24 * ts.clods);
        const up = new T.InstancedMesh(grass(1, [0, 1, 0.25], 0.42), txLeafMat(ts.grass || 'wild-grass', 0.7, 1)[0], ts.grass ? n1 : 0);
        for (let i = 0; i < n1; i++) { const x = rr(x0, x1), h = rr(18, 44); place(up, i, x, -heightAt(x) - 2, rr(-30, -3), rr(-0.1, 0.1), h * 1.05, h, tint(1)); }
        const mat = txLeafMat(ts.grass || 'wild-grass', 0.7, 1)[0];
        const down = new T.InstancedMesh(grass(-1, [0, 0.55, 0.85], 0.75), mat, ts.grass ? n2 : 0);
        for (let i = 0; i < n2; i++) { const x = rr(x0, x1), h = rr(16, 42) * (fbm(x * 0.006, 4.2) > 0.42 ? 1 : 0.4); place(down, i, x, -heightAt(x) + 3, rr(2.5, 7), rr(-0.3, 0.3), h * 0.9, h, tint(0.8)); }
        const e = TX.tex[ts.earth];
        const clods = new T.InstancedMesh(displace(new T.IcosahedronGeometry(1, 1), 0.45, 1.7), new T.MeshStandardMaterial({ map: e.c, normalMap: e.n, roughness: 1, color: 0xb09880 }), n3);
        for (let i = 0; i < n3; i++) { const x = rr(x0, x1), r = rr(3.5, 11); place(clods, i, x, -heightAt(x) - rr(10, 36), rr(2.5, 8), rr(-1, 1), r, r * 0.8, c.setRGB(1, 1, 1)); }
        up.userData.noCast = down.userData.noCast = true;
        for (const o of [up, down, clods]) { o.frustumCulled = false; world.add(o); }
    }
    // foliage: flat cards cut out by their alpha, lit by the round normals they are given
    // (both sides alike), bending in the wind like the trees they replace
    function txLeafMat(name, k, h) {
        const map = TX.tex[name].c;
        const mat = new T.MeshStandardMaterial({ map, alphaTest: 0.42, side: T.DoubleSide, roughness: 0.8, vertexColors: true });
        mat.onBeforeCompile = (sh) => { sh.fragmentShader = sh.fragmentShader.replace('#include <normal_fragment_begin>', T.ShaderChunk.normal_fragment_begin.replace('gl_FrontFacing ? 1.0 : - 1.0', '1.0')); };
        windify(mat, k, h);
        mat.customProgramCacheKey = () => 'tx-leaf';
        const dm = new T.MeshDepthMaterial({ depthPacking: T.RGBADepthPacking, map, alphaTest: 0.42, side: T.DoubleSide });
        windify(dm, k, h); dm.customProgramCacheKey = () => 'tx-leaf-depth';
        return [mat, dm];
    }
    function txCards() {
        const P = [], N = [], U = [], C = [], I = [];
        const v3 = (x, y, z) => new T.Vector3(x, y, z);
        return {
            // o: the foot of the card; a: its length (foot to tip); b: its width; n: its normal; c: its shade
            add(o, a, b, n, c, foot = 1) {
                const i = P.length / 3;
                for (const [sx, sy] of [[-0.5, 0], [0.5, 0], [0.5, 1], [-0.5, 1]]) {
                    P.push(o.x + b.x * sx + a.x * sy, o.y + b.y * sx + a.y * sy, o.z + b.z * sx + a.z * sy);
                    N.push(n.x, n.y, n.z); U.push(sx + 0.5, 1 - sy); const v = sy ? c : c * foot; C.push(v, v, v);
                }
                I.push(i, i + 1, i + 2, i, i + 2, i + 3);
            },
            done() {
                const g = new T.BufferGeometry();
                g.setAttribute('position', new T.Float32BufferAttribute(P, 3));
                g.setAttribute('normal', new T.Float32BufferAttribute(N, 3));
                g.setAttribute('uv', new T.Float32BufferAttribute(U, 2));
                g.setAttribute('color', new T.Float32BufferAttribute(C, 3));
                g.setIndex(I);
                return g;
            }, v3,
        };
    }
    function txPine() { // tiers of drooping branches round the trunk, smaller towards the top
        const K = txCards(), up = K.v3(0, 1, 0);
        for (let y = 48, f = 0; y < 215; y += 13.5, f = (y - 48) / 167) {
            const L = 112 - 88 * f, n = Math.round(8 - 4 * f), a0 = rnd() * 6.28;
            for (let i = 0; i < n; i++) {
                const a = a0 + i / n * 6.283 + rr(-0.25, 0.25), dr = rr(0.18, 0.5) - f * 0.5;
                const out = K.v3(Math.cos(a), 0, Math.sin(a)), V = out.clone().multiplyScalar(Math.cos(dr)).addScaledVector(up, -Math.sin(dr)).normalize();
                const S = K.v3(-Math.sin(a), 0, Math.cos(a)), roll = rr(-0.95, 0.95);
                const W = S.clone().multiplyScalar(Math.cos(roll)).add(V.clone().cross(S).multiplyScalar(Math.sin(roll)));
                K.add(K.v3(0, y, 0), V.multiplyScalar(L * rr(0.9, 1.15)), W.multiplyScalar(L * 0.9), out.clone().multiplyScalar(0.65).addScaledVector(up, 0.75).normalize(), 0.72 + 0.34 * f + rr(-0.06, 0.06));
            }
        }
        for (const a of [0, 1.57]) K.add(K.v3(0, 196, 0), K.v3(0, 48, 0), K.v3(Math.cos(a) * 36, 0, Math.sin(a) * 36), up.clone(), 1.05);
        return K.done();
    }
    function txOak() { return txCrown([[0, 178, 96, 76, 78, 1]]); }
    // crowns of leaf sprays: dark inside, light on the outside and on top. Each: x, y, radius, height, sprays, spray size
    function txCrown(crowns) {
        const K = txCards();
        for (const [cx, cy, RX, RY, n, ls] of crowns) for (let i = 0; i < n; i++) {
            const C = K.v3(cx, cy, 0), d = K.v3(rr(-1, 1), rr(-0.75, 1), rr(-1, 1));
            if (d.length() > 1) { i--; continue; }
            const r = d.length(), p = K.v3(C.x + d.x * RX, C.y + d.y * RY, C.z + d.z * RX);
            const m = d.clone().normalize().add(K.v3(rr(-0.5, 0.5), rr(-0.5, 0.5), 0.8)).normalize();
            const s = m.clone().cross(K.v3(rr(-1, 1), 1, rr(-1, 1))).normalize(), V = s.clone().cross(m).normalize();
            const L = rr(66, 92) * ls;
            K.add(p.addScaledVector(V, -L / 2), V.multiplyScalar(L), s.multiplyScalar(L), d.clone().normalize().add(K.v3(0, 0.5, 0)).normalize(), 0.52 + 0.42 * r + 0.12 * (d.y + 0.75));
        }
        return K.done();
    }
    function txFronds(y, n) { // palm fronds from the top, arching out and down
        const K = txCards(), up = K.v3(0, 1, 0), a0 = rnd() * 6.28;
        for (let i = 0; i < n; i++) {
            const a = a0 + i / n * 6.283 + rr(-0.2, 0.2), out = K.v3(Math.cos(a), 0, Math.sin(a)), dr = rr(-0.5, 0.15);
            const V = out.clone().multiplyScalar(Math.cos(dr)).addScaledVector(up, -Math.sin(dr)).normalize(), W = K.v3(-Math.sin(a), 0, Math.cos(a)), L = rr(170, 215);
            K.add(K.v3(0, y, 0), V.multiplyScalar(L), W.multiplyScalar(L * 0.75), out.clone().multiplyScalar(0.4).addScaledVector(up, 0.9).normalize(), rr(0.85, 1.05));
        }
        return K.done();
    }
    function txTrunk(rt, rb, h) {
        const g = new T.CylinderGeometry(rt, rb, h, 8, 3, true); g.translate(0, h / 2 - 4, 0);
        const u = g.attributes.uv; for (let i = 0; i < u.count; i++) u.setXY(i, u.getX(i) * 1.5, u.getY(i) * h / 170);
        return g;
    }
    // a tree of the level from the placements in `im`: bark trunk plus foliage cards
    function txCactus() { // the original's shape, round and with arms that bend instead of meet at a corner
        const P = [], N = [], U = [];
        const add = (g, m, vk) => {
            g = g.index ? g.toNonIndexed() : g; g.applyMatrix4(m);
            const p = g.attributes.position, n = g.attributes.normal, u = g.attributes.uv;
            for (let i = 0; i < p.count; i++) { P.push(p.getX(i), p.getY(i), p.getZ(i)); N.push(n.getX(i), n.getY(i), n.getZ(i)); U.push(u.getX(i), u.getY(i) * vk); }
        };
        const cyl = (rt, rb, h, x, y, rz) => add(new T.CylinderGeometry(rt, rb, h, 18, 1, true), M4(x, y, 0, 0, 0, rz), h / 94);
        const ball = (r, x, y) => add(new T.SphereGeometry(r, 18, 10), M4(x, y, 0), 1);
        cyl(15, 17, 170, 0, 85, 0); ball(15, 0, 170);
        cyl(10, 11, 46, -30, 82, Math.PI / 2); ball(10.5, -52, 82); cyl(10, 10.5, 60, -52, 112, 0); ball(10, -52, 142);
        cyl(10, 11, 40, 26, 108, Math.PI / 2); ball(10.5, 44, 108); cyl(10, 10.5, 46, 44, 130, 0); ball(10, 44, 153);
        const g = new T.BufferGeometry();
        g.setAttribute('position', new T.Float32BufferAttribute(P, 3)); g.setAttribute('normal', new T.Float32BufferAttribute(N, 3)); g.setAttribute('uv', new T.Float32BufferAttribute(U, 2));
        return g;
    }
    function txDead() { // the original's three limbs, plus the stubs of broken twigs
        const P = [], N = [], U = [];
        const limb = (rt, rb, h, x, y, rz, ry = 0) => {
            const g = new T.CylinderGeometry(rt, rb, h, 9, 4, false).toNonIndexed(); g.applyMatrix4(M4(x, y, 0, 0, ry, rz));
            const p = g.attributes.position, n = g.attributes.normal, u = g.attributes.uv;
            for (let i = 0; i < p.count; i++) { P.push(p.getX(i), p.getY(i), p.getZ(i)); N.push(n.getX(i), n.getY(i), n.getZ(i)); U.push(u.getX(i) * Math.max(1, rb / 6), u.getY(i) * h / 60); }
        };
        limb(5, 12, 140, 0, 70, 0); limb(3, 6, 70, -24, 120, 0.7); limb(3, 5, 60, 22, 135, -0.8);
        limb(1.2, 2.6, 26, -6, 96, 1.1, 0.8); limb(1, 2.2, 22, 6, 112, -1.2, -0.5); limb(0.8, 1.8, 18, -40, 140, 0.25); limb(0.8, 1.6, 16, 38, 152, -0.2);
        const g = new T.BufferGeometry();
        g.setAttribute('position', new T.Float32BufferAttribute(P, 3)); g.setAttribute('normal', new T.Float32BufferAttribute(N, 3)); g.setAttribute('uv', new T.Float32BufferAttribute(U, 2));
        return g;
    }
    function txTree(kind, im) {
        if (kind === 'cactus' || kind === 'deadtree') {
            const cac = kind === 'cactus', sw = SWAY[kind], t = TX.tex[cac ? TX.set.cactus : TX.set.bark];
            const mat = new T.MeshStandardMaterial({ map: t.c, normalMap: t.n, normalScale: new T.Vector2(cac ? 0.8 : 1.3, cac ? 0.8 : 1.3), roughness: cac ? 0.72 : 0.95, color: cac ? 0xffffff : 0xb0a8a0 });
            windify(mat, sw[0], sw[1]); mat.customProgramCacheKey = () => 'tx-solid';
            const o = new T.InstancedMesh(cac ? txCactus() : txDead(), mat, im.count);
            o.instanceMatrix.copy(im.instanceMatrix); if (im.instanceColor) o.instanceColor = im.instanceColor.clone();
            o.customDepthMaterial = windDepth(sw[0], sw[1]); o.frustumCulled = false;
            world.add(o);
            return;
        }
        const pine = kind === 'pine' || kind === 'snowpine', jun = kind === 'jungle', sw = SWAY[kind], leaf = txLeafMat(kind === 'snowpine' ? 'snow-pine-branch' : pine ? 'pine-branch' : jun ? 'jungle-broad-leaves' : 'oak-leaves', sw[0], sw[1]);
        const bark = TX.tex[TX.set.bark];
        const mat = new T.MeshStandardMaterial({ map: bark.c, normalMap: bark.n, normalScale: new T.Vector2(1.2, 1.2), roughness: 0.95, color: pine ? 0xc8b8a8 : 0x9c8f80 });
        windify(mat, sw[0], sw[1]); mat.customProgramCacheKey = () => 'tx-bark';
        const parts = [[pine ? txTrunk(2.5, 11, 226) : jun ? txTrunk(7, 14, 222) : txTrunk(11, 18, 165), mat, windDepth(sw[0], sw[1])],
                       [pine ? txPine() : jun ? txCrown([[0, 228, 74, 46, 40, 0.85], [-58, 205, 56, 40, 26, 0.8], [60, 210, 54, 40, 26, 0.8]]) : txOak(), leaf[0], leaf[1]]];
        if (jun) { const palm = txLeafMat('jungle-palm-frond', sw[0], sw[1]); parts.push([txFronds(240, 9), palm[0], palm[1]]); }
        for (const [g, m, dm] of parts) {
            const o = new T.InstancedMesh(g, m, im.count);
            o.instanceMatrix.copy(im.instanceMatrix); if (im.instanceColor) o.instanceColor = im.instanceColor.clone();
            o.customDepthMaterial = dm; o.frustumCulled = false;
            world.add(o);
        }
    }
    function txGrass(xmin, xmax) { // tufts behind the playing line, thick near it and thinning out
        if (!TX.set.grass) return;
        const K = txCards(), up = K.v3(0, 1, 0);
        for (const a of [0.5, 2.1]) K.add(K.v3(0, 0, 0), up.clone(), K.v3(Math.cos(a), 0, Math.sin(a)), K.v3(0, 1, 0.25).normalize(), 1, 0.38);
        const ts = TX.set, n = Math.floor((xmax - xmin) / 1.7 * ts.tufts), mat = txLeafMat(ts.grass, 0.7, 1)[0];
        const im = new T.InstancedMesh(K.done(), mat, n), m = new T.Matrix4(), q = new T.Quaternion(), s = new T.Vector3(), p = new T.Vector3(), c = new T.Color();
        for (let i = 0; i < n; i++) {
            const z = -(12 + Math.pow(rnd(), 2.2) * 1500), x = rr(xmin, xmax), h = rr(20, 52) * (0.7 + 0.7 * fbm(x * 0.003, z * 0.003));
            if (fbm(x * 0.0022 + 3, z * 0.0022) < ts.patch + rnd() * 0.16) { im.setMatrixAt(i, m.makeScale(0, 0, 0)); im.setColorAt(i, c.setRGB(0, 0, 0)); continue; } // in patches: the ground shows between them
            p.set(x, -surfY(x, z) - 3, z); q.setFromEuler(new T.Euler(0, rr(0, 6.28), rr(-0.08, 0.08))); s.set(h * 1.05, h, h * 1.05);
            im.setMatrixAt(i, m.compose(p, q, s)); im.setColorAt(i, c.setRGB(rr(...ts.grassR), rr(...ts.grassG), rr(...ts.grassB)));
        }
        im.userData.noCast = true; im.frustumCulled = false;
        world.add(im);
    }

    // ---------- the level's ground ----------
    let ridge = [], endX = 0;
    function heightAt(x) { // the original's terrain line (2D y), extended both ways
        if (x <= ridge[0].x) return ridge[0].y;
        const n = ridge.length;
        if (x >= ridge[n - 1].x) return ridge[n - 1].y;
        let lo = 0, hi = n - 1;
        while (hi - lo > 1) { const m = (lo + hi) >> 1; if (ridge[m].x <= x) lo = m; else hi = m; }
        const a = ridge[lo], b = ridge[hi], t = (x - a.x) / (b.x - a.x || 1);
        return a.y + (b.y - a.y) * t;
    }
    // The ground behind the playing line: the same line at z = 0, rising into
    // hills further back.
    function lift(x, z) { return (1 - Math.exp(z / 900)) * (40 + 260 * fbm(x * 0.00105 + 3.1, z * 0.0011 + 7.3)); }
    // The line averaged over a window: the further back, the smoother, so the
    // ground behind becomes rolling hills instead of copies of every bump.
    const SM = { x0: 0, step: 20, sum: null };
    function prepSmooth(xmin, xmax) {
        SM.x0 = xmin; const n = Math.ceil((xmax - xmin) / SM.step) + 2;
        SM.sum = new Float64Array(n + 1);
        for (let i = 0; i < n; i++) SM.sum[i + 1] = SM.sum[i] + heightAt(xmin + i * SM.step);
    }
    function hSmooth(x, w) {
        const n = SM.sum.length - 1, k = Math.max(1, Math.round(w / SM.step));
        const c = Math.round((x - SM.x0) / SM.step);
        const a = Math.max(0, Math.min(n, c - k)), b = Math.max(0, Math.min(n, c + k + 1));
        return b > a ? (SM.sum[b] - SM.sum[a]) / (b - a) : heightAt(x);
    }
    function surfY(x, z) { // 2D y (down) of the ground at x, depth z (<= 0)
        const k = 1 - Math.exp(z / 260);
        const w = Math.min(1600, -z * 1.2);
        return hSmooth(x, w) - lift(x, z) + (fbm(x * 0.008, z * 0.008) - 0.5) * 60 * k;
    }

    // ---------- the themes ----------
    // mountains: [z, top (2D y of the peaks), amplitude, colour high, colour low, snow line (2D y, or null), frequency]
    const THEMES = {
        forest: {
            sun: { dir: [0.35, 0.55, -1], color: '#fff2cc', size: 0.004 }, light: '#fff1d8', lightI: 1.25,
            hemi: ['#d6efff', '#3c5c2c', 0.75], fog: ['#b9e2dc', 3000, 16000],
            grass: '#62a83c', grass2: '#3f7a26', rock: '#7c6d58', soil: '#8B7355', soil2: '#5b4632',
            mountains: [[-11000, -2700, 1300, '#8fb3c9', '#6e8fa6', -2900, 0.00035], [-7200, -1400, 1000, '#6f9a77', '#4e7a58', -2200, 0.0005], [-4300, -500, 700, '#4f8a4a', '#36622f', null, 0.0008]],
            tex: {}, trees: [['pine', 0.55], ['oak', 0.45]], treeDensity: 1, falls: 'water', fallCount: 1, clouds: ['#ffffff', 0.85, 12], mist: ['#e9f3ef', 0.14], fireflies: false,
        },
        desert: {
            sun: { dir: [0.2, 0.42, -1], color: '#fff0b0', size: 0.009 }, light: '#ffe6b0', lightI: 1.45,
            hemi: ['#ffe2b5', '#a0602a', 0.7], fog: ['#ffc98a', 3500, 17000],
            grass: '#e2b06a', grass2: '#c98a45', rock: '#b8693a', soil: '#D2691E', soil2: '#8e4415',
            mountains: [[-10500, -1600, 900, '#e0a36a', '#c47e47', null, 0.0004], [-6800, -700, 500, '#d98e4f', '#b86a32', null, 0.0006]],
            tex: { surface: 'desert-surface', rock: 'desert-rock', earth: 'desert-earth', grass: 'desert-dry-grass', cactus: 'desert-cactus-skin', surfA: null, surfB: [0.92, 1.02, 1.3], rockMono: 1, hum: [0.58, 0.66, 0.85], sub: [0.74, 0.86, 1.15], bed: [0.72, 0.76, 0.86], root: 0, top: 0.72, lipF: [0.62, 0.55, 0.5], lipB: 1, lipS: 0.85, grassR: [0.85, 1.1], grassG: [0.85, 1.1], grassB: [0.8, 1], edgeUp: 0.1, edgeDown: 0, clods: 0.2, tufts: 0.05, patch: 0.5 },
            trees: [['cactus', 1]], treeDensity: 0.35, mesas: true, clouds: ['#fff6e8', 0.35, 4], heat: true,
        },
        snow: {
            sun: { dir: [-0.3, 0.35, -1], color: '#ffffff', size: 0.004 }, light: '#eef6ff', lightI: 0.85,
            hemi: ['#dfeefc', '#7f95ad', 0.55], fog: ['#dceefa', 2500, 15000],
            grass: '#dfe8f2', grass2: '#b4c6d8', rock: '#7d8a9a', soil: '#E0E0E0', soil2: '#9aa7b4',
            mountains: [[-11000, -3000, 1400, '#f2f7fc', '#b7c7d8', -1200, 0.0004], [-7000, -1600, 1000, '#eaf2fa', '#9fb3c8', -400, 0.0006], [-4200, -500, 700, '#e6eef6', '#93a6ba', 300, 0.0009]],
            tex: { surface: 'snow-surface', rock: 'snow-rock', earth: 'snow-earth', grass: 'snow-frosted-grass', surfA: null, surfB: [0.74, 0.78, 0.86], earthAmt: 0.08, stoneB: 2.6, rockMono: 1, hum: [1.1, 1.12, 1.2], sub: [2, 2.05, 2.25], bed: [1.3, 1.35, 1.5], root: 0, top: 0.6, lipF: [0.72, 0.76, 0.84], lipB: 1.02, lipS: 0.9, grassR: [0.85, 1], grassG: [0.9, 1.05], grassB: [0.95, 1.1], edgeUp: 0.1, edgeDown: 0, clods: 0.1, tufts: 0.05, patch: 0.5 },
            trees: [['snowpine', 1]], treeDensity: 0.9, falls: 'ice', fallCount: 1, snow: true, clouds: ['#ffffff', 0.5, 6], mist: ['#ffffff', 0.16],
        },
        volcano: {
            sun: { dir: [0.5, 0.25, -1], color: '#ffb070', size: 0.006 }, light: '#ff9c5a', lightI: 1.0,
            hemi: ['#ff7a4a', '#2a0d05', 0.6], fog: ['#a5341a', 2500, 14000],
            grass: '#3a2a22', grass2: '#22160f', rock: '#3d2d25', soil: '#654321', soil2: '#2b1a0e', cracks: true,
            mountains: [[-11000, -1800, 900, '#4a2418', '#2a120a', null, 0.0004]],
            tex: { surface: 'volcano-surface', rock: 'volcano-rock', earth: 'volcano-earth', grass: null, bark: 'volcano-charred-bark', sky: 'smoke', surfA: null, surfB: [0.72, 0.72, 0.74], earthAmt: 0.35, stoneB: 1.6, rockMono: 1, hum: [0.85, 0.8, 0.75], sub: [1.35, 1.22, 1.1], bed: [0.95, 0.9, 0.9], root: 0, top: 0.55, lipF: [0.5, 0.48, 0.46], lipB: 1.1, lipS: 0.8, edgeUp: 0, edgeDown: 0, clods: 0.3, tufts: 0 },
            trees: [['deadtree', 1]], treeDensity: 0.25, volcanoes: 3, falls: 'lava', fallCount: 0.6, embers: true, ash: true, clouds: ['#5a3a30', 0.6, 8],
        },
        jungle: {
            sun: { dir: [0.2, 0.6, -1], color: '#fff6c0', size: 0.004 }, light: '#fff3c8', lightI: 1.15,
            hemi: ['#c8f0b0', '#1f3a12', 0.8], fog: ['#7fb88a', 2200, 13000],
            grass: '#3f8f2a', grass2: '#245c18', rock: '#5a5a3a', soil: '#556B2F', soil2: '#2e3c17',
            mountains: [[-10000, -2200, 1200, '#4f8f5a', '#2f6a3a', null, 0.0005], [-6500, -1200, 900, '#2f7a3a', '#1f5a28', null, 0.0007], [-4000, -400, 600, '#2a6a2a', '#1a4a1a', null, 0.001]],
            tex: { surface: 'jungle-surface', rock: 'jungle-rock', earth: 'jungle-earth', grass: 'jungle-fern', bark: 'jungle-bark', surfA: null, surfB: [1.1, 1.1, 1.1], earthAmt: 0.6, stoneB: 1.2, rockMono: 1, hum: [0.6, 0.55, 0.5], sub: [1.3, 1.15, 1], bed: [0.9, 0.9, 0.85], root: 1, top: 0.35, lipF: [0.25, 0.22, 0.18], lipB: 1.25, lipS: 0.6, grassR: [0.42, 0.6], grassG: [0.55, 0.75], grassB: [0.32, 0.5], edgeUp: 0.45, edgeDown: 0.5, clods: 0.6, tufts: 0.75, patch: 0.36 },
            trees: [['jungle', 0.7], ['oak', 0.3]], treeDensity: 1.6, falls: 'water', fallCount: 1.8, mist: ['#e6f5e6', 0.2], fireflies: true, clouds: ['#ffffff', 0.5, 6],
        },
        space: {
            sun: { dir: [0.6, 0.3, -1], color: '#ffe680', size: 0.006 }, light: '#ffffff', lightI: 1.3, stars: 1.0, nebula: true,
            hemi: ['#6a6ab0', '#101018', 0.45], fog: ['#08081e', 6000, 40000],
            grass: '#9a9a9a', grass2: '#6a6a6a', rock: '#5a5a5a', soil: '#4a4a4a', soil2: '#2a2a2a',
            mountains: [[-9000, -900, 700, '#5a5a66', '#3a3a44', null, 0.0007]],
            tex: { surface: 'space-surface', rock: 'space-rock', earth: 'space-earth', grass: null, craters: 1, surfA: null, surfB: [0.78, 0.8, 0.88], earthAmt: 0.4, stoneB: 1.5, rockMono: 1, hum: [0.85, 0.85, 0.87], sub: [1.25, 1.24, 1.22], bed: [0.95, 0.95, 1], root: 0, top: 0.6, lipF: [0.45, 0.45, 0.47], lipB: 1.05, lipS: 0.8, edgeUp: 0, edgeDown: 0, clods: 0.2, tufts: 0 },
            trees: [], planets: true, asteroids: true,
        },
        candy: { // the original's Mega Mountain: pink sky, one plum mountain, drifting sweets; no trees
            sun: { dir: [0.3, 0.45, -1], color: '#fff0f4', size: 0.005 }, light: '#f6e6ee', lightI: 0.85,
            hemi: ['#f0d4e2', '#7a4a6c', 0.55], fog: ['#f2c4d4', 3000, 16000],
            grass: '#e8bcd8', grass2: '#c890b8', rock: '#a87098', soil: '#DDA0DD', soil2: '#6e4466',
            mountains: [[-10000, -1800, 900, '#d8aac4', '#b88aa8', -2200, 0.0004], [-6500, -900, 700, '#b894b8', '#94709a', null, 0.0006], [-4000, -300, 500, '#c08aae', '#9a6a8c', null, 0.0009]],
            tex: { surface: 'candy-surface', rock: 'candy-rock', earth: 'candy-earth', grass: null, surfA: null, surfB: [0.96, 0.86, 0.95], earthAmt: 0.25, stoneB: 1.3, rockMono: 1, hum: [0.62, 0.6, 0.62], sub: [1.0, 1.0, 1.0], bed: [1.2, 1.1, 1.2], root: 0, top: 0.45, lipF: [0.62, 0.5, 0.6], lipB: 1.05, lipS: 0.8, edgeUp: 0, edgeDown: 0, clods: 0.2, tufts: 0 },
            trees: [], candy: true, clouds: ['#f4e4ec', 0.45, 6],
        },
        cyber: { // the original's Insane Incline: a dark blue night, a slate mountain, robots
            sun: { dir: [0.2, 0.3, -1], color: '#b8c8ff', size: 0.005 }, light: '#a8b8e8', lightI: 0.75, stars: 0.35,
            hemi: ['#3a3a80', '#0a0a1a', 0.5], fog: ['#181848', 3000, 18000],
            grass: '#2a2a48', grass2: '#1c1c34', rock: '#2a2a44', soil: '#2F2F4F', soil2: '#141428',
            mountains: [[-11000, -1200, 700, '#1c1c44', '#101030', null, 0.0005]],
            tex: { surface: 'cyber-surface', rock: 'cyber-rock', earth: 'cyber-earth', grass: null, surfA: null, surfB: [1.0, 1.0, 1.08], earthAmt: 0.15, stoneB: 1.1, rockMono: 0, hum: [1.1, 1.1, 1.25], sub: [1.7, 1.7, 1.95], bed: [1.1, 1.1, 1.2], root: 0, top: 0.5, lipF: [0.32, 0.33, 0.42], lipB: 1.05, lipS: 0.8, edgeUp: 0, edgeDown: 0, clods: 0.15, tufts: 0 },
            trees: [], skyline: true, data: true, rim: '#7fa8ff', rimI: 1.0,
        },
        hell: { // the original's Death Descent: a dark red sky over a dark red mountain, demons
            sun: { dir: [0.0, 0.2, -1], color: '#901408', size: 0.003 }, light: '#c8503a', lightI: 0.8,
            hemi: ['#a02818', '#140000', 0.55], fog: ['#4a0806', 2000, 12000],
            grass: '#2a0a08', grass2: '#140303', rock: '#3a0e0a', soil: '#4d0000', soil2: '#1e0000',
            mountains: [[-10000, -1500, 900, '#3a0808', '#1e0404', null, 0.0005], [-6000, -600, 600, '#2a0404', '#140202', null, 0.0008]],
            tex: { surface: 'hell-surface', rock: 'hell-rock', earth: 'hell-earth', grass: null, surfA: null, surfB: [1.0, 0.86, 0.84], earthAmt: 0.2, stoneB: 1.2, rockMono: 0, hum: [0.42, 0.24, 0.22], sub: [0.72, 0.42, 0.38], bed: [0.8, 0.6, 0.58], root: 0, top: 0.5, lipF: [0.34, 0.13, 0.11], lipB: 1.05, lipS: 0.8, edgeUp: 0, edgeDown: 0, clods: 0.25, tufts: 0 },
            trees: [], embers: true, ash: true,
        },
        rainbow: { // the original's Ultimate Challenge: violet sky going gold at the horizon, a hot pink mountain, crystals
            sun: { dir: [0.3, 0.15, -1], color: '#ffd060', size: 0.007 }, light: '#ffd8a0', lightI: 0.95,
            hemi: ['#c070e0', '#5a1a4a', 0.55], fog: ['#e09070', 3000, 17000],
            grass: '#e070b0', grass2: '#b04888', rock: '#9a58c0', soil: '#FF69B4', soil2: '#8a2a68',
            mountains: [[-10500, -2200, 1100, '#8a40a8', '#5a2878', -2600, 0.0004], [-6800, -1000, 800, '#b04890', '#7a2a68', null, 0.0006]],
            tex: { surface: 'rainbow-surface', rock: 'rainbow-rock', earth: 'rainbow-earth', grass: null, surfA: null, surfB: [1.2, 0.72, 0.95], earthAmt: 0.2, stoneB: 1.1, rockMono: 0, hum: [0.92, 0.36, 0.6], sub: [1.3, 0.52, 0.84], bed: [0.9, 0.7, 1.0], root: 0, top: 0.55, lipF: [0.6, 0.3, 0.48], lipB: 1.05, lipS: 0.8, edgeUp: 0, edgeDown: 0, clods: 0.2, tufts: 0 },
            trees: [], sparkles: true, clouds: ['#ffd8b8', 0.4, 5],
        },
    };

    // ---------- building blocks ----------
    function addTimed(fn) { updaters.push(fn); }

    // Shadertoy-style effects per theme, all drawn by shaders (own code).
    // clouds: [cover, lit colour, shade colour, underglow or null]
    const FX = {
        forest: { clouds: [0.42, '#ffffff', '#7f8fa0'], rays: 0.9, bloom: 0.35, vol: 1.0 },
        desert: { clouds: [0.22, '#fff4e0', '#c0906a'], rays: 0.6, haze: 1.0, bloom: 0.3, vol: 0.6 },
        snow: { clouds: [0.34, '#f4f8ff', '#8497ad'], rays: 0.35, bloom: 0.1, vol: 0.5 },
        volcano: { clouds: [0.62, '#6a4034', '#24100a', '#ff5a18'], rays: 0.5, haze: 1.3, bloom: 1.0, vol: 0.8 },
        jungle: { clouds: [0.5, '#f4fff0', '#6f8f78'], rays: 1.1, bloom: 0.35, vol: 1.2 },
        space: { nebula: 1, rays: 0.35, bloom: 0.8, vol: 0 },
        candy: { clouds: [0.36, '#fbf2f6', '#b892aa'], rays: 0.5, bloom: 0.3, vol: 0.6 },
        cyber: { bloom: 0.6, rays: 0.2, vol: 0.3 },
        hell: { clouds: [0.5, '#3a100c', '#120202', '#a01808'], haze: 0.8, bloom: 0.6, rays: 0.3, vol: 0.6 },
        rainbow: { clouds: [0.3, '#ffe2b8', '#a05890'], rays: 0.7, bloom: 0.5, vol: 0.7 },
    };
    let fxNow = FX.forest;

    function makeSky(th, c1, c2, fx) {
        const cl = fx.clouds || [0, '#fff', '#fff', null];
        const m = new T.ShaderMaterial({
            side: T.BackSide, depthWrite: false, fog: false,
            uniforms: {
                top: { value: lin(c1) }, bot: { value: lin(c2) }, sunDir: { value: new T.Vector3(...th.sun.dir).normalize() },
                sunCol: { value: lin(th.sun.color) }, sunSize: { value: th.sun.size }, uTime: { value: 0 },
                stars: { value: th.stars || 0 }, uNebula: { value: fx.nebula || 0 }, uCamX: { value: 0 },
                uCover: { value: cl[0] }, uLit: { value: lin(cl[1]) }, uShade: { value: lin(cl[2]) },
                uUnder: { value: lin(cl[3] || '#000000') }, uUnderK: { value: cl[3] ? 1 : 0 },
                uAurora: { value: fx.aurora || 0 }, uSynth: { value: fx.synth || 0 }, uDusk: { value: 0 },
            },
            vertexShader: `varying vec3 vD; void main(){ vD=position; gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.); }`,
            fragmentShader: `uniform vec3 top,bot,sunCol,sunDir,uLit,uShade,uUnder; uniform float sunSize,uTime,stars,uNebula,uCamX,uCover,uUnderK,uAurora,uSynth,uDusk;
varying vec3 vD; ${GLSL_NOISE}
float fbm3(vec2 p){float v=0.,a=.5;for(int i=0;i<3;i++){v+=a*noise(p);p=p*2.07+vec2(3.1,1.7);a*=.5;}return v;}
void main(){ vec3 d=normalize(vD); vec3 sd=normalize(sunDir);
 float t=smoothstep(-0.3,0.4,d.y); vec3 c=mix(bot,top,t);
 float s=max(dot(d,sd),0.);

 // nebula: fbm warped by fbm, twice
 if(uNebula>0.){ vec2 p=d.xy/(abs(d.z)+.35)*1.6;
   vec2 q=vec2(fbm(p+uTime*.01),fbm(p+vec2(5.2,1.3)));
   vec2 r=vec2(fbm(p+3.5*q+vec2(1.7,9.2)+uTime*.015),fbm(p+3.5*q+vec2(8.3,2.8)));
   float f=fbm(p+3.5*r);
   vec3 n=mix(vec3(.10,.02,.25),vec3(.05,.45,.55),clamp(f*f*3.,0.,1.));
   n=mix(n,vec3(.95,.35,.15),clamp(length(q)*.9-.35,0.,1.));
   n=mix(n,vec3(.9,.8,1.),clamp(length(r.x)*.6-.3,0.,1.)*.6);
   c+=n*(f*f*1.6)*uNebula; }

 // aurora: curtains made of stacked noise planes
 if(uAurora>0.&&d.y>0.){ vec3 a=vec3(0.);
   for(int i=0;i<22;i++){ float fi=float(i); float h=.8+fi*.045;
     vec2 p=d.xz*(h/max(d.y,.04))*.12+vec2(uTime*.012,0.);
     float w=fbm3(p*vec2(.7,2.2)+vec2(0.,uTime*.03));
     float b=pow(1.-abs(sin(p.x*3.1+w*7.)),7.);
     a+=hue(fi/22.*.6+.35+uTime*.02)*b*(1.-fi/22.)*.07; }
   c=c*(1.-clamp(length(a)*uAurora*.5,0.,.55))+a*smoothstep(0.,.18,d.y)*uAurora*1.6; }

 // sun: normal or synthwave with stripes
 if(uSynth>0.){ vec3 rt=normalize(cross(sd,vec3(0.,1.,0.))), up=cross(rt,sd);
   float R=sqrt(2.*sunSize)*1.25; vec2 l=vec2(dot(d,rt),dot(d,up))/R;
   float rr=length(l); vec3 sc=mix(vec3(1.,.15,.6),vec3(1.,.85,.2),smoothstep(-1.,.9,l.y));
   float gap=step(.5,fract(l.y*7.+uTime*.35))*step(l.y,.15)*smoothstep(-1.,.15,l.y)*1.;
   float disk=smoothstep(1.,.98,rr)*(1.-gap*smoothstep(.15,-1.,l.y)*1.5);
   c=mix(c,sc*1.6,clamp(disk,0.,1.)); c+=vec3(1.,.2,.7)*exp(-max(rr-1.,0.)*3.)*.35*step(1.,rr);
   c+=vec3(1.,.3,.8)*exp(-abs(d.y+.02)*60.)*.5; }
 else { c+=sunCol*(pow(s,6.)*.18+pow(s,60.)*.45);
   c=mix(c,sunCol*1.6,smoothstep(1.-sunSize*1.15,1.-sunSize,s)); }

 // clouds on a plane overhead, lit from the sun side
 if(uCover>0.&&d.y>0.){ vec2 p=d.xz/(d.y+.03)*.55+vec2(uCamX*.00004+uTime*.006,0.);
   vec2 q=vec2(fbm3(p*.8+uTime*.01),fbm3(p*.8+vec2(4.7,2.1)));
   float n=fbm(p+1.6*q);
   float dens=smoothstep(1.-uCover,1.-uCover+.32,n);
   float n2=fbm(p+1.6*q+sd.xz*.12);
   float lit=clamp((n-n2)*5.+.55,0.,1.);
   vec3 cc=mix(uShade,uLit,lit);
   cc+=sunCol*pow(s,8.)*(1.-dens)*1.4;
   cc+=uUnder*uUnderK*(1.-lit)*(.6+.4*sin(uTime*1.3+p.x*3.))*.9;
   dens*=smoothstep(.0,.12,d.y);
   c=mix(c,cc,dens*.92); }

 // dusk at the finish: the day's sky gives way to an evening one, so the fireworks show
 if(uDusk>0.){ float h=smoothstep(-.12,.6,d.y);
   vec3 dk=mix(vec3(.55,.16,.06),vec3(.11,.035,.13),smoothstep(0.,.22,h));
   dk=mix(dk,vec3(.008,.012,.05),smoothstep(.2,1.,h));
   dk+=vec3(1.,.38,.1)*pow(s,4.)*.55*(1.-h);                // the low sun's glow
   dk+=c*.1;                                                 // a trace of the level's own colours
   c=mix(c,dk,uDusk); }
 float st=max(stars,uDusk*smoothstep(.05,.4,d.y));
 if(st>0.){ vec2 q=vec2(atan(d.z,d.x)*260.,asin(clamp(d.y,-1.,1.))*260.); vec2 f=floor(q); float n=hash(f);
   float tw=.55+.45*sin(uTime*2.5+n*60.); float r=length(fract(q)-.5); c+=vec3(1.)*step(.993,n)*smoothstep(.5,.1,r)*tw*st; }
 gl_FragColor=linearToOutputTexel(vec4(c,1.)); }`,
        });
        timed.push(m);
        const mesh = new T.Mesh(new T.SphereGeometry(30000, 48, 24), m);
        mesh.renderOrder = -10; mesh.frustumCulled = false;
        return mesh;
    }

    function buildGround(th, xmin, xmax) {
        // x samples: the original's points inside the level, even steps outside
        const xs = [];
        for (let x = xmin; x < ridge[0].x; x += x < -4000 ? 200 : 70) xs.push(x);
        for (const p of ridge) xs.push(p.x);
        for (let x = ridge[ridge.length - 1].x + 70; x <= xmax; x += x > endX + 4000 ? 200 : 70) xs.push(x);
        const zs = [0, -15, -45, -100, -180, -290, -430, -600, -800, -1040, -1320, -1640, -2000, -2400, -2850, -3300, -3900, -4700, -5800, -7200, -9000, -11500, -14500];
        const pos = [], idx = [], W = xs.length;
        for (const z of zs) for (const x of xs) pos.push(x, -surfY(x, z), z);
        for (let j = 0; j < zs.length - 1; j++)
            for (let i = 0; i < W - 1; i++) {
                const a = j * W + i, b = a + 1, c = a + W, d = c + 1;
                idx.push(a, b, c, b, d, c);
            }
        const g = new T.BufferGeometry();
        g.setAttribute('position', new T.Float32BufferAttribute(pos, 3));
        g.setIndex(idx);
        g.computeVertexNormals();
        const N = g.attributes.normal.array, col = [];
        const top = lin(th.grass), top2 = lin(th.grass2), rock = lin(th.rock), c = new T.Color();
        for (let i = 0; i < pos.length; i += 3) {
            const ny = N[i + 1], n = fbm(pos[i] * 0.004, pos[i + 2] * 0.004);
            c.copy(top2).lerp(top, n);
            c.lerp(rock, 1 - T.MathUtils.smoothstep(ny, 0.55, 0.85));
            col.push(c.r, c.g, c.b);
        }
        g.setAttribute('color', new T.Float32BufferAttribute(col, 3));
        const tx = txOn(th), gmat = new T.MeshStandardMaterial({ vertexColors: true, roughness: 0.95, metalness: 0 });
        if (tx) {
            const e = new Float32Array(pos.length / 3);
            for (let i = 0; i < e.length; i++) e[i] = fbm(pos[i * 3] * 0.0016 + 7, pos[i * 3 + 2] * 0.0016);
            g.setAttribute('aE', new T.BufferAttribute(e, 1));
            txGround(gmat);
        }
        const mesh = new T.Mesh(g, gmat);
        world.add(mesh);
        if (th.neon) { // a neon grid laid over the ground
            const grid = shaderMat('', `vec2 g=abs(fract(vW.xz/140.)-.5); float l=smoothstep(.44,.5,max(g.x,g.y));
 float p=.6+.4*sin(uTime*2.-vW.x*.002); outc=vec4(mix(vec3(0.,.9,1.),vec3(1.,.2,.9),step(.5,fract(vW.z/1400.)))*l*p*1.5,l);`, {},
                { transparent: true, additive: true, polygonOffset: true });
            world.add(new T.Mesh(g, grid));
        }

        // the cut face at z = 0: the soil under the playing line
        const sh = new T.Shape();
        sh.moveTo(xs[0], -heightAt(xs[0]));
        for (const x of xs) sh.lineTo(x, -heightAt(x));
        sh.lineTo(xs[W - 1], -1800);
        sh.lineTo(xs[0], -1800);
        const face = new T.Mesh(tx ? txFaceGeo(xs) : new T.ShapeGeometry(sh), shaderMat(
            'uniform vec3 uS,uS2; uniform float uCrack; ' + (tx ? TX_SOIL_DECL : '') + FIRE_GLSL,
            `vec2 p=vW.xy; float st=noise(vec2(p.x*.0025,p.y*.03))*.6+noise(vec2(p.x*.011,p.y*.11))*.4;
 vec3 c=mix(uS2,uS,st);
 float pb=noise(p*.07); c=mix(c,uS2*.55,smoothstep(.78,.86,pb)*.6);
${tx ? TX_SOIL : ''}
 c*=.7+.3*smoothstep(-1700.,-250.,p.y);
 if(uCrack>0.){ float n=noise(p*.009+vec2(0.,uTime*.05)); float cr=1.-smoothstep(0.,.025,abs(n-.5));
   c+=vec3(1.,.32,.04)*cr*(.7+.3*sin(uTime*2.3+p.x*.01))*1.4; }
 vec3 fl=fireLight(vW); c+=c*fl*.9+fl*.02;
 outc=vec4(c,1.);`,
            { ...FIRE_U, uS: { value: lin(th.soil) }, uS2: { value: lin(th.soil2) }, uCrack: { value: th.cracks ? 1 : 0 }, ...(tx ? { uE: { value: TX.tex[TX.set.earth].c }, uG: { value: TX.tex[TX.set.rock].c }, uEN: { value: TX.tex[TX.set.earth].n }, uGN: { value: TX.tex[TX.set.rock].n }, uHum: { value: new T.Vector3(...TX.set.hum) }, uSub: { value: new T.Vector3(...TX.set.sub) }, uBed: { value: new T.Vector3(...TX.set.bed) }, uRoot: { value: TX.set.root }, uTop: { value: TX.set.top }, uStB: { value: TX.set.stoneB } } : {}) }));
        world.add(face);

        // the lip along the line: grass, snow, sand, icing...
        const lp = [], lu = [], li = [];
        const lipPts = xs.map((x) => ({ x, y: heightAt(x) }));
        for (let i = 0; i < lipPts.length; i++) {
            const p = lipPts[i], th2 = (tx ? 34 + 26 * vnoise(p.x * 0.012, 2.7) : 20) + 16 * vnoise(p.x * 0.02, 1.3) + (th.icing ? 22 * Math.max(0, vnoise(p.x * 0.05, 4.1) - 0.45) * 3 : 0);
            lp.push(p.x, -p.y + 3, 1, p.x, -(p.y + th2), 1);
            lu.push(p.x / 100, 1, p.x / 100, 0);
            if (i < lipPts.length - 1) { const a = i * 2; li.push(a, a + 2, a + 1, a + 1, a + 2, a + 3); }
        }
        const lg = new T.BufferGeometry();
        lg.setAttribute('position', new T.Float32BufferAttribute(lp, 3));
        lg.setAttribute('uv', new T.Float32BufferAttribute(lu, 2));
        lg.setIndex(li);
        const lip = new T.Mesh(lg, shaderMat('uniform vec3 uA,uB; uniform float uMode; ' + (tx ? 'uniform sampler2D uF; uniform vec3 uLF; uniform float uLB,uLS; ' : '') + FIRE_GLSL,
            `float n=noise(vec2(vUv.x*6.,vUv.y*3.));
 vec3 c=mix(uB,uA,smoothstep(0.,.7,vUv.y)+n*.2);
${tx ? ` float jag=fbm(vec2(vUv.x*4.,3.))*.42+noise(vec2(vUv.x*31.,1.))*.3; if(vUv.y<jag) discard;
 float lf=smoothstep(jag,min(jag+.35,1.),vUv.y); vec3 tf=texture2D(uF,vW.xy/300.).rgb;
 c=mix(tf*uLF,tf*uLB,smoothstep(.15,.85,lf))*mix(uLS,1.,lf);
` : ''} if(uMode>.5&&uMode<1.5){ c=hue(vUv.x*.08-uTime*.05)*mix(.7,1.1,vUv.y); }
 if(uMode>1.5){ c=mix(vec3(0.,.8,1.),vec3(1.,.2,.9),.5+.5*sin(vUv.x*.6-uTime*2.))*(1.2+.5*smoothstep(.7,1.,vUv.y)); }
 vec3 fl=fireLight(vW); c+=c*fl*.9+fl*.02;
 outc=vec4(c,1.);`,
            { ...FIRE_U, uA: { value: lin(th.grass) }, uB: { value: lin(th.grass2) }, uMode: { value: th.rainbowLip ? 1 : th.neon ? 2 : 0 }, ...(tx ? { uF: { value: TX.tex[TX.set.surface].c }, uLF: { value: new T.Vector3(...TX.set.lipF) }, uLB: { value: TX.set.lipB }, uLS: { value: TX.set.lipS } } : {}) }, { side: T.DoubleSide }));
        world.add(lip);
        if (tx) txEdge(ridge[0].x - 2500, endX + 2500);
    }

    function buildMountains(th, xmin, xmax) {
        for (const [z0, peak, amp, cHi, cLo, snowLine, freq] of th.mountains) {
            const tx = txOn(th), base = 2600, xs = [], step = tx ? 50 : 120;
            const span = (D - z0) / D * 1200; // wider for the far ones
            for (let x = xmin - span; x <= xmax + span; x += step) xs.push(x);
            const low = tx ? 95 * (D - z0) / D : 0; // a level with its own clouds keeps some sky above the ridges
            const top = xs.map((x) => low + peak + amp * (1 - Math.pow(fbm(x * freq + z0 * 0.01, z0 * 0.001, 5), 1.2) * 1.7) + (tx ? (fbm(x * 0.007, z0 * 0.01, 4) - 0.5) * 170 : 0));
            const rows = tx ? Array.from({ length: 15 }, (_, i) => [i / 14, 1700 * Math.pow(i / 14, 1.1)]) : [[0, 0], [0.18, 260], [0.45, 650], [0.75, 1150], [1, 1700]];
            const pos = [], col = [], idx = [], W = xs.length, c = new T.Color();
            const hi = lin(cHi), lo = lin(cLo), snowC = lin('#f6f9fc');
            for (const [f, dz] of rows)
                for (let i = 0; i < W; i++) {
                    const y = top[i] + (base - top[i]) * f + (f > 0 && f < 1 ? (vnoise(xs[i] * 0.004, f * 9) - 0.5) * 220 : 0) + (tx && f > 0 && f < 1 ? (fbm(xs[i] * 0.008, f * 13 + z0 * 0.002, 4) - 0.5) * 520 * Math.sqrt(Math.sin(f * Math.PI)) : 0);
                    pos.push(xs[i], -y, z0 + dz);
                    c.copy(lo).lerp(hi, 1 - f);
                    if (snowLine !== null && y < snowLine + low + vnoise(xs[i] * 0.01, 2) * 300) c.lerp(snowC, 0.85);
                    col.push(c.r, c.g, c.b);
                }
            for (let j = 0; j < rows.length - 1; j++)
                for (let i = 0; i < W - 1; i++) { const a = j * W + i; idx.push(a, a + W, a + 1, a + 1, a + W, a + W + 1); }
            const g = new T.BufferGeometry();
            g.setAttribute('position', new T.Float32BufferAttribute(pos, 3));
            g.setAttribute('color', new T.Float32BufferAttribute(col, 3));
            g.setIndex(idx);
            g.computeVertexNormals();
            const mmat = new T.MeshStandardMaterial({ vertexColors: true, roughness: 1, flatShading: true });
            if (tx) txRock(mmat, 1500, 0.9);
            world.add(new T.Mesh(g, mmat));
        }
    }

    // trees and other things standing on the ground behind the line
    function treeGeo(kind) {
        const C = (h) => lin(h);
        switch (kind) {
            case 'pine': return merge([
                { geo: new T.CylinderGeometry(6, 9, 50, 6), m: M4(0, 25, 0), color: C('#5a3b22') },
                { geo: new T.ConeGeometry(62, 90, 8), m: M4(0, 80, 0), color: C('#2f6b32') },
                { geo: new T.ConeGeometry(50, 78, 8), m: M4(0, 125, 0), color: C('#357a38') },
                { geo: new T.ConeGeometry(36, 66, 8), m: M4(0, 165, 0), color: C('#3d8a40') }]);
            case 'snowpine': {
                const snowy = (x, y, z, nx, ny) => C(ny > 0.35 ? '#f4f8fc' : '#2c5a3a');
                return merge([
                    { geo: new T.CylinderGeometry(6, 9, 50, 6), m: M4(0, 25, 0), color: C('#4a3424') },
                    { geo: new T.ConeGeometry(62, 90, 8), m: M4(0, 80, 0), color: snowy },
                    { geo: new T.ConeGeometry(50, 78, 8), m: M4(0, 125, 0), color: snowy },
                    { geo: new T.ConeGeometry(36, 66, 8), m: M4(0, 165, 0), color: snowy }]);
            }
            case 'oak': return merge([
                { geo: new T.CylinderGeometry(9, 14, 90, 7), m: M4(0, 45, 0), color: C('#5b3d24') },
                { geo: new T.IcosahedronGeometry(68, 1), m: M4(0, 140, 0), color: C('#4c8a34') },
                { geo: new T.IcosahedronGeometry(48, 1), m: M4(-42, 115, 10), color: C('#457f2f') },
                { geo: new T.IcosahedronGeometry(44, 1), m: M4(40, 120, -8), color: C('#55963a') }]);
            case 'jungle': return merge([
                { geo: new T.CylinderGeometry(9, 15, 210, 7), m: M4(0, 105, 0), color: C('#4d3a24') },
                { geo: new T.IcosahedronGeometry(80, 1), m: M4(0, 230, 0, 0, 0, 0, 1), color: C('#2c7a26') },
                { geo: new T.IcosahedronGeometry(60, 1), m: M4(-60, 205, 0), color: C('#246a20') },
                { geo: new T.IcosahedronGeometry(58, 1), m: M4(62, 210, 0), color: C('#348a2c') }]);
            case 'cactus': return merge([
                { geo: new T.CylinderGeometry(15, 17, 170, 8), m: M4(0, 85, 0), color: C('#3f7f3a') },
                { geo: new T.SphereGeometry(15, 8, 6), m: M4(0, 170, 0), color: C('#4a8f44') },
                { geo: new T.CylinderGeometry(10, 11, 46, 7), m: M4(-30, 82, 0, 0, 0, Math.PI / 2), color: C('#3f7f3a') },
                { geo: new T.CylinderGeometry(10, 10, 60, 7), m: M4(-52, 112, 0), color: C('#3f7f3a') },
                { geo: new T.CylinderGeometry(10, 11, 40, 7), m: M4(26, 108, 0, 0, 0, Math.PI / 2), color: C('#3f7f3a') },
                { geo: new T.CylinderGeometry(10, 10, 46, 7), m: M4(44, 130, 0), color: C('#3f7f3a') }]);
            case 'deadtree': return merge([
                { geo: new T.CylinderGeometry(5, 12, 140, 6), m: M4(0, 70, 0), color: C('#1e1410') },
                { geo: new T.CylinderGeometry(3, 6, 70, 5), m: M4(-24, 120, 0, 0, 0, 0.7), color: C('#1e1410') },
                { geo: new T.CylinderGeometry(3, 5, 60, 5), m: M4(22, 135, 0, 0, 0, -0.8), color: C('#1e1410') }]);
            case 'spike': return merge([
                { geo: new T.ConeGeometry(34, 220, 5), m: M4(0, 110, 0), color: C('#2a0606') },
                { geo: new T.ConeGeometry(18, 120, 5), m: M4(30, 60, 0, 0, 0, -0.35), color: C('#3a0808') }]);
            case 'lollipop': {
                const swirl = (x, y, z) => { const a = Math.atan2(y - 150, x); const r = Math.hypot(x, y - 150); return C(Math.sin(a * 3 + r * 0.25) > 0 ? '#ffffff' : '#ff4fa0'); };
                return merge([
                    { geo: new T.CylinderGeometry(3, 3, 130, 6), m: M4(0, 65, 0), color: C('#ffffff') },
                    { geo: new T.CylinderGeometry(48, 48, 14, 28), m: M4(0, 150, 0, Math.PI / 2, 0, 0), color: swirl }]);
            }
            case 'gumdrop': return merge([
                { geo: new T.SphereGeometry(55, 14, 10, 0, Math.PI * 2, 0, Math.PI / 2), m: M4(0, 0, 0, 0, 0, 0, 1), color: C('#ffffff') }]);
            case 'crystal': return merge([
                { geo: new T.OctahedronGeometry(1, 0), m: new T.Matrix4().makeScale(40, 130, 40).setPosition(0, 110, 0), color: C('#ffffff') },
                { geo: new T.OctahedronGeometry(1, 0), m: new T.Matrix4().makeScale(24, 80, 24).multiply(new T.Matrix4().makeRotationZ(0.4)).setPosition(36, 70, 0), color: C('#ffffff') }]);
        }
    }
    // ---- wind in the trees: in the vertex shader, so no work for the processor.
    // Each tree bends from its foot (more the higher up, squared), leans a little
    // downwind and sways on two slow waves; a gust runs along the level. The phase
    // comes from where the tree stands, so neighbours do not move in step. The wind
    // blows the same way for all of them (worked back through each tree's own turn).
    // The shadow uses the same bend (a depth material with the same code).
    // Stiff things barely move: cactus, lollipop; spikes, gumdrops and crystals not at all.
    const WIND = { t: { value: 0 } };
    const SWAY = { pine: [1, 200], snowpine: [0.8, 200], oak: [0.75, 190], jungle: [1.3, 280], deadtree: [0.35, 120], cactus: [0.08, 190], lollipop: [0.22, 180] };
    // attr: the bend comes from an attribute aSway (0 at the foot, 1 at the tip) instead of
    // the height in the model, and the phase from the vertex's own place: for merged
    // meshes such as the grass tufts, where many blades share one model
    function windify(mat, k, h, attr) {
        const prev = mat.onBeforeCompile;
        mat.onBeforeCompile = (sh, r) => {
            if (prev) prev(sh, r);
            sh.uniforms.uWindT = WIND.t; sh.uniforms.uWindK = { value: k }; sh.uniforms.uWindH = { value: h };
            sh.vertexShader = 'uniform float uWindT,uWindK,uWindH;\n' + (attr ? 'attribute float aSway;\n' : '') + sh.vertexShader.replace('#include <begin_vertex>', `#include <begin_vertex>
 { float wh=${attr ? 'aSway' : 'clamp(transformed.y/uWindH,0.,1.4)'}; vec3 ip=${attr ? 'transformed' : 'vec3(0.)'}; mat3 im=mat3(1.);
#ifdef USE_INSTANCING
   ip=instanceMatrix[3].xyz; im=mat3(instanceMatrix);
#endif
   float ph=ip.x*.0021+ip.z*.0013, gust=.55+.45*sin(uWindT*.35-ip.x*.0006);
   float s=(sin(uWindT*1.3+ph)*.7+sin(uWindT*2.9+ph*1.7)*.3)*gust, b=uWindK*wh*wh;
   vec3 w=vec3(b*(s*14.+9.*gust),-b*abs(s)*2.,b*sin(uWindT*1.1+ph*2.3)*5.);
   transformed+=transpose(im)*w/max(dot(im[0],im[0]),1e-4); }`);
        };
        mat.customProgramCacheKey = () => attr ? 'wind-attr' : 'wind';
        return mat;
    }
    function windDepth(k, h) {
        return windify(new T.MeshDepthMaterial({ depthPacking: T.RGBADepthPacking }), k, h);
    }
    function buildTrees(th, xmin, xmax) {
        if (!th.trees.length) return;
        const total = Math.floor((xmax - xmin) / 22 * th.treeDensity);
        for (const [kind, share] of th.trees) {
            const n = Math.floor(total * share);
            const geo = treeGeo(kind);
            const shiny = kind === 'crystal';
            const mat = shiny
                ? new T.MeshStandardMaterial({ vertexColors: true, roughness: 0.15, metalness: 0.2, flatShading: true, emissive: new T.Color(0.12, 0.05, 0.15) })
                : new T.MeshStandardMaterial({ vertexColors: true, roughness: 0.85, flatShading: true });
            const im = new T.InstancedMesh(geo, mat, n);
            const m = new T.Matrix4(), q = new T.Quaternion(), s = new T.Vector3(), p = new T.Vector3(), c = new T.Color();
            let k = 0;
            for (let i = 0; i < n * 3 && k < n; i++) {
                const z = -(650 + Math.pow(rnd(), 0.7) * 2600);
                const x = rr(xmin, xmax);
                if (kind !== 'jungle' && fbm(x * 0.0015, z * 0.0015) < 0.38) continue; // in groves
                const y = surfY(x, z);
                const sc = rr(0.7, 1.3) * (kind === 'gumdrop' ? 0.8 : 1);
                p.set(x, -y - 8, z);
                q.setFromEuler(new T.Euler(0, rr(0, Math.PI * 2), kind === 'lollipop' ? 0 : rr(-0.06, 0.06)));
                if (kind === 'lollipop') q.setFromEuler(new T.Euler(0, rr(-0.5, 0.5), rr(-0.1, 0.1)));
                s.set(sc, sc * rr(0.85, 1.2), sc);
                m.compose(p, q, s);
                im.setMatrixAt(k, m);
                if (kind === 'lollipop') c.setHSL(rr(0, 1), 0.7, 0.75);
                else if (kind === 'gumdrop') c.setHSL(rr(0, 1), 0.85, 0.6);
                else if (kind === 'crystal') c.setHSL(rr(0, 1), 0.8, 0.7);
                else c.setHSL(0, 0, rr(0.8, 1.15));
                im.setColorAt(k, c);
                k++;
            }
            im.count = k;
            if (txOn(th) && (kind === 'pine' || kind === 'oak' || kind === 'snowpine' || kind === 'jungle' || (kind === 'cactus' && TX.set.cactus) || (kind === 'deadtree' && TX.set.bark !== 'pine-bark'))) { txTree(kind, im); im.dispose(); continue; }
            const sw = SWAY[kind];
            if (sw) { windify(mat, sw[0], sw[1]); im.customDepthMaterial = windDepth(sw[0], sw[1]); }
            world.add(im);
        }
    }

    // liquids: water, ice, chocolate, lava, rainbow
    const FLOW = {
        water: { mode: 0, a: '#2d7fb0', b: '#bfe7f7', f: '#ffffff', speed: 0.9 },
        ice: { mode: 1, a: '#a8d4ec', b: '#eaf8ff', f: '#ffffff', speed: 0.07 },
        choc: { mode: 2, a: '#3a1d0e', b: '#7a4524', f: '#f3d9b5', speed: 0.6 },
        lava: { mode: 3, a: '#ff3a00', b: '#ffd23a', f: '#1a0703', speed: 0.25 },
        rainbow: { mode: 4, a: '#ffffff', b: '#ffffff', f: '#ffffff', speed: 0.7 },
    };
    function flowMat(kind, sx, sy) {
        const f = FLOW[kind];
        return shaderMat('uniform float uMode,uSpeed,uSX,uSY; uniform vec3 uA,uB,uF;',
            `vec2 uv=vUv; float t=uTime*uSpeed;
 float n1=fbm(vec2(uv.x*uSX,uv.y*uSY-t));
 float n2=noise(vec2(uv.x*uSX*2.3,uv.y*uSY*1.7-t*1.7));
 float edge=smoothstep(0.,.14,uv.x)*smoothstep(1.,.86,uv.x);
 vec3 c; float a=1.;
 if(uMode<2.5){
   c=mix(uA,uB,smoothstep(.3,.8,n1));
   float foam=smoothstep(.6,.9,n2)*.8+smoothstep(.08,0.,uv.y)*.9+smoothstep(.8,1.,uv.y)*.5;
   c=mix(c,uF,clamp(foam,0.,1.));
   a=edge*(.8+.2*n1);
 } else if(uMode<3.5){
   float crust=smoothstep(.52,.66,n1);
   vec3 hot=mix(uA,uB,smoothstep(.2,.9,n2));
   c=mix(hot*1.7,uF,crust*.85);
   a=edge;
 } else {
   c=hue(uv.x*.9+uv.y*.4-t*.25);
   c=mix(c,vec3(1.),smoothstep(.62,.95,n2)*.7+smoothstep(.08,0.,uv.y)*.6);
   a=edge*(.82+.18*n1);
 }
 outc=vec4(c,a);`,
            { uMode: { value: f.mode }, uSpeed: { value: f.speed }, uSX: { value: sx }, uSY: { value: sy }, uA: { value: lin(f.a) }, uB: { value: lin(f.b) }, uF: { value: lin(f.f) } },
            { transparent: true, side: T.DoubleSide, depthWrite: false });
    }
    function glowMat(color, strength) {
        return shaderMat('uniform vec3 uC; uniform float uK;',
            `float d=abs(vUv.x*2.-1.); float a=pow(1.-d,2.)*uK*(.8+.2*sin(uTime*3.+vUv.y*20.)); outc=vec4(uC*a,a);`,
            { uC: { value: lin(color) }, uK: { value: strength } }, { transparent: true, additive: true, depthWrite: false, side: T.DoubleSide });
    }

    function rockMat(hex) { return new T.MeshStandardMaterial({ color: lin(hex), roughness: 1, flatShading: true }); }
    function displace(g, amt, f) {
        const P = g.attributes.position;
        for (let i = 0; i < P.count; i++) {
            const x = P.getX(i), y = P.getY(i), z = P.getZ(i);
            P.setXYZ(i, x + (vnoise(y * f, z * f) - 0.5) * amt, y + (vnoise(x * f, z * f + 5) - 0.5) * amt * 0.5, z + (vnoise(x * f + 9, y * f) - 0.5) * amt);
        }
        g.computeVertexNormals();
        return g;
    }

    function buildFalls(th, kind, xmin, xmax) {
        const n = Math.max(1, Math.round((xmax - xmin) / 6000 * th.fallCount));
        const tex = textures();
        for (let i = 0; i < n; i++) {
            const x = xmin + (i + 0.5) / n * (xmax - xmin) + rr(-600, 600);
            const z = rr(-3900, -3500);
            const topY = rr(-700, -150), botY = 2400; // 2D y
            const H = botY - topY, w = rr(170, 260);
            // the cliff: a rough block, lower towards its sides
            const cliffW = txOn(th) ? 3400 : 1500, cliffDrop = txOn(th) ? 950 : 500;
            const cg = new T.BoxGeometry(cliffW, H, 900, txOn(th) ? 36 : 16, 26, 4);
            const P = cg.attributes.position;
            for (let k = 0; k < P.count; k++) {
                const lx = P.getX(k), ly = P.getY(k);
                if (ly > H / 2 - 1) P.setY(k, ly - Math.pow(Math.abs(lx) / (cliffW / 2), 2) * cliffDrop - vnoise(lx * 0.01, 3) * 140);
            }
            displace(cg, 110, 0.006);
            let fallT = topY + 30; // where the water leaves the rock (2D y)
            if (txOn(th)) { let top = -1e9; for (let k = 0; k < P.count; k++) if (Math.abs(P.getX(k)) < w * 0.7) top = Math.max(top, P.getY(k)); fallT = (topY + botY) / 2 - top + 30; }
            const cliffMat = rockMat(kind === 'lava' ? '#2a1410' : kind === 'choc' ? '#c47aa8' : kind === 'ice' ? '#9fb2c6' : kind === 'rainbow' ? '#b080e0' : '#6f6a62');
            if (txOn(th)) { txRock(cliffMat, 620, 1.2); cg.computeVertexNormals(); }
            const cliff = new T.Mesh(cg, cliffMat);
            cliff.position.set(x, -(topY + botY) / 2, z);
            world.add(cliff);
            // the fall itself, a little in front of the face, bulging out as it drops
            const pts = [];
            for (let s = 0; s <= 24; s++) {
                const f = s / 24;
                pts.push(new T.Vector3(x + Math.sin(f * 3 + i) * 12, -(fallT + f * (botY - fallT)), z + 450 + 30 + Math.sqrt(f) * 120));
            }
            const r = ribbon(pts, (f) => w * (0.85 + 0.5 * f));
            const fall = new T.Mesh(r.geo, flowMat(kind, 3, r.length / w * 0.9));
            world.add(fall);
            // the stream on the top, coming over the edge
            const lip = [];
            for (let s = 0; s <= 10; s++) lip.push(new T.Vector3(x + Math.sin(s * 0.5 + i) * 30, -fallT + 2, z + 480 - (10 - s) * 50));
            const rl = ribbon(lip, () => w * 0.8, () => new T.Vector3(0, 1, 0));
            world.add(new T.Mesh(rl.geo, flowMat(kind, 3, rl.length / w)));
            if (kind === 'lava') {
                const gr = ribbon(pts, (f) => w * (2.6 + f));
                world.add(new T.Mesh(gr.geo, glowMat('#ff5a10', 0.55)));
                const pl = new T.PointLight(0xff6a20, 2.2, 5000, 2);
                pl.position.set(x, -(topY + 600), z + 900);
                world.add(pl);
            }
            // spray and mist along the lower part
            const mist = [];
            for (let k = 0; k < 10; k++) {
                const sm = new T.SpriteMaterial({ map: kind === 'lava' ? tex.smoke : tex.dot, color: kind === 'lava' ? 0x8a6a60 : 0xffffff, transparent: true, opacity: 0.0, depthWrite: false });
                const sp = new T.Sprite(sm);
                const ph = rnd();
                world.add(sp);
                mist.push({ sp, ph });
            }
            addTimed((t) => {
                for (const m of mist) {
                    const s = (t * 0.07 + m.ph) % 1;
                    const y = topY + H * (0.25 + 0.5 * m.ph) - s * 500;
                    m.sp.position.set(x + Math.sin(m.ph * 20 + t * 0.3) * w * 0.8, -y, z + 620);
                    const sc = w * (1.6 + s * 3);
                    m.sp.scale.set(sc, sc, 1);
                    m.sp.material.opacity = Math.sin(s * Math.PI) * (kind === 'ice' ? 0.12 : 0.28);
                }
            });
        }
    }

    function buildVolcanoes(th, n, xmin, xmax) {
        const tex = textures();
        for (let i = 0; i < n; i++) {
            const x = xmin + (i + 0.5) / n * (xmax - xmin) + rr(-1500, 1500);
            const z = rr(-8200, -6200);
            const topY = rr(-1900, -1300), baseY = 2600, H = baseY - topY;
            const rTop = 380, rBase = 3400;
            const g = new T.CylinderGeometry(rTop, rBase, H, 72, 18, true);
            const P = g.attributes.position;
            for (let k = 0; k < P.count; k++) {
                const px = P.getX(k), py = P.getY(k), pz = P.getZ(k);
                const a = Math.atan2(pz, px), f = (py + H / 2) / H;
                const r = Math.hypot(px, pz) * (1 + (vnoise(a * 4, f * 6) - 0.5) * 0.18 * (1 - f * 0.6));
                P.setXYZ(k, Math.cos(a) * r, py + (f > 0.99 ? -vnoise(a * 7, 1) * 120 : 0), Math.sin(a) * r);
            }
            g.computeVertexNormals();
            const cmat = rockMat(th.volcanoes > 1 ? '#3a2620' : '#2a0c08');
            if (txOn(th)) txRock(cmat, 1100, 1);
            const cone = new T.Mesh(g, cmat);
            const cy = -(topY + baseY) / 2;
            cone.position.set(x, cy, z);
            world.add(cone);
            // crater glow and light
            const glow = new T.Sprite(new T.SpriteMaterial({ map: tex.dot, color: 0xff6a1a, transparent: true, opacity: 0.9, blending: T.AdditiveBlending, depthWrite: false }));
            glow.position.set(x, -topY + 40, z + 200);
            glow.scale.set(1400, 700, 1);
            world.add(glow);
            const pl = new T.PointLight(0xff5a1a, 3, 9000, 2);
            pl.position.set(x, -topY + 300, z + 900);
            world.add(pl);
            // lava running down the side that faces us
            const flows = 4;
            for (let j = 0; j < flows; j++) {
                let th0 = Math.PI / 2 + (j - (flows - 1) / 2) * 0.32 + rr(-0.08, 0.08);
                const pts = [];
                const hStart = H * rr(0.93, 0.99), hEnd = H * rr(0.15, 0.4);
                for (let s = 0; s <= 40; s++) {
                    const h = hStart + (hEnd - hStart) * (s / 40);
                    th0 += (vnoise(s * 0.3, j * 7.1 + i) - 0.5) * 0.05;
                    const r = rTop + (rBase - rTop) * (1 - h / H);
                    const rr2 = r * (1 + (vnoise(th0 * 4, (h / H) * 6) - 0.5) * 0.18 * (1 - (h / H) * 0.6)) + 14;
                    pts.push(new T.Vector3(x + Math.cos(th0) * rr2, cy - H / 2 + h, z + Math.sin(th0) * rr2));
                }
                const nrm = (p) => new T.Vector3(p.x - x, (rBase - rTop) / H * Math.hypot(p.x - x, p.z - z), p.z - z).normalize();
                const rb = ribbon(pts, (f) => 80 + 150 * f, nrm);
                world.add(new T.Mesh(rb.geo, flowMat('lava', 2.5, rb.length / 120)));
                const gl = ribbon(pts.map((p) => p.clone().add(nrm(p).multiplyScalar(6))), (f) => 360 + 300 * f, nrm);
                world.add(new T.Mesh(gl.geo, glowMat('#ff4a10', 0.4)));
            }
            // smoke rising from the crater, and lava thrown up
            const puffs = [];
            for (let k = 0; k < 16; k++) {
                const sp = new T.Sprite(new T.SpriteMaterial({ map: tex.smoke, color: 0x3a2a26, transparent: true, opacity: 0, depthWrite: false }));
                world.add(sp); puffs.push({ sp, ph: k / 16 });
            }
            const bombs = [];
            const bg = new T.SphereGeometry(18, 8, 6), bm = new T.MeshBasicMaterial({ color: lin('#ffb03a') });
            for (let k = 0; k < 14; k++) { const b = new T.Mesh(bg, bm); world.add(b); bombs.push({ b, ph: rnd(), vx: rr(-500, 500), vy: rr(900, 1700), vz: rr(-100, 300), per: rr(2.5, 4.5) }); }
            addTimed((t) => {
                for (const p of puffs) {
                    const s = (t * 0.035 + p.ph) % 1;
                    p.sp.position.set(x + s * 1800 + Math.sin(s * 6 + p.ph * 10) * 150, -topY + 150 + s * 3200, z + 100);
                    const sc = 500 + s * 2600; p.sp.scale.set(sc, sc, 1);
                    p.sp.material.opacity = Math.min(1, s * 6) * (1 - s) * 0.75;
                }
                for (const o of bombs) {
                    const s = ((t / o.per + o.ph) % 1) * o.per;
                    o.b.position.set(x + o.vx * s, -topY + 60 + o.vy * s - 450 * s * s, z + 200 + o.vz * s);
                    o.b.visible = o.vy * s - 450 * s * s > -200;
                }
                pl.intensity = 2.6 + Math.sin(t * 7.3 + i) * 0.4 + Math.sin(t * 13.1) * 0.2;
            });
        }
    }

    function buildMesas(xmin, xmax, tx) {
        const n = Math.max(2, Math.round((xmax - xmin) / 3500));
        const bands = ['#b8562a', '#d0743c', '#c4643a', '#e0925a', '#a84a22'].map(lin);
        for (let i = 0; i < n; i++) {
            const w = rr(900, 2200), topY = rr(-500, 150), H = 2500 - topY;
            const g = new T.BoxGeometry(w, H, rr(700, 1200), tx ? 42 : 14, tx ? 72 : 24, tx ? 12 : 4);
            const P = g.attributes.position;
            for (let k = 0; k < P.count; k++) { // tapering sides
                const f = (P.getY(k) + H / 2) / H;
                P.setX(k, P.getX(k) * (1.25 - 0.35 * f));
            }
            displace(g, 90, 0.008);
            const col = [], c = new T.Color();
            for (let k = 0; k < P.count; k++) {
                const y = P.getY(k) + H / 2;
                c.copy(bands[Math.floor(y / 140 + vnoise(P.getX(k) * 0.003, 1) * 2) % bands.length]);
                col.push(c.r, c.g, c.b);
            }
            g.setAttribute('color', new T.Float32BufferAttribute(col, 3));
            const mm = new T.MeshStandardMaterial({ vertexColors: true, roughness: 1, flatShading: true });
            if (tx) { g.computeVertexNormals(); txRock(mm, 520, 1.1); }
            const m = new T.Mesh(g, mm);
            m.position.set(xmin + (i + 0.5) / n * (xmax - xmin) + rr(-800, 800), -(topY + 2500) / 2, rr(-6000, -3600));
            world.add(m);
        }
    }

    function buildClouds([color, opacity, count], xmin, xmax) {
        const tex = textures();
        const n = Math.round(count * (xmax - xmin) / 6000);
        const cl = [];
        for (let i = 0; i < n; i++) {
            const sp = new T.Sprite(new T.SpriteMaterial({ map: tex.cloud, color: srgb(color), transparent: true, opacity: opacity * rr(0.6, 1), depthWrite: false }));
            const z = rr(-9000, -3000), s = rr(1400, 3200) * (D - z) / (D + 6000);
            sp.scale.set(s * 2, s, 1);
            sp.position.set(rr(xmin - 3000, xmax + 3000), -rr(-2600, -400) * (D - z) / (D + 6000), z);
            world.add(sp); duskMats.push([sp.material, sp.material.color.clone()]);
            cl.push({ sp, v: rr(20, 60) });
        }
        addTimed((t, dt) => { for (const c of cl) { c.sp.position.x += c.v * dt; if (c.sp.position.x > xmax + 4000) c.sp.position.x = xmin - 4000; } });
    }

    function buildMistBanks([color, opacity], xmin, xmax) {
        const tex = textures(), list = [];
        const n = Math.round((xmax - xmin) / 900);
        for (let i = 0; i < n; i++) {
            const z = rr(-3000, -1400), x = rr(xmin, xmax);
            const sp = new T.Sprite(new T.SpriteMaterial({ map: tex.cloud, color: srgb(color), transparent: true, opacity: opacity * rr(0.5, 1), depthWrite: false }));
            const s = rr(900, 1700);
            sp.scale.set(s * 2.2, s * 0.6, 1);
            sp.position.set(x, -(surfY(x, z) - 140), z);
            world.add(sp); duskMats.push([sp.material, sp.material.color.clone()]);
            list.push({ sp, ph: rnd() * 6, x0: x });
        }
        addTimed((t) => { for (const m of list) m.sp.position.x = m.x0 + Math.sin(t * 0.05 + m.ph) * 300; });
    }

    // particles around the camera: snow, embers, ash, fireflies, candy, sparkles, data
    function buildParticles(kind) {
        const tex = textures();
        const cfg = {
            snow: { n: 2200, color: '#ffffff', size: 9, add: false, vy: [60, 140], vx: [-40, 20], op: 0.9 },
            embers: { n: 900, color: '#ff8a2a', size: 9, add: true, vy: [-160, -60], vx: [-30, 60], op: 1 },
            ash: { n: 1000, color: '#6a5a55', size: 7, add: false, vy: [20, 70], vx: [-30, 30], op: 0.7 },
            fireflies: { n: 400, color: '#e8ff7a', size: 10, add: true, vy: [-10, 10], vx: [-10, 10], op: 1, blink: true },
            candy: { n: 280, color: '#ffffff', size: 14, add: false, vy: [-20, 20], vx: [-20, 20], op: 0.85, rainbow: true, sat: 0.45 },
            sparkles: { n: 300, color: '#ffffff', size: 9, add: true, vy: [-30, 10], vx: [-10, 10], op: 0.7, rainbow: true, sat: 0.5, blink: true },
            data: { n: 300, color: '#60d8e8', size: 7, add: true, vy: [-120, -50], vx: [0, 0], op: 0.55 },
        }[kind];
        const n = cfg.n, pos = new Float32Array(n * 3), col = new Float32Array(n * 3), vel = [];
        const X0 = -4000, X1 = 6000, Y0 = -1400, Y1 = 1500, Z0 = 300, Z1 = -4200;
        const c = lin(cfg.color), hc = new T.Color();
        for (let i = 0; i < n; i++) {
            pos[i * 3] = rr(X0, X1); pos[i * 3 + 1] = rr(Y0, Y1); pos[i * 3 + 2] = rr(Z1, Z0);
            vel.push([rr(...cfg.vx), -rr(...cfg.vy), rnd() * 6]);
            if (cfg.rainbow) { hc.setHSL(rnd(), cfg.sat ?? 0.9, 0.7); col.set([hc.r, hc.g, hc.b], i * 3); } else col.set([c.r, c.g, c.b], i * 3);
        }
        const g = new T.BufferGeometry();
        g.setAttribute('position', new T.BufferAttribute(pos, 3));
        g.setAttribute('color', new T.BufferAttribute(col, 3));
        const m = new T.PointsMaterial({ size: cfg.size, map: tex.dot, vertexColors: true, transparent: true, opacity: cfg.op, depthWrite: false, blending: cfg.add ? T.AdditiveBlending : T.NormalBlending, sizeAttenuation: true });
        const pts = new T.Points(g, m);
        pts.frustumCulled = false;
        world.add(pts);
        addTimed((t, dt) => {
            const cx = cameraX + 960, cy = -(cameraY + 540);
            for (let i = 0; i < n; i++) {
                const v = vel[i];
                let x = pos[i * 3] + (v[0] + Math.sin(t + v[2]) * 25) * dt, y = pos[i * 3 + 1] + v[1] * dt;
                // live in a box that follows the camera (relative to the camera, wrapped)
                let rx = x - cx, ry = y - cy;
                if (rx < X0) x += X1 - X0; else if (rx > X1) x -= X1 - X0;
                if (ry < Y0) y += Y1 - Y0; else if (ry > Y1) y -= Y1 - Y0;
                pos[i * 3] = x; pos[i * 3 + 1] = y;
            }
            g.attributes.position.needsUpdate = true;
            if (cfg.blink) m.opacity = 0.75 + 0.25 * Math.sin(t * 3);
        });
    }

    function buildSpace(xmin, xmax) {
        const tex = textures();
        // planets with bands, one with a ring
        const pl = [['#ff6b9d', '#8b008b', 900, 0.4], ['#4b5ad0', '#1a0033', 1400, 0], ['#e0b070', '#8a5a30', 650, 0.7]];
        const sunDir = new T.Vector3(0.6, 0.3, 1).normalize();
        pl.forEach(([a, b, r, ring], i) => {
            const z = -9000 - i * 3000, x = xmin + (i + 0.5) / 3 * (xmax - xmin), y = rr(-1800, -600) * (D - z) / D * 0.5;
            const c = new T.Vector3(x, -y, z);
            const m = shaderMat('uniform vec3 uA,uB,uC,uL; uniform float uR;',
                `vec3 n=normalize(vW-uC); float b=sin(vUv.y*38.+fbm(vUv*vec2(12.,5.))*5.);
 vec3 c=mix(uA,uB,.5+.5*b); float l=.18+.95*max(dot(n,uL),0.); outc=vec4(c*l,1.);`,
                { uA: { value: lin(a) }, uB: { value: lin(b) }, uC: { value: c }, uL: { value: sunDir }, uR: { value: r } }, { fog: false });
            const s = new T.Mesh(new T.SphereGeometry(r, 48, 32), m);
            s.position.copy(c);
            world.add(s);
            if (ring) {
                const rm = shaderMat('uniform vec3 uC; uniform float uR;',
                    `float d=length(vW-uC)/uR; float b=noise(vec2(d*28.,1.)); float a=smoothstep(1.35,1.45,d)*smoothstep(2.25,2.05,d)*(.35+.5*b);
 outc=vec4(mix(vec3(.8,.6,.4),vec3(1.,.85,.6),b),a);`, { uC: { value: c }, uR: { value: r } }, { transparent: true, side: T.DoubleSide, fog: false });
                const rg = new T.Mesh(new T.RingGeometry(r * 1.3, r * 2.3, 96, 1), rm);
                rg.position.copy(c); rg.rotation.set(-1.25, 0.25, 0.3);
                world.add(rg);
            }
        });
        // asteroids tumbling in front of the stars
        const n = Math.round((xmax - xmin) / 60);
        const ag = displace(new T.IcosahedronGeometry(1, 1), 0.5, 1.7);
        const at = TX.set && TX.tex[TX.set.rock], im = new T.InstancedMesh(ag, new T.MeshStandardMaterial(at ? { color: lin('#b8a890'), map: at.c, normalMap: at.n, roughness: 1, flatShading: true } : { color: lin('#8B7355'), roughness: 1, flatShading: true }), n);
        const rocks = [];
        for (let i = 0; i < n; i++) rocks.push({ p: new T.Vector3(rr(xmin - 2000, xmax + 2000), rr(-300, 1600), rr(-4500, -600)), s: rr(10, 55), r: new T.Euler(rnd() * 6, rnd() * 6, 0), w: rr(-0.8, 0.8), v: rr(15, 60) });
        const mt = new T.Matrix4(), q = new T.Quaternion(), sv = new T.Vector3();
        addTimed((t, dt) => {
            for (let i = 0; i < n; i++) {
                const o = rocks[i];
                o.p.x += o.v * dt; if (o.p.x > xmax + 2500) o.p.x = xmin - 2500;
                o.r.x += o.w * dt; o.r.y += o.w * 0.7 * dt;
                q.setFromEuler(o.r); sv.set(o.s, o.s * 0.8, o.s);
                mt.compose(o.p, q, sv); im.setMatrixAt(i, mt);
            }
            im.instanceMatrix.needsUpdate = true;
        });
        world.add(im);
        // a black hole with its disk
        const bh = shaderMat('', `vec2 p=vUv*2.-1.; float r=length(p); float a=atan(p.y,p.x);
 float disk=smoothstep(.95,.6,r)*smoothstep(.25,.4,r)*(.5+.5*noise(vec2(a*6.+uTime*.8,r*10.)));
 vec3 c=vec3(1.,.45,.1)*disk*1.6+vec3(.3,.1,.5)*smoothstep(1.,.4,r)*.4; float hole=smoothstep(.3,.24,r);
 outc=vec4(mix(c,vec3(0.),hole),max(disk,hole)+smoothstep(1.,.3,r)*.3);`, {}, { transparent: true, fog: false, depthWrite: false });
        const bhm = new T.Mesh(new T.PlaneGeometry(2400, 2400), bh);
        bhm.position.set((xmin + xmax) * 0.6, 900, -12000); bhm.rotation.z = 0.4;
        world.add(bhm);
    }

    function buildSkyline(xmin, xmax) {
        const parts = [];
        for (let x = xmin - 2000; x < xmax + 2000; x += rr(160, 420)) {
            const w = rr(160, 420), d = rr(160, 420), topY = rr(-700, 700), H = 2600 - topY;
            const g = new T.BoxGeometry(w, H, d);
            g.translate(x, -(topY + 2600) / 2, rr(-9500, -6000));
            parts.push(g);
        }
        const pos = [], idx = [];
        let off = 0;
        for (const g of parts) {
            pos.push(...g.attributes.position.array);
            for (const i of g.index.array) idx.push(i + off);
            off += g.attributes.position.count;
        }
        const g = new T.BufferGeometry();
        g.setAttribute('position', new T.Float32BufferAttribute(pos, 3));
        g.setAttribute('uv', new T.Float32BufferAttribute(new Float32Array(pos.length / 3 * 2), 2));
        g.setIndex(idx);
        const m = shaderMat('', `vec2 q=vec2((vW.x+vW.z)/34.,vW.y/44.); vec2 cell=floor(q); vec2 f=fract(q);
 float win=step(.22,f.x)*step(f.x,.78)*step(.25,f.y)*step(f.y,.75);
 float h=hash(cell); float lit=step(.8,h)*(.8+.2*sin(uTime*(.3+h*1.5)+h*40.));
 vec3 wc=mix(vec3(.55,.7,1.),vec3(.3,.85,.95),step(.93,h));
 vec3 c=vec3(.025,.025,.06)+wc*win*lit*.6; outc=vec4(c,1.);`, {});
        world.add(new T.Mesh(g, m));
    }

    // ---------- the figures ----------
    // Rob, the monsters, mines, saucers and Julian: the models of
    // tools/prerender/render.js (the same designs as the rendered LVGL look),
    // placed every frame where the game has them. Units there: Rob 1 = his
    // radius (30), a monster 1 = 25 (at the original's 1.7x, standing on y = 0),
    // a mine 1 = 20, a saucer 1 = 40, Julian 1 = 60 (standing on y = 0).
    const MAKERS = { slime: makeSlime, cactus: makeCactus, yeti: makeYeti, lava: makeLava, plant: makePlant,
                     alien: makeAlien, candy: makeCandy, robot: makeRobot, demon: makeDemon, crystal: makeCrystal };
    let figs = null;
    let shadowTex = null;
    function shadowMesh() { // a soft half ellipse lying on the ground behind the line
        if (!shadowTex) shadowTex = canvasTex(128, 64, (g, w, h) => {
            const rg = g.createRadialGradient(w / 2, h, 0, w / 2, h, w / 2);
            rg.addColorStop(0, 'rgba(0,0,0,0.85)'); rg.addColorStop(0.5, 'rgba(0,0,0,0.4)'); rg.addColorStop(1, 'rgba(0,0,0,0)');
            g.fillStyle = rg; g.fillRect(0, 0, w, h);
        });
        const geo = new T.PlaneGeometry(1, 1);
        geo.rotateX(-Math.PI / 2); geo.translate(0, 0, -0.5);
        const m = new T.Mesh(geo, new T.MeshBasicMaterial({ map: shadowTex, transparent: true, depthWrite: false, opacity: 0.6,
            polygonOffset: true, polygonOffsetFactor: -4, polygonOffsetUnits: -4, fog: false }));
        m.renderOrder = 1;
        return m;
    }
    function envFromSky(th) { // what the figures reflect: this level's sky over its ground
        const s = new T.Scene();
        s.add(new T.Mesh(sky.geometry, sky.material));
        const g = new T.Mesh(new T.CircleGeometry(25000, 32), new T.MeshBasicMaterial({ color: lin(th.grass2) }));
        g.rotation.x = -Math.PI / 2; g.position.y = -2500;
        s.add(g);
        if (envRT) envRT.dispose();
        envRT = pmrem.fromScene(s, 0.02, 10, 60000);
        g.geometry.dispose(); g.material.dispose();
        return envRT.texture;
    }
    function template(make, scale, env) {
        const o = make(), lights = [];
        o.traverse((c) => {
            if (c.isLight) lights.push(c); // no extra lights per figure: the world's light
            if (c.material) {
                const ms = Array.isArray(c.material) ? c.material : [c.material];
                for (const m of ms) if (m.isMeshStandardMaterial) {
                    m.envMap = env; m.envMapIntensity = (m.envMapIntensity ?? 1) * 0.9;
                    if (m.clearcoat !== undefined) m.clearcoat *= 0.6;
                }
            }
        });
        for (const l of lights) l.parent.remove(l);
        o.scale.setScalar(scale);
        return o;
    }
    function pool(tpl, withShadow) {
        const list = [];
        return {
            get(i) {
                while (list.length <= i) {
                    const o = tpl.clone(); world.add(o); shadowize(o);
                    const sh = withShadow ? shadowMesh() : null; if (sh) world.add(sh);
                    list.push({ o, sh });
                }
                list[i].o.visible = true;
                return list[i];
            },
            hideFrom(n) { for (let i = n; i < list.length; i++) { list[i].o.visible = false; if (list[i].sh) list[i].sh.visible = false; } },
        };
    }
    // a ghost Rob (ghost.js): the same figure, see-through and tinted, casting no shadow
    let ghostList = [];
    const PEER_COLS = [0xff5a5a, 0x6dff7a, 0xffa040, 0xb98cff, 0x40ffe0, 0xffffff, 0xc8ff40, 0xff4fa3]; // as mp.js
    // the fire wall of a race (mp.js): a sheet facing the camera, from far behind up to its
    // front at x, standing on the ground there: flames at the front, smoke behind and above,
    // an orange light just ahead of it.
    // Made at the first race (a new light makes three compile its shaders once).
    let wallX = null, wallObj = null;
    const WALL_W = 5000, WALL_H = 1800;
    function makeWall() {
        const m = shaderMat('', `
 float t=uTime, x=vUv.x, y=vUv.y;                       // x: 0 far behind .. 1 the front; y: 0 the ground
 float d=(1.-x)*${WALL_W.toFixed(1)};                       // world units behind the front
 float n=fbm(vec2(vW.x*.004, vW.y*.004 - t*1.2));
 float tongue=fbm(vec2(vW.x*.007+3., t*.9));            // each column its own height, changing
 float lick=fbm(vec2(vW.x*.016, vW.y*.012 - t*3.2));    // licks rising through the flames
 float front=smoothstep(0.,80.,d+(fbm(vec2(vW.y*.006, t*1.5))-.5)*420.); // a leaning, flickering front
 float fh=(.06+.42*tongue*tongue)*(1.-.5*smoothstep(0.,1400.,d))*(.6+.6*smoothstep(0.,250.,d));
 float f=front*smoothstep(fh, fh*.15, y+(lick-.5)*fh*.9);
 vec3 fc=mix(vec3(.75,.12,0.),vec3(1.,.42,.04),lick);
 fc=mix(fc,vec3(1.,.82,.4),smoothstep(.55,1.,f*lick*1.5)*smoothstep(fh,0.,y)); // hot near the ground
 float sm=front*smoothstep(.2,.7,n)*smoothstep(.95,.45,y)*smoothstep(fh*.6,fh*1.6,y)*(.45+.4*smoothstep(0.,700.,d)); // smoke above
 vec3 sc=mix(vec3(.06,.03,.02),vec3(.26,.06,.02),smoothstep(.5,0.,y));
 outc=vec4(mix(sc,fc,f), max(f*.95, sm*.8));`, {}, { transparent: true, fog: false, side: T.DoubleSide });
        const mesh = new T.Mesh(new T.PlaneGeometry(WALL_W, WALL_H), m);
        mesh.renderOrder = 6; mesh.frustumCulled = false;
        const light = new T.PointLight(0xff5a1a, 0, 1800, 1.8);
        world.add(mesh); world.add(light);
        return { mesh, light };
    }
    function syncWall(t) {
        if (wallX == null) { if (wallObj) { wallObj.mesh.visible = false; wallObj.light.intensity = 0; } return; }
        if (!wallObj) wallObj = makeWall();
        const g = getTerrainHeight(Math.max(0, wallX)), { mesh, light } = wallObj;
        mesh.visible = true;
        mesh.position.set(wallX - WALL_W / 2, -g - 40 + WALL_H / 2, 40);
        light.position.set(wallX + 60, -g + 250, 260);
        light.intensity = 1.5 + 0.4 * Math.sin(t * 13) * Math.sin(t * 7.3);
    }
    function ghostOf(o, col) {
        const tint = new T.Color(col);
        o.traverse((c) => {
            c.castShadow = c.receiveShadow = false;
            if (!c.material) return;
            const one = (m) => { m = m.clone(); m.transparent = true; m.opacity = 0.5; m.depthWrite = false;
                if (m.color) m.color.lerp(tint, 0.75);
                if (m.emissive) { m.emissive.copy(tint); m.emissiveIntensity = 0.5; }
                return m; };
            c.material = Array.isArray(c.material) ? c.material.map(one) : one(c.material);
            c.renderOrder = 5;
        });
        o.visible = false;
        return o;
    }
    function buildFigures(cfg, th) {
        const env = envFromSky(th);
        const monster = MAKERS[cfg.monsterType] || makeSlime;
        figs = {
            rob: template(makeRob, 30, env), robSh: shadowMesh(),
            mons: pool(template(monster, 25, env), true),
            mines: pool(template(makeMine, 20, env), true),
            ufos: pool(template(makeUfo, 40, env), true),
            jul: pool(template(makeJulian, 60, env), true),
            ghosts: { me: ghostOf(template(makeRob, 30, env), 0x5fd0ff), bot: ghostOf(template(makeRob, 30, env), 0xffd23c), friend: ghostOf(template(makeRob, 30, env), 0xff7ad9) },
        };
        // the other racers (mp.js), one colour per place in the room
        PEER_COLS.forEach((c, i) => { figs.ghosts['p' + i] = ghostOf(template(makeRob, 30, env), c); });
        world.add(figs.rob); world.add(figs.robSh);
        for (const k in figs.ghosts) world.add(figs.ghosts[k]);
        // the jumps Rob has left (two; a launch counts as one, landing gives them back):
        // shown above him from the first jump on, so a space that does nothing is explained
        figs.pips = [0, 1].map(() => { // a dot with a dark rim, readable on sky and snow alike
            const g = new T.SphereGeometry(1, 14, 10), opt = { transparent: true, depthTest: false, depthWrite: false };
            const m = new T.Mesh(g, new T.MeshBasicMaterial({ color: 0xffe14d, ...opt }));
            const rim = new T.Mesh(g, new T.MeshBasicMaterial({ color: 0x111111, opacity: 0.85, ...opt }));
            rim.scale.setScalar(1.3); rim.renderOrder = 10; m.renderOrder = 11; m.add(rim);
            m.visible = false; world.add(m); return m;
        });
        // make them now, not on the first frame
        obstacles.forEach((_, i) => figs.mons.get(i)); mines.forEach((_, i) => figs.mines.get(i));
        saucers.forEach((_, i) => figs.ufos.get(i)); julians.forEach((_, i) => figs.jul.get(i));
    }
    // the shadow of something whose lowest point is at 2D y `foot`, at x
    function placeShadow(sh, x, foot, w, vis) {
        const g = getTerrainHeight(x), h = Math.max(0, g - foot);
        const k = Math.max(0, 1 - h / 380);
        sh.visible = vis && k > 0 && !(SHADOW.on && SHADOW.dl);
        if (!sh.visible) return;
        const s = w * (1 + h / 500);
        sh.position.set(x, -g + 1, 0);
        sh.scale.set(s, 1, s * 0.55);
        sh.material.opacity = 0.75 * k;
    }
    function syncFigures(t) {
        const showRob = !gameOver || explosionParticles.length > 0;
        const R = rob.radius;
        figs.rob.visible = showRob;
        figs.rob.position.set(rob.x, -rob.y, 0);
        figs.rob.rotation.z = -rob.rotation;
        figs.rob.scale.setScalar(R);
        placeShadow(figs.robSh, rob.x, rob.y + R, R * 2.2, showRob);
        const used = rob.jumpsUsed, pips = showRob && used > 0 && gameStarted && !gameOver && !gameWon;
        figs.pips.forEach((m, i) => {
            m.visible = pips; if (!pips) return;
            const left = i < 2 - used;
            m.position.set(rob.x + (i - 0.5) * R * 0.75, -rob.y + R * 1.8, 0);
            m.scale.setScalar(R * (left ? 0.26 : 0.2));
            m.material.color.setHex(left ? 0xffe14d : 0x555555);
        });
        for (const k in figs.ghosts) figs.ghosts[k].visible = false;
        for (const g of ghostList) {
            const o = figs.ghosts[g.fig || g.kind]; if (!o) continue;
            o.visible = true; o.position.set(g.x, -g.y, -R * 0.2); o.rotation.z = -g.r; o.scale.setScalar(R);
        }
        syncWall(t);
        obstacles.forEach((obs, i) => {
            const { o, sh } = figs.mons.get(i), cx = obs.x + obs.width / 2, g = getTerrainHeight(cx);
            o.position.set(cx, -g + 2, 0);
            const b = Math.sin(t * 3 + i * 1.7); // breathing
            o.scale.set(25 * (1 - 0.025 * b), 25 * (1 + 0.035 * b), 25);
            placeShadow(sh, cx, g, obs.width * 1.3, true);
        });
        figs.mons.hideFrom(obstacles.length);
        mines.forEach((m, i) => {
            const { o, sh } = figs.mines.get(i);
            o.position.set(m.x, -m.y, 0);
            o.scale.setScalar(m.radius * (1 + 0.04 * Math.sin(Date.now() / 300 + m.pulse)));
            placeShadow(sh, m.x, m.y + m.radius, m.radius * 2.4, true);
        });
        figs.mines.hideFrom(mines.length);
        saucers.forEach((u, i) => {
            const { o, sh } = figs.ufos.get(i);
            o.position.set(u.x, -u.y, 0);
            o.rotation.z = Math.max(-0.35, Math.min(0.35, -(u.vx || 0) * 0.04));
            o.rotation.y = Math.sin(t * 0.8 + (u.phase || 0)) * 0.25;
            placeShadow(sh, u.x, u.y + 16, 90, true);
        });
        figs.ufos.hideFrom(saucers.length);
        julians.forEach((j, i) => {
            const { o, sh } = figs.jul.get(i), cx = j.x + j.width / 2, foot = j.y + j.height;
            o.position.set(cx, -foot, 0);
            placeShadow(sh, cx, foot, j.width * 1.4, true);
        });
        figs.jul.hideFrom(julians.length);
    }

    // ---------- fire: Julian's fireballs, the explosions and their light ----------
    // The game keeps its own lists (fireballs, explosionParticles) and calls
    // RR3D.boom() from createExplosion(); everything here only looks.
    // Billboards go in two batches of quads (one draw call each): hot things
    // added onto the picture, smoke laid over it. Up to NL point lights follow
    // the strongest fires and light the figures, the ground and the soil face.
    const NL = 4;
    const FIRE_U = {
        uFire: { value: Array.from({ length: NL }, () => new T.Vector4(0, 0, 0, 1)) },
        uFireC: { value: Array.from({ length: NL }, () => new T.Vector3()) },
    };
    const FIRE_GLSL = `uniform vec4 uFire[${NL}]; uniform vec3 uFireC[${NL}];
vec3 fireLight(vec3 w){ vec3 s=vec3(0.); for(int i=0;i<${NL};i++){
 float k=clamp(1.-length(uFire[i].xyz-w)/uFire[i].w,0.,1.); s+=uFireC[i]*k*k; } return s; }`;

    function batch(max, body, additive, order) {
        const n4 = max * 4;
        const P = new Float32Array(n4 * 3), Q = new Float32Array(n4 * 4), R = new Float32Array(n4), A = new Float32Array(n4 * 4);
        const idx = new Uint16Array(max * 6), C = [-1, -1, 1, -1, 1, 1, -1, 1];
        for (let i = 0; i < max; i++) { const v = i * 4; idx.set([v, v + 1, v + 2, v, v + 2, v + 3], i * 6); }
        for (let v = 0; v < n4; v++) { Q[v * 4] = C[(v % 4) * 2]; Q[v * 4 + 1] = C[(v % 4) * 2 + 1]; }
        const g = new T.BufferGeometry();
        const at = [['position', P, 3], ['aQ', Q, 4], ['aRot', R, 1], ['aP', A, 4]].map(([n, a, s]) => {
            const b = new T.BufferAttribute(a, s); b.setUsage(T.DynamicDrawUsage); g.setAttribute(n, b); return b; });
        g.setIndex(new T.BufferAttribute(idx, 1));
        const m = new T.ShaderMaterial({
            uniforms: { uTime: { value: 0 } }, transparent: true, depthWrite: false, fog: false,
            blending: additive ? T.AdditiveBlending : T.NormalBlending,
            vertexShader: `attribute vec4 aQ,aP; attribute float aRot; varying vec2 vUv; varying vec4 vP;
void main(){ vUv=aQ.xy*.5+.5; vP=aP; vec4 mv=viewMatrix*vec4(position,1.);
 float c=cos(aRot),s=sin(aRot); vec2 q=aQ.xy*aQ.zw; mv.xy+=vec2(c*q.x-s*q.y,s*q.x+c*q.y);
 gl_Position=projectionMatrix*mv; }`,
            fragmentShader: `uniform float uTime; varying vec2 vUv; varying vec4 vP; ${GLSL_NOISE}
float edge(float r){ return smoothstep(1.,.7,r); }
vec3 heat(float h){ return mix(mix(vec3(.45,.02,0.),vec3(1.,.32,.02),smoothstep(0.,.45,h)),vec3(1.,.9,.6),smoothstep(.6,1.,h)); }
void main(){ vec2 p=vUv*2.-1.; float r=length(p); vec4 outc=vec4(0.,0.,0.,1.);
${body}
 gl_FragColor=linearToOutputTexel(outc); }`,
        });
        timed.push(m);
        const mesh = new T.Mesh(g, m);
        mesh.frustumCulled = false; mesh.renderOrder = order;
        world.add(mesh);
        let n = 0;
        return {
            begin() { n = 0; },
            push(x, y, z, sx, sy, rot, a, b, c, d) {
                if (n >= max) return;
                for (let k = 0; k < 4; k++) {
                    const v = n * 4 + k;
                    P[v * 3] = x; P[v * 3 + 1] = y; P[v * 3 + 2] = z;
                    Q[v * 4 + 2] = sx; Q[v * 4 + 3] = sy; R[v] = rot;
                    A[v * 4] = a; A[v * 4 + 1] = b; A[v * 4 + 2] = c; A[v * 4 + 3] = d;
                }
                n++;
            },
            end() { g.setDrawRange(0, n * 6); for (const b of at) b.needsUpdate = true; },
        };
    }

    // vP = (life or strength, seed, kind, age)
    const HOT = `float life=vP.x, sd=vP.y, k=vP.z, age=vP.w;
 if(k<.5){ // a fireball: a turbulent ball of fire with a halo
   vec2 q=p*vec2(1.,1.25); float rq=length(q);
   float n=fbm(q*2.6+vec2(sd*7.+uTime*4.,sd*3.)), n2=noise(q*7.+vec2(uTime*6.,sd));
   float e=.42+.36*n*smoothstep(-.6,.6,p.x); // the flames trail back (the quad points backwards along +x)
   float body=smoothstep(e,e-.2,rq), h=(1.-rq/e)*.9+.25*n2;
   outc.rgb=(heat(clamp(h,0.,1.))*body*1.1+vec3(1.,.32,.04)*exp(-rq*rq*7.)*.3*edge(r))*life;
 } else if(k<1.5){ // an explosion: a ball that swells, churns and cools
   float n=fbm(p*1.7+vec2(sd*5.,sd*3.-age*1.6)), n2=noise(p*7.+sd*11.+age*3.);
   float e=.42+.38*n, body=smoothstep(e,e-.3,r);
   float h=(1.-r/e)*(1.-age)*1.15+n2*.22-age*.35;
   outc.rgb=heat(clamp(h,0.,1.))*body*(1.35*(1.-age)+.12)+vec3(1.,.45,.1)*exp(-r*r*4.)*edge(r)*pow(1.-age,2.)*.5;
 } else if(k<2.5){ // a spark or an ember's glow (age: 0 orange, 1 gold)
   vec3 c=mix(vec3(1.,.3,.03),vec3(1.,.8,.2),age);
   outc.rgb=mix(c,vec3(1.,.85,.5),life*life*.6)*exp(-r*r*5.)*edge(r)*1.5*life;
 } else if(k<3.5){ // the flash
   outc.rgb=vec3(1.,.72,.42)*exp(-r*r*4.5)*edge(r)*life*.55;
 } else if(k<4.5){ // the shock ring
   float d=abs(r-.82)+(noise(p*9.+sd)-.5)*.05; outc.rgb=vec3(1.,.62,.32)*smoothstep(.07,0.,d)*life*.3;
 } else if(k<5.5){ // a glint rising in the beam
   outc.rgb=vec3(.45,1.,1.)*exp(-r*r*5.)*edge(r)*1.3*life;
 } else if(k<6.5){ // a ring running up the beam (a flat quad: an ellipse)
   float d=abs(r-.86); outc.rgb=vec3(.2,1.,.95)*smoothstep(.12,0.,d)*life*.7;
 } else if(k<7.5){ // the glow under the saucer
   outc.rgb=vec3(.3,1.,1.)*exp(-r*r*3.)*edge(r)*life*.8;
 } else { // fireworks: hue in fract(sd), a flicker seed in floor(sd); age 1 = glitter
   vec3 c=hue(fract(sd)); c=c*c*1.5+.05;
   float g=1.;
   if(age>.5) g=step(.45,noise(vec2(uTime*28.,floor(sd)*7.3)))*1.6;
   if(k<8.5) outc.rgb=mix(c,vec3(1.),pow(life,4.)*.6)*exp(-r*r*5.)*edge(r)*2.8*life*mix(1.,g,smoothstep(.6,.2,life));
   else if(k<10.5) outc.rgb=mix(c,vec3(1.),.4)*exp(-r*r*4.)*edge(r)*life*.7;
   else { // a gem's twinkle: a four-pointed star (sd 1 cyan, 0 magenta)
     float st=exp(-abs(p.x)*16.)*exp(-abs(p.y)*2.2)+exp(-abs(p.y)*16.)*exp(-abs(p.x)*2.2)+exp(-r*r*18.);
     outc.rgb=mix(vec3(1.),mix(vec3(1.,.3,1.),vec3(.3,1.,1.),sd),.35)*st*edge(r)*life*2.;
   }
 }`;
    const SMOKE = `float life=vP.x, sd=vP.y, hot=vP.w;
 float n=fbm(p*1.5+vec2(sd*9.,sd*4.+uTime*.25));
 float a=smoothstep(1.,.25,r+(n-.5)*.7)*life;
 vec3 c=mix(vec3(.045,.035,.03),vec3(.24,.2,.17),n);
 c+=vec3(1.,.36,.06)*hot*hot*smoothstep(.85,0.,r)*2.;
 outc=vec4(c,a*.66);`;

    // the saucer's beam: an open cone, wide at the bottom, its radius set per frame
    function beamMesh() {
        const g = new T.CylinderGeometry(1, 1, 1, 48, 12, true);
        g.translate(0, -0.5, 0); // y from 0 (top) to -1
        const m = new T.ShaderMaterial({
            uniforms: { uTime: { value: 0 }, uTop: { value: 20 }, uBot: { value: 50 }, uLen: { value: 280 }, uA: { value: 0 } },
            transparent: true, depthWrite: false, side: T.DoubleSide, fog: false,
            vertexShader: `uniform float uTop,uBot,uLen; varying vec3 vL,vN,vV;
void main(){ vL=position; float r=mix(uTop,uBot,-position.y);
 vec4 w=modelMatrix*vec4(position.x*r,position.y*uLen,position.z*r,1.);
 vN=normalize(vec3(position.x,0.,position.z)); vV=normalize(cameraPosition-w.xyz);
 gl_Position=projectionMatrix*viewMatrix*w; }`,
            fragmentShader: `uniform float uTime,uA; varying vec3 vL,vN,vV; ${GLSL_NOISE}
void main(){ float k=-vL.y, ang=atan(vL.z,vL.x);
 float edge=pow(clamp(1.-abs(dot(normalize(vN),normalize(vV))),0.,1.),2.5);
 float bands=.5+.5*sin(k*34.+uTime*9.);               // rings that run up
 float sw=noise(vec2(ang*2.5+k*3.,k*5.-uTime*2.));    // a slow swirl
 float fade=smoothstep(0.,.08,k)*smoothstep(1.,.55,k);
 float I=(.1+.5*edge+.22*bands*bands*(.4+.6*sw))*fade*uA;
 // laid over the picture rather than added: it glows on a dark sky and
 // still tints a white one cyan
 gl_FragColor=linearToOutputTexel(vec4(vec3(.15,1.,.95)*1.5,clamp(I*.85,0.,.9))); }`,
        });
        timed.push(m);
        const mesh = new T.Mesh(g, m);
        mesh.frustumCulled = false; mesh.renderOrder = 5; mesh.visible = false;
        return mesh;
    }

    const CONF_MAX = 1100;
    let fire = null;
    function buildFire(th) {
        const lights = [];
        for (let i = 0; i < NL; i++) { const l = new T.PointLight(0xff7a2a, 0, 600, 2); world.add(l); lights.push(l); }
        const chunks = new T.InstancedMesh(new T.IcosahedronGeometry(1, 0),
            new T.MeshStandardMaterial({ color: lin(th.rock), roughness: 0.85, flatShading: true, emissive: lin('#ff5010'), emissiveIntensity: 0.35 }), 240);
        chunks.count = 0; chunks.frustumCulled = false;
        world.add(chunks);
        const scorch = [];
        for (let i = 0; i < 6; i++) { const s = shadowMesh(); s.visible = false; s.material.color = lin('#1a0c04'); world.add(s); scorch.push({ m: s, life: 0 }); }
        const beam = beamMesh();
        world.add(beam);
        // confetti: small cards that tumble, catch the light and come to rest on the ground
        const conf = new T.InstancedMesh(new T.PlaneGeometry(1, 1),
            new T.MeshStandardMaterial({ side: T.DoubleSide, roughness: 0.5, metalness: 0.1,
                                         envMap: envRT ? envRT.texture : null, envMapIntensity: 0.6 }), CONF_MAX);
        conf.setColorAt(0, new T.Color(1, 1, 1));
        conf.count = 0; conf.frustumCulled = false;
        world.add(conf);
        fire = {
            conf, cp: [], rockets: [], bursts: [],
            lights, chunks, scorch, sc: 0, beam, beamA: 0, beamS: null, beamP: 0,
            smoke: batch(700, SMOKE, false, 4),
            hot: batch(1400, HOT, true, 5),
            parts: [],   // own particles: sparks, embers, smoke
            booms: [],   // own explosion records
        };
    }
    const DUMMY = new T.Object3D();
    function addPart(o) { if (fire.parts.length < 1800) fire.parts.push(o); }
    function boom(x, y) {
        if (!fire) return;
        fire.booms.push({ x, y: -y, age: 0, seed: rnd() * 10, size: rr(110, 130) });
        shake(0.55 * Math.max(0.2, 1 - Math.abs(x - (cameraX + 960)) / 1400));
        for (let i = 0; i < 46; i++) { // sparks in every direction, the camera's too
            const a = rnd() * Math.PI * 2, e = rr(-0.6, 0.9), s = rr(350, 1100);
            addPart({ k: 2, x, y: -y, z: 40, vx: Math.cos(a) * Math.cos(e) * s, vy: Math.sin(a) * Math.cos(e) * s + 150, vz: Math.sin(e) * s,
                      life: 1, decay: rr(0.9, 1.8), size: rr(5, 9), g: 900, drag: 1.6, tint: rnd() });
        }
        for (let i = 0; i < 14; i++) // smoke that rolls up after the fire
            addPart({ k: 9, x: x + rr(-50, 50), y: -y + rr(-30, 40), z: rr(-20, 70), vx: rr(-60, 60), vy: rr(40, 140), vz: rr(-20, 30),
                      life: 1, decay: rr(0.28, 0.45), size: rr(36, 60), grow: rr(28, 50), g: -40, drag: 1.2, delay: rr(0.08, 0.35), hot: 1, rot: rnd() * 6, seed: rnd() * 10 });
        const gy = getTerrainHeight(x);
        if (gy - y < 90) { // a scorch mark where it went off on the ground
            const s = fire.scorch[fire.sc++ % fire.scorch.length];
            s.life = 1; s.m.visible = true;
            s.m.position.set(x, -gy + 1.5, 0); s.m.scale.set(170, 1, 95); s.m.rotation.y = rr(-0.2, 0.2);
        }
    }
    // the finish: a confetti cannon on both sides of Rob
    function confettiBurst(x, y) {
        if (!fire) return;
        const gy = -getTerrainHeight(x), c = new T.Color();
        for (const side of [-1, 1]) for (let i = 0; i < 380; i++) {
            if (fire.cp.length >= CONF_MAX - 120) break;
            const a = Math.PI / 2 - side * rr(0.05, 0.6), sp = rr(1300, 2500);
            c.setHSL(rnd(), 1, rr(0.5, 0.62));
            fire.cp.push({ x: x + side * 130 + rr(-10, 10), y: gy + 10, z: rr(-30, 40),
                vx: Math.cos(a) * sp, vy: Math.sin(a) * sp, vz: rr(-1400, 250),
                rx: rnd() * 6, ry: rnd() * 6, rz: rnd() * 6, wx: rr(-9, 9), wy: rr(-7, 7), wz: rr(-4, 4),
                f: rr(2, 4), ph: rnd() * 6, s: rr(7, 12), col: c.clone(), rest: false, life: 1 });
        }
        for (const side of [-1, 1]) // a puff from each cannon
            for (let i = 0; i < 6; i++) addPart({ k: 9, x: x + side * 130, y: gy + 20, z: 0, vx: rr(-60, 60), vy: rr(80, 220), vz: 0,
                life: 0.6, decay: 0.6, size: 30, grow: 60, g: -20, drag: 1.5, hot: 0, rot: rnd() * 6, seed: rnd() * 10 });
    }
    // a firework the game sets off at (x, y): a rocket goes up behind the line
    // and bursts at depth, where it covers the same spot on the screen
    function firework(x, y) {
        // fewer, cleaner bursts than the game asks for (3 a second)
        if (!fire || fire.rockets.length > 6 || last - (fire.lastRocket || -9) < 0.55) return;
        fire.lastRocket = last;
        const zb = rr(-600, -200), k = (D - zb) / D;   // in front of the skyline and the mesas          // farther, so bigger to look the same
        // the game picks y in 0..540 as if the camera never rose or sank: read
        // it as the top half of the screen, so a high camera still sees them
        y += cameraY;
        const cx = cameraX + 960, cy = -(cameraY + 540);
        const bx = cx + (x - cx) * k, by = cy + (-y - cy) * k;
        const gy = -surfY(bx, zb);
        if (window.RRSfx) RRSfx.firework(bx);
        fire.rockets.push({ x: bx + rr(-40, 40) * k, y0: gy, x1: bx, y1: Math.max(gy + 200, by), z: zb, k, t: 0, T: rr(0.7, 1.1), hue: rnd() });
    }
    function burst(r) {
        const n = 170, h = r.hue, glit = rnd() < 0.4 ? 1 : 0, sp = rr(480, 620) * r.k;
        for (let i = 0; i < n; i++) {
            const u = rnd() * 2 - 1, a = rnd() * Math.PI * 2, q = Math.sqrt(1 - u * u), v = sp * rr(0.85, 1);
            const hh = rnd() < 0.15 ? (h + 0.5) % 1 : h; // a few of the other colour
            addPart({ k: 8, x: r.x1, y: r.y1, z: r.z, vx: q * Math.cos(a) * v, vy: q * Math.sin(a) * v + 40 * r.k, vz: u * v,
                life: 1, decay: rr(0.5, 0.65), size: rr(5, 8) * r.k, g: 140 * r.k, drag: 1.5, hue: hh + Math.floor(rnd() * 60), glit, sk: r.k });
        }
        addPart({ k: 10, x: r.x1, y: r.y1, z: r.z, vx: 0, vy: 0, vz: 0, life: 1, decay: 3.5, size: 220 * r.k, g: 0, drag: 0, hue: h });
        const c = new T.Color().setHSL(h, 1, 0.6);
        fire.bursts.push({ x: r.x1, y: r.y1, z: r.z, age: 0, c: [c.r, c.g, c.b] });
        if (window.RRSfx) RRSfx.burst(r.x1);
    }
    function syncFire(t, dt) {
        const F = fire, hot = F.hot, sm = F.smoke, src = [];
        hot.begin(); sm.begin();
        const f60 = dt * 60;
        // Julian's fireballs, with a trail of smoke and embers
        for (const fb of fireballs) {
            const g = getTerrainHeight(fb.x), under = fb.y - g;
            if (under > 0 && !fb._hit) { // it reaches the ground: a puff and some sparks
                fb._hit = true;
                for (let i = 0; i < 10; i++) addPart({ k: 2, x: fb.x, y: -g, z: rr(0, 60), vx: rr(-300, 300), vy: rr(150, 500), vz: rr(-150, 250), life: 1, decay: rr(1.5, 2.5), size: rr(4, 7), g: 900, drag: 1.5, tint: rnd() });
                for (let i = 0; i < 3; i++) addPart({ k: 9, x: fb.x + rr(-15, 15), y: -g + 10, z: 30, vx: rr(-30, 30), vy: rr(40, 90), vz: 0, life: 0.8, decay: 0.5, size: 22, grow: 40, g: -20, drag: 1, hot: 0.6, rot: rnd() * 6, seed: rnd() * 10 });
            }
            const vis = fb.life * Math.max(0, Math.min(1, 1 - under / 22));
            if (vis <= 0) continue;
            if (fb._s === undefined) fb._s = rnd() * 10;
            const sp = Math.hypot(fb.vx, fb.vy), ang = Math.atan2(-fb.vy, fb.vx);
            hot.push(fb.x, -fb.y, 40, fb.size * 2.4 + sp * 0.6, fb.size * 2.4, ang + Math.PI, vis, fb._s, 0, 0);
            hot.push(fb.x, -fb.y, 39, fb.size * 5, fb.size * 5, 0, vis * 0.3, 0, 3, 0);
            if (rnd() < 0.9 * f60) addPart({ k: 9, x: fb.x - fb.vx + rr(-4, 4), y: -fb.y + fb.vy + rr(-4, 4), z: rr(25, 45), vx: rr(-20, 20), vy: rr(30, 70), vz: 0,
                                             life: 0.7 * vis, decay: rr(0.7, 1.1), size: fb.size * 0.8, grow: 30, g: -30, drag: 1, hot: 0.9, rot: rnd() * 6, seed: rnd() * 10 });
            if (rnd() < 0.7 * f60) addPart({ k: 2, x: fb.x + rr(-6, 6), y: -fb.y + rr(-6, 6), z: rr(20, 60), vx: -fb.vx * 10 + rr(-60, 60), vy: fb.vy * 10 + rr(-40, 90), vz: rr(-40, 40),
                                             life: vis, decay: rr(1.4, 2.4), size: rr(3, 5), g: -60, drag: 2, tint: rnd() });
            src.push({ x: fb.x, y: -fb.y, s: 0.9 * vis * (0.85 + 0.15 * Math.sin(t * 23 + fb._s * 5)), R: 480, c: [1, 0.45, 0.12] });
        }
        // the explosions
        for (let i = F.booms.length - 1; i >= 0; i--) {
            const b = F.booms[i];
            b.age += dt / 1.15;
            if (b.age >= 1) { F.booms.splice(i, 1); continue; }
            const a = b.age, grow = 1 - Math.pow(1 - Math.min(1, a * 1.6), 3);
            const s = b.size * (0.35 + 0.85 * grow);
            hot.push(b.x, b.y + a * 60, 55, s, s, b.seed, 1, b.seed, 1, a);
            if (a < 0.22) hot.push(b.x, b.y, 54, 380 * (0.6 + a * 2), 380 * (0.6 + a * 2), 0, 1 - a / 0.22, 0, 3, 0);
            if (a < 0.4) { const rs = 60 + a * 650; hot.push(b.x, b.y, 50, rs, rs, b.seed, 1 - a / 0.4, b.seed, 4, 0); }
            const k = Math.exp(-a * 4.5);
            src.push({ x: b.x, y: b.y + 20, s: 2.6 * k, R: 800, c: [1, 0.45 + 0.35 * k, 0.15 + 0.3 * k] });
        }
        // the game's own explosion particles as glowing chunks
        let ci = 0;
        for (const p of explosionParticles) {
            if (p._z === undefined) { p._z = rr(-30, 90); p._vz = rr(-3, 6); p._r = rnd() * 6; p._w = rr(-0.3, 0.3); p._s = rr(0.6, 1.4); }
            p._z += p._vz * f60; p._r += p._w * f60;
            if (ci < 240) {
                DUMMY.position.set(p.x, -p.y, p._z);
                DUMMY.rotation.set(p._r, p._r * 0.7, p._r * 1.3);
                DUMMY.scale.setScalar(p.size * 1.3 * p._s * (0.4 + 0.6 * p.life));
                DUMMY.updateMatrix();
                F.chunks.setMatrixAt(ci++, DUMMY.matrix);
            }
            hot.push(p.x, -p.y, p._z + 8, p.size * 3.2 * p.life, p.size * 3.2 * p.life, 0, p.life, 0, 2, p.color === '#FFD700' ? 1 : 0);
        }
        F.chunks.count = ci;
        F.chunks.instanceMatrix.needsUpdate = true;
        // fireworks: rockets on their way up, then the light of each burst
        for (let i = F.rockets.length - 1; i >= 0; i--) {
            const r = F.rockets[i];
            r.t += dt;
            const e = Math.min(1, r.t / r.T), ee = 1 - (1 - e) * (1 - e);
            const x = r.x + (r.x1 - r.x) * ee, y = r.y0 + (r.y1 - r.y0) * ee;
            hot.push(x, y, r.z, 6 * r.k, 6 * r.k, 0, 1, 0, 2, 1);
            if (rnd() < 2 * f60) addPart({ k: 2, x: x + rr(-2, 2) * r.k, y, z: r.z, vx: rr(-20, 20) * r.k, vy: rr(-60, 0) * r.k, vz: 0,
                life: 0.8, decay: rr(1.6, 2.6), size: rr(2.5, 4) * r.k, g: 60 * r.k, drag: 1, tint: 0.8 });
            if (e >= 1) { burst(r); F.rockets.splice(i, 1); }
        }
        for (let i = F.bursts.length - 1; i >= 0; i--) {
            const b = F.bursts[i];
            b.age += dt;
            if (b.age > 0.9) { F.bursts.splice(i, 1); continue; }
            src.push({ x: b.x, y: b.y, z: b.z, s: 1.6 * Math.exp(-b.age * 4), R: 1800, c: b.c });
        }
        // confetti: the game's own pieces, and the cannons'
        {
            const im = F.conf;
            let n = 0;
            const put = (x, y, z, rx, ry, rz, sz, col) => {
                if (n >= CONF_MAX) return;
                DUMMY.position.set(x, y, z); DUMMY.rotation.set(rx, ry, rz); DUMMY.scale.set(sz, sz * 1.6, sz);
                DUMMY.updateMatrix(); im.setMatrixAt(n, DUMMY.matrix); im.setColorAt(n, col); n++;
            };
            for (const c of confetti) {
                if (!c._c) { c._c = new T.Color().setStyle(c.color); c._z = rr(-80, 160); c._rx = rnd() * 6; c._ry = rnd() * 6; c._w = rr(0.1, 0.3); }
                c._rx += c._w * f60; c._ry += c._w * 0.7 * f60;
                put(c.x, -c.y, c._z, c._rx, c._ry, c.rotation, c.size * 1.6, c._c);
            }
            const dr = Math.exp(-2.3 * dt), bottom = -(cameraY + 1400);
            for (let i = F.cp.length - 1; i >= 0; i--) {
                const q = F.cp[i];
                if (q.rest) {
                    q.life -= dt / 10;
                    if (q.life <= 0) { F.cp.splice(i, 1); continue; }
                } else {
                    q.vx = q.vx * dr + Math.sin(t * q.f + q.ph) * 300 * dt; q.vy = q.vy * dr - 480 * dt; q.vz *= dr;
                    q.x += q.vx * dt; q.y += q.vy * dt; q.z += q.vz * dt;
                    q.rx += q.wx * dt; q.ry += q.wy * dt; q.rz += q.wz * dt;
                    if (q.vy < 0 && q.z < -4) { // it lands on the ground behind the line
                        const gy = -surfY(q.x, q.z);
                        if (q.y <= gy + 1) { q.rest = true; q.y = gy + 1; q.rx = -Math.PI / 2 + rr(-0.3, 0.3); q.ry = rr(-0.3, 0.3); }
                    }
                    if (q.y < bottom) { F.cp.splice(i, 1); continue; }
                }
                put(q.x, q.y, q.z, q.rx, q.ry, q.rz, q.s * Math.min(1, q.life * 4), q.col);
            }
            im.count = n;
            im.instanceMatrix.needsUpdate = true;
            if (im.instanceColor) im.instanceColor.needsUpdate = true;
        }
        // the saucer's beam (the game still decides who is in it), fading in and out
        const on = !!(beamActive && beamTarget);
        if (on) { F.beamS = beamTarget; F.beamP = beamProgress; }
        F.beamA += ((on ? 1 : 0) - F.beamA) * Math.min(1, dt * (on ? 9 : 5));
        F.beam.visible = F.beamA > 0.01 && !!F.beamS;
        if (F.beam.visible) {
            const u = F.beamS, A = F.beamA, top = u.y + 20, bot = 50 + F.beamP * 0.5, L = 280, U = F.beam.material.uniforms;
            F.beam.position.set(u.x, -top, 0);
            U.uTop.value = 20; U.uBot.value = bot; U.uLen.value = L;
            U.uA.value = A * (0.88 + 0.12 * Math.sin(t * 31));
            const rad = (k) => 20 + (bot - 20) * k;
            for (let i = 0; i < 4; i++) { // rings running up
                const k = 1 - ((i / 4 + t * 0.7) % 1), f = Math.min(1, k / 0.1) * Math.min(1, (1 - k) / 0.35);
                hot.push(u.x, -(top + k * L), 6, rad(k), rad(k) * 0.16, 0, f * A, 0, 6, 0);
            }
            hot.push(u.x, -(u.y + 16), 30, 80, 34, 0, A, 0, 7, 0);
            if (rnd() < 1.4 * f60) {
                const k = rr(0.15, 1), r = rad(k) * 0.85;
                addPart({ k: 5, x: u.x + rr(-r, r), y: -(top + k * L), z: rr(-r, r) * 0.6, vx: 0, vy: rr(90, 200), vz: 0,
                          life: 1, decay: rr(1.2, 2), size: rr(3, 6), g: 0, drag: 0 });
            }
            src.push({ x: u.x, y: -(top + 190), s: 1.5 * A, R: 430, c: [0.25, 1, 1] });
        }
        // own particles
        for (let i = F.parts.length - 1; i >= 0; i--) {
            const q = F.parts[i];
            if (q.delay > 0) { q.delay -= dt; continue; }
            q.life -= q.decay * dt;
            if (q.life <= 0) { F.parts.splice(i, 1); continue; }
            const dr = Math.exp(-q.drag * dt);
            q.vx *= dr; q.vy = q.vy * dr - q.g * dt; q.vz *= dr;
            q.x += q.vx * dt; q.y += q.vy * dt; q.z += q.vz * dt;
            if (q.k === 2) {
                const sp = Math.hypot(q.vx, q.vy);
                hot.push(q.x, q.y, q.z, q.size + sp * 0.018, q.size, Math.atan2(q.vy, q.vx), q.life, 0, 2, q.tint);
            } else if (q.k === 8) {
                // a streak: the head at the particle, the tail behind it
                const sp = Math.hypot(q.vx, q.vy) + 1e-3, len = Math.min(70 * q.sk, sp * 0.1);
                hot.push(q.x - q.vx / sp * len * 0.5, q.y - q.vy / sp * len * 0.5, q.z, q.size + len * 0.5, q.size, Math.atan2(q.vy, q.vx), q.life, q.hue, 8, q.glit);
            } else if (q.k === 10) {
                hot.push(q.x, q.y, q.z, q.size, q.size, 0, q.life, q.hue, 10, 0);
            } else if (q.k === 5) {
                hot.push(q.x, q.y, q.z, q.size * 0.6, q.size * 1.6, 0, q.life * F.beamA, 0, 5, 0);
            } else {
                q.size += q.grow * dt; q.hot = Math.max(0, q.hot - dt * 2.2);
                sm.push(q.x, q.y, q.z, q.size, q.size, q.rot + t * 0.2, Math.min(1, q.life * 1.6), q.seed, 9, q.hot);
            }
        }
        if (soil) syncSoil(t, dt, hot);
        hot.end(); sm.end();
        // scorch marks fade away
        for (const s of F.scorch) if (s.life > 0) { s.life -= dt / 9; s.m.visible = s.life > 0; s.m.material.opacity = 0.7 * Math.min(1, s.life * 2); }
        // the lights go to the strongest fires
        src.sort((a, b) => b.s - a.s);
        for (let i = 0; i < NL; i++) {
            const l = F.lights[i], s = src[i], u = FIRE_U.uFire.value[i], uc = FIRE_U.uFireC.value[i];
            if (s) {
                l.position.set(s.x, s.y, s.z ?? 70); l.intensity = s.s; l.distance = s.R; l.color.setRGB(...s.c);
                u.set(s.x, s.y, s.z ?? 70, s.R); uc.set(s.c[0] * s.s, s.c[1] * s.s, s.c[2] * s.s);
            } else { l.intensity = 0; uc.set(0, 0, 0); }
        }
    }

    // ---------- the course: the finish, the checkpoints, Julian's sign, the power meter ----------
    // Cloth that waves: three's own lit material, its vertices moved in the
    // vertex shader. `x0`..`x0 + W` is the cloth's width along x; `pinned`:
    // held on the left only (a flag) instead of at both ends (a banner).
    function waving(mat, x0, W, amp, freq, speed, pinned) {
        const U = { value: 0 };
        timed.push({ uniforms: { uTime: U } });
        const f = (v) => v.toFixed(4);
        mat.onBeforeCompile = (sh) => {
            sh.uniforms.uTime = U;
            sh.vertexShader = 'uniform float uTime;\n' + sh.vertexShader.replace('#include <begin_vertex>', `#include <begin_vertex>
 float wu=clamp((position.x-(${f(x0)}))/${f(W)},0.,1.);
 float wa=${pinned ? 'wu' : 'sin(3.14159*wu)'};
 transformed.z+=wa*${f(amp)}*sin(wu*${f(freq)}-uTime*${f(speed)}+position.y*.04);`);
        };
        mat.customProgramCacheKey = () => ['wave', x0, W, amp, freq, speed, pinned].join(',');
        return mat;
    }
    function metal(hex, env) {
        return new T.MeshStandardMaterial({ color: lin(hex), metalness: 0.85, roughness: 0.28, envMap: env, envMapIntensity: 1 });
    }

    let course = null;
    function buildCourse(th) {
        const env = envRT ? envRT.texture : null, aniso = renderer.capabilities.getMaxAnisotropy();
        const C = { cps: [], plates: [], meter: null, fx: finishLine };
        const group = new T.Group();
        world.add(group);

        // the finish: a checkered banner between two gold posts, just behind the line
        const fx = finishLine, fz = -70, bw = 250, bh = 78, top = 235;
        const banner = canvasTex(1024, 320, (g, w, h) => {
            const q = 40;
            for (let y = 0; y < h; y += q) for (let x = 0; x < w; x += q) {
                g.fillStyle = ((x + y) / q) % 2 ? '#111' : '#f4f4f4'; g.fillRect(x, y, q, q);
            }
            g.fillStyle = 'rgba(20,10,0,0.82)'; g.fillRect(0, 70, w, h - 140);
            g.fillStyle = '#FFD700'; g.fillRect(0, 70, w, 8); g.fillRect(0, h - 78, w, 8);
            g.font = 'bold 150px Arial'; g.textAlign = 'center'; g.textBaseline = 'middle';
            g.lineWidth = 10; g.strokeStyle = '#000'; g.strokeText('FINISH', w / 2, h / 2 + 6);
            const gr = g.createLinearGradient(0, 90, 0, h - 90);
            gr.addColorStop(0, '#fff2a0'); gr.addColorStop(0.5, '#FFD700'); gr.addColorStop(1, '#c89000');
            g.fillStyle = gr; g.fillText('FINISH', w / 2, h / 2 + 6);
        });
        banner.anisotropy = aniso;
        const bmat = waving(new T.MeshStandardMaterial({ map: banner, side: T.DoubleSide, roughness: 0.75,
            emissive: lin('#ffffff'), emissiveMap: banner, emissiveIntensity: 0.18 }), -bw / 2, bw, 7, 9, 4.2, false);
        const bm = new T.Mesh(new T.PlaneGeometry(bw, bh, 32, 6), bmat);
        const gy = (x, z) => -surfY(x, z);
        const baseY = Math.max(gy(fx - bw / 2 - 8, fz), gy(fx + bw / 2 + 8, fz));
        bm.position.set(fx, baseY + top - bh / 2, fz);
        group.add(bm);
        const gold = metal('#FFD700', env);
        for (const s of [-1, 1]) {
            const x = fx + s * (bw / 2 + 8), y0 = gy(x, fz) - 20, H = baseY + top + 18 - y0;
            const post = new T.Mesh(new T.CylinderGeometry(6, 7.5, H, 20), gold);
            post.position.set(x, y0 + H / 2, fz); group.add(post);
            const ball = new T.Mesh(new T.SphereGeometry(12, 24, 16), new T.MeshStandardMaterial({ color: lin('#ffe680'),
                emissive: lin('#ffcc33'), emissiveIntensity: 1.6, roughness: 0.3 }));
            ball.position.set(x, y0 + H + 10, fz); group.add(ball);
        }
        // a checkered strip on the ground, from the line far back
        {
            const zs = [], xs = [-22, 22], pos = [], uv = [], idx = [];
            for (let z = 0; z >= -900; z -= 30) zs.push(z);
            zs.forEach((z, j) => xs.forEach((dx, i) => { pos.push(fx + dx, gy(fx + dx, z) + 1.5, z); uv.push(i, -z / 44); }));
            for (let j = 0; j < zs.length - 1; j++) { const a = j * 2; idx.push(a, a + 2, a + 1, a + 1, a + 2, a + 3); }
            const g = new T.BufferGeometry();
            g.setAttribute('position', new T.Float32BufferAttribute(pos, 3));
            g.setAttribute('uv', new T.Float32BufferAttribute(uv, 2));
            g.setIndex(idx); g.computeVertexNormals();
            const ck = canvasTex(64, 64, (c) => { c.fillStyle = '#f4f4f4'; c.fillRect(0, 0, 64, 64); c.fillStyle = '#111'; c.fillRect(0, 0, 32, 32); c.fillRect(32, 32, 32, 32); });
            ck.wrapS = ck.wrapT = T.RepeatWrapping; ck.repeat.set(1, 1); ck.anisotropy = aniso;
            ck.magFilter = T.NearestFilter;
            const strip = new T.Mesh(g, new T.MeshStandardMaterial({ map: ck, roughness: 0.8, side: T.DoubleSide,
                polygonOffset: true, polygonOffsetFactor: -3, polygonOffsetUnits: -3 }));
            group.add(strip);
        }

        // the checkpoints (level 10): a pole with a flag that turns from red to green
        const flagTex = (n, on) => canvasTex(160, 120, (g, w, h) => {
            g.beginPath(); g.moveTo(0, 0); g.lineTo(w, h / 2); g.lineTo(0, h); g.closePath();
            g.fillStyle = on ? '#1fd655' : '#e8282b'; g.fill();
            g.lineWidth = 6; g.strokeStyle = on ? '#0a7a2c' : '#7a0d0f'; g.stroke();
            g.font = 'bold 30px Arial'; g.fillStyle = '#fff'; g.textBaseline = 'middle';
            g.fillText('CP' + n, 12, h / 2);
        });
        const pole = metal('#e8e8f0', env);
        checkpoints.forEach((cp, i) => {
            const x = cp.x + 4, z = -14, foot = gy(x, z) - 6, H = cp.height + 6;
            const p = new T.Mesh(new T.CylinderGeometry(3.5, 4.5, H, 14), pole);
            p.position.set(x, foot + H / 2, z); group.add(p);
            const off = flagTex(i + 1, false), on = flagTex(i + 1, true);
            const fm = waving(new T.MeshStandardMaterial({ map: off, alphaTest: 0.5, side: T.DoubleSide, roughness: 0.7,
                emissive: lin('#ffffff'), emissiveMap: off, emissiveIntensity: 0.12 }), 0, 64, 6, 7, 6, true);
            const fg = new T.PlaneGeometry(64, 48, 14, 4); fg.translate(32, -24, 0);
            const f = new T.Mesh(fg, fm);
            f.position.set(x + 3, foot + H - 2, z); group.add(f);
            const ball = new T.Mesh(new T.SphereGeometry(7, 20, 14), new T.MeshStandardMaterial({ color: lin('#ff4040'),
                emissive: lin('#ff2020'), emissiveIntensity: 1.4, roughness: 0.3 }));
            ball.position.set(x, foot + H + 6, z); group.add(ball);
            C.cps.push({ cp, fm, ball, off, on, was: false, x, y: foot + H });
        });

        // Julian's sign: a plate with his name that jumps with him
        if (julians.length) {
            const tex = canvasTex(320, 96, (g, w, h) => {
                const gr = g.createLinearGradient(0, 0, 0, h); gr.addColorStop(0, '#7a1410'); gr.addColorStop(1, '#3a0806');
                g.fillStyle = gr; g.fillRect(0, 0, w, h);
                g.strokeStyle = '#FFD700'; g.lineWidth = 8; g.strokeRect(6, 6, w - 12, h - 12);
                g.font = 'bold 58px Arial'; g.textAlign = 'center'; g.textBaseline = 'middle';
                g.lineWidth = 6; g.strokeStyle = '#000'; g.strokeText('JULIAN', w / 2, h / 2 + 3);
                g.fillStyle = '#FFD700'; g.fillText('JULIAN', w / 2, h / 2 + 3);
            });
            tex.anisotropy = aniso;
            const side = new T.MeshStandardMaterial({ color: lin('#5a0f0c'), roughness: 0.5, metalness: 0.3, envMap: env });
            const face = new T.MeshStandardMaterial({ map: tex, roughness: 0.45, metalness: 0.2, envMap: env, envMapIntensity: 0.5,
                emissive: lin('#ffffff'), emissiveMap: tex, emissiveIntensity: 0.35 });
            const geo = new RoundedBoxGeometry(104, 32, 8, 2, 4);
            { // the face's picture spread over the whole face (the box's own uvs crowd it into the middle)
                const P = geo.attributes.position, UV = geo.attributes.uv;
                for (let i = 0; i < P.count; i++) UV.setXY(i, P.getX(i) / 104 + 0.5, P.getY(i) / 32 + 0.5);
            }
            julians.forEach(() => {
                const o = new T.Mesh(geo, [side, side, side, side, face, face]);
                group.add(o); C.plates.push(o);
            });
        }

        // the jump power meter: a ring round Rob that fills up while the mouse is held
        const mm = new T.ShaderMaterial({
            uniforms: { uP: { value: 0 }, uA: { value: 0 }, uTime: { value: 0 } },
            transparent: true, depthWrite: false, depthTest: false, fog: false,
            vertexShader: 'varying vec2 vP; void main(){ vP=position.xy; gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.); }',
            fragmentShader: `uniform float uP,uA,uTime; varying vec2 vP;
vec3 hsv(float h){ return clamp(abs(mod(h*6.+vec3(0.,4.,2.),6.)-3.)-1.,0.,1.); }
void main(){ float r=length(vP), a=fract(atan(vP.x,vP.y)/6.2831853+1.); // 0 at the top, clockwise
 float band=smoothstep(52.,54.,r)*smoothstep(68.,66.,r);
 float tick=step(.985,fract(a*10.))*band;
 vec3 col=hsv(uP/3.)*1.35;                         // red to green, as in the original
 float fill=step(a,uP)*band;
 float full=step(.999,uP)*(.5+.5*sin(uTime*14.));
 vec3 c=mix(vec3(.04),col*(1.+full*.8),fill)+vec3(1.,.84,0.)*tick*.8;
 float halo=smoothstep(80.,60.,r)*smoothstep(46.,58.,r)*(1.-band)*uP*.35;
 c+=col*halo;
 float al=max(max(band*(.55+.45*fill),tick),halo)*uA;
 gl_FragColor=linearToOutputTexel(vec4(c,al)); }`,
        });
        timed.push(mm);
        const meter = new T.Mesh(new T.RingGeometry(46, 82, 72, 1), mm);
        meter.renderOrder = 8; meter.frustumCulled = false; meter.visible = false;
        group.add(meter);
        C.meter = meter;
        course = C;
    }

    function syncCourse(t, dt) {
        const C = course;
        // checkpoints: the flag turns green with a shower of green sparks
        for (const k of C.cps) {
            if (k.cp.activated && !k.was) {
                k.was = true;
                k.fm.map = k.on; k.fm.emissiveMap = k.on; k.fm.needsUpdate = true;
                k.ball.material.color.copy(lin('#40ff70')); k.ball.material.emissive.copy(lin('#20ff50'));
                if (fire) for (let i = 0; i < 60; i++) {
                    const a = rnd() * Math.PI * 2, e = rr(-0.3, 1), s = rr(200, 600);
                    addPart({ k: 8, x: k.x, y: k.y, z: -14, vx: Math.cos(a) * Math.cos(e) * s, vy: Math.sin(e) * s + 200, vz: Math.sin(a) * Math.cos(e) * s,
                        life: 1, decay: rr(0.7, 1.1), size: rr(4, 7), g: 500, drag: 1.4, hue: 0.67 + Math.floor(rnd() * 60), glit: 0, sk: 1 });
                }
            } else if (!k.cp.activated && k.was) { // the level started again
                k.was = false;
                k.fm.map = k.off; k.fm.emissiveMap = k.off; k.fm.needsUpdate = true;
                k.ball.material.color.copy(lin('#ff4040')); k.ball.material.emissive.copy(lin('#ff2020'));
            }
            if (k.was) k.ball.material.emissiveIntensity = 1.4 + 0.6 * Math.sin(t * 5);
        }
        // Julian's sign floats over his head and sways a little as he lands
        julians.forEach((j, i) => {
            const o = C.plates[i]; if (!o) return;
            o.position.set(j.x + j.width / 2, -(j.y - 42), 10);
            o.rotation.z = Math.sin(t * 2.1 + i) * 0.05 - (j.vy || 0) * 0.008;
            o.rotation.y = Math.sin(t * 1.3 + i * 2) * 0.12;
        });
        // gold glints rising at the finish while it is in view
        if (fire && Math.abs(C.fx - (cameraX + 960)) < 1300 && rnd() < 0.35 * dt * 60)
            addPart({ k: 2, x: C.fx + rr(-125, 125), y: -getTerrainHeight(C.fx) + rr(0, 60), z: rr(-90, -40), vx: rr(-10, 10), vy: rr(50, 120), vz: 0,
                life: 1, decay: rr(0.5, 0.9), size: rr(3, 5), g: -10, drag: 0.5, tint: 1 });
        // the power meter
        const m = C.meter, U = m.material.uniforms, on = isMouseDown;
        U.uA.value += ((on ? 1 : 0) - U.uA.value) * Math.min(1, dt * (on ? 14 : 6));
        if (on) U.uP.value = Math.min(mouseDownTime / 30, 1);
        m.visible = U.uA.value > 0.01;
        if (m.visible) m.position.set(rob.x, -rob.y, 45);
    }

    // ---------- the soil: what the original draws in the ground, as things in the soil face ----------
    // Same places as drawMountain(): along the line's points, every 5th a pair
    // of stones, every 7th a gold vein with nuggets, every 9th two gems, every
    // 11th a mole in its burrow, every 6th a second gold vein, every 8th silver.
    // Static pieces are merged per material (one draw each); the moles move.
    function moleGeo() {
        const brown = lin('#654321'), light = lin('#8a6a48'), pink = lin('#ffb6c1'), black = lin('#111111'), paw = lin('#8b4513');
        const S = (r, w = 16, h = 12) => new T.SphereGeometry(r, w, h);
        const parts = [
            { geo: S(10), m: new T.Matrix4().compose(new T.Vector3(0, 0, 0), new T.Quaternion(), new T.Vector3(1, 0.92, 0.95)), color: brown },
            { geo: S(4.8), m: M4(0, -3, 7.5), color: light },
            { geo: S(2.6, 10, 8), m: M4(0, -2.5, 12), color: pink },
            { geo: S(1.6, 8, 6), m: M4(-4.5, 2.2, 8.6), color: black },
            { geo: S(1.6, 8, 6), m: M4(4.5, 2.2, 8.6), color: black },
            { geo: S(3.6, 10, 8), m: new T.Matrix4().compose(new T.Vector3(-9, -9, 5), new T.Quaternion(), new T.Vector3(1.2, 0.6, 1)), color: paw },
            { geo: S(3.6, 10, 8), m: new T.Matrix4().compose(new T.Vector3(9, -9, 5), new T.Quaternion(), new T.Vector3(1.2, 0.6, 1)), color: paw },
        ];
        for (const s of [-1, 1]) for (const dy of [-1, 1]) { // whiskers
            const g = new T.CylinderGeometry(0.3, 0.3, 9, 4); g.rotateZ(Math.PI / 2);
            parts.push({ geo: g, m: M4(s * 9, -3 + dy, 9.5, 0, s * 0.35, dy * 0.12), color: black });
        }
        return merge(parts);
    }
    function lumpy(geo, amt, sd) { // a stone or nugget: a little irregular
        const g = geo.index ? geo.toNonIndexed() : geo, P = g.attributes.position;
        for (let i = 0; i < P.count; i++) {
            const x = P.getX(i), y = P.getY(i), z = P.getZ(i), k = 1 + amt * (vnoise(x * 0.4 + sd, y * 0.4 + z * 0.3) - 0.5);
            P.setXYZ(i, x * k, y * k, z * k);
        }
        return g;
    }

    let soil = null;
    const MOLE = 1.5; // the moles a size up from the original's drawing, so they read in 3D
    function buildSoil(th) {
        const env = envRT ? envRT.texture : null;
        const S = { moles: [], gems: [] };
        const group = new T.Group();
        world.add(group);
        const rocks = [], gold = [], silver = [], gems = [], holes = [], tufts = [];
        const pts = mountainPoints;
        const soilDark = lin(th.soil2).multiplyScalar(0.55), soilMid = lin(th.soil2).multiplyScalar(0.8), dirt = lin('#654321');
        const tube = (curve, r, list) => list.push({ geo: new T.TubeGeometry(curve, 16, r, 6, false), color: soilDark });
        const V = (x, y, z = 0.6) => new T.Vector3(x, -y, z);
        const grassA = lin(th.grass), grassB = lin(th.grass2), hc = new T.Color();
        const tx = txOn(th), quartz = [];
        // a vein as it lies in rock: the original's path, wandering a little, swelling and
        // pinching, flat in the face, with a few chips of quartz, small nuggets and flecks
        const strand = (P, rAt, zk, z0, color, list) => { // a tube round the points, thick and thin as rAt says
            const pos = [], idx = [], n = P.length, R = 6;
            P.forEach((p, i) => {
                const a = P[Math.max(i - 1, 0)], b = P[Math.min(i + 1, n - 1)], tx0 = b.x - a.x, ty0 = b.y - a.y, l = Math.hypot(tx0, ty0) || 1, r = rAt(i / (n - 1));
                for (let k = 0; k < R; k++) { const t = k / R * 6.2832, c = Math.cos(t) * r, sn = Math.sin(t) * r * zk; pos.push(p.x - ty0 / l * c, p.y + tx0 / l * c, p.z + z0 + sn); }
                if (i < n - 1) for (let k = 0; k < R; k++) { const v = i * R + k, w = i * R + (k + 1) % R; idx.push(v, w, v + R, w, w + R, v + R); }
            });
            const g = new T.BufferGeometry(); g.setAttribute('position', new T.Float32BufferAttribute(pos, 3)); g.setIndex(idx);
            list.push({ geo: g, color });
        };
        const taper = (t) => Math.pow(Math.max(Math.sin(t * Math.PI), 0), 0.45);
        const vein = (curve, R, main, pale, list, sd) => {
            const P = curve.getPoints(44);
            for (const p of P) { p.x += (vnoise(p.x * 0.11 + sd, p.y * 0.11) - 0.5) * 5 + (vnoise(p.x * 0.9 + sd, p.y * 0.9) - 0.5) * 5; p.y += (vnoise(p.x * 0.11, p.y * 0.11 + sd) - 0.5) * 5 + (vnoise(p.x * 0.9, p.y * 0.9 + sd) - 0.5) * 5; }
            const rAt = (t) => R * (0.3 + 0.55 * vnoise(t * 7 + sd, 2.3)) * taper(t) * Math.min(1, Math.max(0, (vnoise(t * 11 + sd * 1.7, 5.1) - 0.36) * 9)); // broken into lengths: a seam, not a worm
            for (let k = 0; k < 3; k++) { // chips of pale quartz in the rock along it
                const p = P[Math.floor(rnd() * P.length)], r = rr(1.2, 2.6);
                quartz.push({ geo: lumpy(new T.IcosahedronGeometry(r, 0), 0.7, k + sd * 3), m: M4(p.x + rr(-6, 6), p.y + rr(-6, 6), -r * 0.45, rnd() * 6, rnd() * 6, 0), color: lin('#cfc6b4').multiplyScalar(rr(0.5, 0.75)) });
            }
            strand(P, rAt, 0.3, -0.35, main, list);
            for (let k = 0, nk = 2 + Math.floor(rnd() * 3); k < nk; k++) { // nuggets on the vein
                const p = P[1 + Math.floor(rnd() * (P.length - 2))];
                list.push({ geo: lumpy(new T.IcosahedronGeometry(rr(0.7, 1.5), 0), 0.7, k + sd), m: M4(p.x + rr(-1.5, 1.5), p.y + rr(-1.5, 1.5), 0.2, rnd() * 6, rnd() * 6, 0), color: pale });
            }
            for (let k = 0; k < 7; k++) { // flecks in the rock around it
                const p = P[Math.floor(rnd() * P.length)];
                list.push({ geo: new T.TetrahedronGeometry(rr(0.4, 0.8), 0), m: M4(p.x + rr(-9, 9), p.y + rr(-9, 9), 0.3, rnd() * 6, rnd() * 6, 0), color: main.clone().multiplyScalar(0.75) });
            }
        };
        const goldC = lin('#ffae22'), goldP = lin('#ffc95a'), silverC = lin('#d8d8e4'), silverP = lin('#f4f4fa');
        for (let i = 1; i < pts.length - 2; i++) {
            const p = pts[i];
            // tufts on the edge where the original draws its little strokes
            if (!th.neon && !th.icing && !txOn(th)) for (let j = 0; j < 3; j++) {
                const x = p.x + j * 20 + rr(-6, 6), z = rr(-28, 2), y = -heightAt(x) - 1;
                const c = th.rainbowLip ? hc.setHSL(rnd(), 0.7, 0.6).clone() : grassB.clone().lerp(grassA, rnd()).multiplyScalar(rr(0.75, 1.05));
                for (let b = 0; b < 3; b++) {
                    const h = rr(7, 15), g = new T.ConeGeometry(1.5, h, 3);
                    g.translate(0, h / 2, 0);
                    tufts.push({ geo: g, m: M4(x + rr(-3, 3), y, z + rr(-3, 3), rr(-0.3, 0.3), rnd() * 6, rr(-0.45, 0.45)), color: c, foot: y, h });
                }
            }
            if (i % 5 === 0) { // stones half in the face
                for (const [dx, dy, r] of [[20, 15, 8], [35, 18, 6]]) {
                    const g = lumpy(new T.DodecahedronGeometry(r, 0), 0.5, i + dx);
                    rocks.push({ geo: g, m: new T.Matrix4().compose(V(p.x + dx, p.y + dy, 1), new T.Quaternion().setFromEuler(new T.Euler(rnd() * 6, rnd() * 6, rnd() * 6)), new T.Vector3(1, 1, 0.55)),
                                 color: soilDark.clone().lerp(soilMid, rnd()) });
                }
            }
            if (i % 7 === 0) { // a gold vein with nuggets
                const c7 = new T.CatmullRomCurve3([V(p.x, p.y + 20), V(p.x + 30, p.y + 35), V(p.x + 50, p.y + 30), V(p.x + 70, p.y + 45)], false, 'centripetal');
                if (tx) vein(c7, 2.6, goldC, goldP, gold, i); else tube(c7, 1.7, gold);
                gold.push({ geo: lumpy(new T.IcosahedronGeometry(tx ? 2.2 : 4, tx ? 1 : 0), 0.6, i), m: M4(p.x + 30, -(p.y + 35), tx ? 0.2 : 1.5, rnd() * 6, rnd() * 6, 0), color: tx ? goldP : soilDark });
                gold.push({ geo: lumpy(new T.IcosahedronGeometry(tx ? 2.8 : 5, tx ? 1 : 0), 0.6, i + 3), m: M4(p.x + 50, -(p.y + 30), tx ? 0.2 : 1.5, rnd() * 6, rnd() * 6, 0), color: tx ? goldP : soilDark });
            }
            if (i % 6 === 0) // the second kind of gold vein
                { const c6 = new T.QuadraticBezierCurve3(V(p.x + 10, p.y + 50), V(p.x + 40, p.y + 60), V(p.x + 80, p.y + 55)); if (tx) vein(c6, 3, goldC, goldP, gold, i + 50); else tube(c6, 2, gold); }
            if (i % 8 === 0) // silver
                { const c8 = new T.CatmullRomCurve3([V(p.x + 5, p.y + 40), V(p.x + 25, p.y + 50), V(p.x + 45, p.y + 45)], false, 'centripetal'); if (tx) vein(c8, 2.2, silverC, silverP, silver, i + 90); else tube(c8, 1.5, silver); }
            if (i % 9 === 0) { // two gems
                for (const [dx, dy, s, col] of [[40, 25, 8, '#00ffff'], [60, 40, 6, '#ff00ff']]) {
                    const g = new T.OctahedronGeometry(1, 0);
                    const x = p.x + dx, y = -(p.y + dy);
                    gems.push({ geo: g, m: new T.Matrix4().compose(new T.Vector3(x, y, 2), new T.Quaternion().setFromEuler(new T.Euler(0.25, rnd() * 0.8, 0)), new T.Vector3(s * 0.75, s, s * 0.6)), color: lin(col) });
                    S.gems.push({ x, y, z: 2 + s * 0.6, s, ph: rnd() * 40, f: rr(0.6, 1.3), cyan: col === '#00ffff' });
                }
            }
            if (i % 11 === 0) { // a mole in its burrow
                const x = p.x + 25, y = -(p.y + 30);
                const hole = new T.CircleGeometry(13 * MOLE, 24);
                holes.push({ geo: hole, m: M4(x, y + 1, 0.4), color: lin('#140c06') });
                holes.push({ geo: new T.SphereGeometry(15 * MOLE, 18, 8, 0, Math.PI * 2, 0, Math.PI / 2),
                             m: new T.Matrix4().compose(new T.Vector3(x, y - 12 * MOLE, 0), new T.Quaternion(), new T.Vector3(1.15, 0.55, 0.5)), color: dirt });
                S.moles.push({ x, y, ph: rnd() * 10, up: 0 });
            }
        }
        const add = (parts, mat) => { if (!parts.length) return null; const m = new T.Mesh(merge(parts), mat); m.frustumCulled = false; group.add(m); return m; };
        add(rocks, new T.MeshStandardMaterial({ vertexColors: true, roughness: 0.95, flatShading: true }));
        // the grass in the wind: each blade's vertices get how far up the blade they are
        const tuftMesh = add(tufts, windify(new T.MeshStandardMaterial({ vertexColors: true, roughness: 0.9, flatShading: true }), 0.32, 1, true));
        if (tuftMesh) {
            const P = tuftMesh.geometry.attributes.position.array, sway = new Float32Array(P.length / 3);
            let v = 0;
            for (const t of tufts) {
                const n = (t.geo.index ? t.geo.index.count : t.geo.attributes.position.count);
                for (let i = 0; i < n; i++, v++) sway[v] = Math.min(1, Math.max(0, (P[v * 3 + 1] - t.foot) / t.h));
            }
            tuftMesh.geometry.setAttribute('aSway', new T.BufferAttribute(sway, 1));
        }
        add(holes, new T.MeshStandardMaterial({ vertexColors: true, roughness: 1 }));
        add(quartz, new T.MeshStandardMaterial({ vertexColors: true, roughness: 0.6, flatShading: true }));
        add(gold, new T.MeshStandardMaterial({ color: tx ? 0xffffff : lin('#ffd24a'), vertexColors: !!tx, metalness: tx ? 0.4 : 0.9, roughness: tx ? 0.34 : 0.28, envMap: env, envMapIntensity: tx ? 0.85 : 1.4,
                                               emissive: lin('#ffb000'), emissiveIntensity: tx ? 0.06 : 0.12, flatShading: true }));
        add(silver, new T.MeshStandardMaterial({ color: tx ? 0xffffff : lin('#d8d8e4'), vertexColors: !!tx, metalness: 0.9, roughness: 0.22, envMap: env, envMapIntensity: 1.4,
                                                 emissive: lin('#8090a0'), emissiveIntensity: 0.08 }));
        const gm = add(gems, new T.MeshStandardMaterial({ vertexColors: true, metalness: 0.2, roughness: 0.05, envMap: env, envMapIntensity: 2.2,
                                               flatShading: true, emissive: lin('#ffffff'), emissiveIntensity: 0.0 }));
        // gems light up from within: a second, added copy in their own colour
        if (gm) { // the same geometry once more, added: they glow from within
            const glow = new T.Mesh(gm.geometry, new T.MeshBasicMaterial({ vertexColors: true, transparent: true, opacity: 0.35, blending: T.AdditiveBlending, depthWrite: false }));
            glow.renderOrder = 3; glow.frustumCulled = false; group.add(glow);
        }
        if (S.moles.length) {
            const geo = moleGeo(), mat = new T.MeshStandardMaterial({ vertexColors: true, roughness: 0.75 });
            for (const m of S.moles) { m.o = new T.Mesh(geo, mat); m.o.scale.setScalar(MOLE); m.o.position.set(m.x, m.y, -30 * MOLE); group.add(m.o); }
        }
        soil = S;
    }

    // per frame: the moles peek out and duck when Rob comes by; the gems twinkle
    function syncSoil(t, dt, hot) {
        const cx = cameraX + 960;
        for (const m of soil.moles) {
            if (Math.abs(m.x - cx) > 1200) { m.o.visible = false; continue; }
            m.o.visible = true;
            const near = Math.abs(rob.x - m.x) < 190 && Math.abs(-rob.y - m.y) < 240;
            const want = near ? 0 : (Math.sin(t * 0.9 + m.ph) > -0.35 ? 1 : 0);
            m.up += (want - m.up) * Math.min(1, dt * (want ? 3 : 12)); // out slowly, in fast
            const e = m.up * m.up * (3 - 2 * m.up);
            m.o.position.set(m.x, m.y + (-4 + 6 * e) * MOLE, (-26 + 30 * e) * MOLE);
            m.o.rotation.y = Math.max(-0.7, Math.min(0.7, (rob.x - m.x) / 320)) * e + Math.sin(t * 1.7 + m.ph) * 0.15;
            m.o.rotation.z = Math.sin(t * 3.1 + m.ph) * 0.06;
        }
        if (!hot) return;
        for (const g of soil.gems) {
            if (Math.abs(g.x - cx) > 1100) continue;
            const k = Math.pow(Math.max(0, Math.sin(t * g.f * 2.2 + g.ph)), 12);
            if (k > 0.02) hot.push(g.x + g.s * 0.2, g.y + g.s * 0.35, g.z + 2, g.s * 2.4, g.s * 2.4, t * 0.5 + g.ph, k, g.cyan ? 1 : 0, 11, 0);
        }
    }

    // ---------- the public part ----------
    // The whole picture once more through a shader: rays from the sun,
    // glow around bright parts, heat shimmer, colour fringes, scan lines.
    // effects (F) and the free camera (C) are kept, as music, sound and quality are
    function prefGet(k, d) { try { const v = localStorage.getItem('rr3d.' + k); return v === null ? d : JSON.parse(v); } catch (e) { return d; } }
    function prefSet(k, v) { try { localStorage.setItem('rr3d.' + k, JSON.stringify(v)); } catch (e) {} }
    let aoRT = [], aoMat = null, volRT = [], volMat = null, rt = null, rayRT = null, rayMat = null, copyMat = null, post = null, postScene, postCam, postQuad, glowRT = [], brightMat, blurMat, fxOn = /[?&]fx=0/.test(location.search) ? false : prefGet('fx', true);
    const sunDirNow = new T.Vector3(0, 0, -1);
    // dynamic resolution: the scene is drawn into the lower-left part uS of its target
    // (no new buffers), and every pass reads only that part
    const US = { value: 1 };
    function initPost() {
        const gl2 = renderer.capabilities.isWebGL2;
        const half = gl2 && renderer.extensions.has('EXT_color_buffer_float');
        const opt = { type: half ? T.HalfFloatType : T.UnsignedByteType };
        rt = new T.WebGLRenderTarget(1920, 1080, { ...opt, samples: gl2 ? 4 : 0 });
        rt.depthTexture = new T.DepthTexture(1920, 1080); // for the light in the air
        post = new T.ShaderMaterial({
            depthTest: false, depthWrite: false,
            uniforms: {
                tScene: { value: rt.texture }, tGlow: { value: null }, tRays: { value: null }, uTime: { value: 0 }, uSun: { value: new T.Vector2(0.5, 0.8) }, uSunVis: { value: 0 },
                uRays: { value: 0 }, uRayCol: { value: new T.Color(1, 1, 1) }, uBloom: { value: 0 }, uHaze: { value: 0 },
                uCA: { value: 0 }, uScan: { value: 0 }, uPx: { value: new T.Vector2(1 / 1920, 1 / 1080) }, uDusk: { value: 0 }, tAO: { value: null }, uAO: { value: 0 }, uGrade: { value: 0 },
                tVol: { value: null }, uVol: { value: 0 }, uVolCol: { value: new T.Color(1, 1, 1) }, uS: US,
            },
            vertexShader: 'varying vec2 vUv; void main(){ vUv=uv; gl_Position=vec4(position.xy,0.,1.); }',
            fragmentShader: `uniform sampler2D tScene,tGlow,tRays,tVol,tAO; uniform float uAO,uGrade; uniform vec2 uSun,uPx; uniform vec3 uRayCol,uVolCol;
uniform float uTime,uSunVis,uRays,uBloom,uHaze,uCA,uScan,uDusk,uVol,uS; varying vec2 vUv; ${GLSL_NOISE}
vec3 tex(vec2 p){ return texture2D(tScene,clamp(p,uPx,1.-uPx)*uS).rgb; }
void main(){ vec2 uv=vUv;
 if(uHaze>0.){ float k=smoothstep(.95,.25,uv.y)*uHaze;
   uv+=vec2(noise(uv*vec2(18.,40.)+vec2(0.,uTime*2.2))-.5,noise(uv*vec2(22.,36.)-vec2(uTime*1.7,0.))-.5)*uPx*vec2(5.,7.)*k; }
 vec3 c;
 if(uCA>0.){ vec2 o=(uv-.5)*uCA*.006; c=vec3(tex(uv+o).r,tex(uv).g,tex(uv-o).b); } else c=tex(uv);
 // dusk: the day dims to evening; what glows (fireworks, sparks, lamps) keeps its light
 if(uDusk>0.){ float l=max(c.r,max(c.g,c.b)); vec3 tint=mix(vec3(1.),vec3(.42,.36,.55),uDusk);
   c=mix(c*tint,c,smoothstep(.95,1.7,l)); }
 if(uAO>0.) c*=mix(1.,texture2D(tAO,uv).r,uAO);
 // the light in the air: r = sunlit haze along the view ray, g = all haze (it dims what is behind it)
 if(uVol>0.){ vec2 v=texture2D(tVol,uv).rg; c=c*exp(-v.g*uVol*.55)+uVolCol*v.r*uVol; }
 if(uGrade>0.){ float l=dot(c,vec3(.2126,.7152,.0722)); vec3 g=mix(vec3(l),c,1.14); // a little more colour
   g*=mix(vec3(.93,.98,1.07),vec3(1.05,1.01,.93),smoothstep(.02,.55,l)); // cool shade, warm light
   g=pow(max(g,vec3(0.)),vec3(1.08))*1.07; // deeper shadows
   c=mix(c,g,uGrade); }
 if(uBloom>0.) c+=texture2D(tGlow,uv).rgb*uBloom*.9;
 if(uRays>0.&&uSunVis>0.) c+=uRayCol*texture2D(tRays,uv).r*uRays*uSunVis*2.2;
 if(uScan>0.){ c*=.9+.1*sin(uv.y*1080.*3.14159); c+=vec3(.02,0.,.03)*uScan; }
 float v=length((uv-.5)*vec2(1.,.8)); c*=1.-.28*pow(v*1.25,2.6);
 gl_FragColor=linearToOutputTexel(vec4(c,1.)); }`,
        });
        // the glow: the bright parts at a quarter of the size, blurred
        // across and down twice (soft, without the speckles of loose taps)
        for (let i = 0; i < 2; i++) glowRT.push(new T.WebGLRenderTarget(480, 270, opt));
        const vs = 'varying vec2 vUv; void main(){ vUv=uv; gl_Position=vec4(position.xy,0.,1.); }';
        brightMat = new T.ShaderMaterial({ depthTest: false, depthWrite: false, vertexShader: vs,
            uniforms: { tSrc: { value: rt.texture }, uPx: { value: new T.Vector2(1 / 1920, 1 / 1080) }, uS: US },
            fragmentShader: `uniform sampler2D tSrc; uniform vec2 uPx; uniform float uS; varying vec2 vUv;
void main(){ vec3 c=vec3(0.);
 for(int j=0;j<2;j++) for(int i=0;i<2;i++) c+=texture2D(tSrc,(vUv+(vec2(float(i),float(j))*2.-1.)*uPx)*uS).rgb;
 c*=.25; float l=max(c.r,max(c.g,c.b)); gl_FragColor=vec4(c*smoothstep(.72,1.25,l),1.); }` });
        blurMat = new T.ShaderMaterial({ depthTest: false, depthWrite: false, vertexShader: vs,
            uniforms: { tSrc: { value: null }, uDir: { value: new T.Vector2() } },
            fragmentShader: `uniform sampler2D tSrc; uniform vec2 uDir; varying vec2 vUv;
void main(){ vec3 c=texture2D(tSrc,vUv).rgb*.2270;
 c+=(texture2D(tSrc,vUv+uDir*1.3846).rgb+texture2D(tSrc,vUv-uDir*1.3846).rgb)*.3162;
 c+=(texture2D(tSrc,vUv+uDir*3.2308).rgb+texture2D(tSrc,vUv-uDir*3.2308).rgb)*.0703;
 gl_FragColor=vec4(c,1.); }` });
        post.uniforms.tGlow.value = glowRT[0].texture;
        // the sun's rays: 48 steps towards the sun per pixel, but at a quarter of the
        // size (they are soft anyway): a sixteenth of the work of doing it per screen pixel
        rayRT = new T.WebGLRenderTarget(480, 270, opt);
        post.uniforms.tRays.value = rayRT.texture;
        rayMat = new T.ShaderMaterial({ depthTest: false, depthWrite: false, vertexShader: vs,
            uniforms: { tSrc: { value: rt.texture }, uSun: post.uniforms.uSun, uPx: { value: new T.Vector2(1 / 1920, 1 / 1080) }, uS: US },
            fragmentShader: `uniform sampler2D tSrc; uniform vec2 uSun,uPx; uniform float uS; varying vec2 vUv;
void main(){ vec2 dl=(vUv-uSun)*(1./48.)*.95; vec2 p=vUv; float w=1.,acc=0.;
 for(int i=0;i<48;i++){ p-=dl; vec3 s=texture2D(tSrc,clamp(p,uPx,1.-uPx)*uS).rgb; float l=max(dot(s,vec3(.3,.59,.11))-.55,0.);
   acc+=l*w*smoothstep(.75,0.,length((p-uSun)*vec2(1.78,1.))); w*=.965; }
 gl_FragColor=vec4(acc*(1./48.),0.,0.,1.); }` });
        // the light in the air (volumetric light): per pixel, from the camera to what it
        // sees, a number of steps through the haze; at each step the sun's shadow map says
        // whether the sun reaches that point. Lit haze glows, haze in shadow stays dark, so
        // the shadows of trees, rocks and Rob become shafts in the air. Worked out at a
        // quarter (hoog) or half (ultra) of the size, with a dither that the blur removes.
        for (let i = 0; i < 2; i++) volRT.push(new T.WebGLRenderTarget(480, 270, opt));
        volMat = new T.ShaderMaterial({ depthTest: false, depthWrite: false, vertexShader: vs,
            defines: { STEPS: 24 },
            uniforms: { tDepth: { value: rt.depthTexture }, tShadow: { value: null }, uShadowM: { value: new T.Matrix4() },
                uInvProj: { value: new T.Matrix4() }, uCamWorld: { value: new T.Matrix4() }, uCamPos: { value: new T.Vector3() },
                uLight: { value: new T.Vector3(0, 1, 0) }, uRefY: { value: 0 }, uMaxD: { value: 7000 }, uDens: { value: 1 / 4500 },
                uTime: { value: 0 }, uCloud: { value: 0.8 }, uS: US },
            fragmentShader: `#include <packing>
uniform sampler2D tDepth,tShadow; uniform mat4 uShadowM,uInvProj,uCamWorld; uniform vec3 uCamPos,uLight;
uniform float uRefY,uMaxD,uDens,uTime,uCloud,uS; varying vec2 vUv; ${GLSL_NOISE}
// clouds high above the level: where the sun's line from a point crosses their
// layer, a patch of cloud takes most of the light -- the big shafts, drifting with the wind
float cloudAt(vec3 p){
 vec3 q=p+uLight*((uRefY+2600.-p.y)/max(uLight.y,.2));
 vec2 c=q.xz*vec2(1./2200.,1./1500.)+vec2(uTime*.05,0.);
 float n=noise(c)*.65+noise(c*2.3+7.1)*.35;
 return smoothstep(.42,.62,n)*uCloud; }
void main(){
 float d=texture2D(tDepth,vUv*uS).x;
 vec4 vp=uInvProj*vec4(vUv*2.-1.,d*2.-1.,1.); vp/=vp.w;
 vec3 wp=(uCamWorld*vec4(vp.xyz,1.)).xyz, rd=wp-uCamPos; float L=length(rd); rd/=L; L=min(L,uMaxD);
 // only the air behind the front of the world counts: in front of it is the cut the
 // player looks through (the soil face, the figures on the line)
 float t0=rd.z<-1e-4?max(0.,(uCamPos.z-40.)/-rd.z):L; L=max(L-t0,0.);
 float st=L/float(STEPS), n=fract(52.9829189*fract(dot(gl_FragCoord.xy,vec2(.06711056,.00583715))));
 float lit=0.,tot=0.;
 for(int i=0;i<STEPS;i++){
  vec3 p=uCamPos+rd*(t0+(float(i)+n)*st);
  float h=exp(-clamp((p.y-uRefY)/1600.,0.,6.)); // thinner higher up
  vec4 sc=uShadowM*vec4(p,1.); float s=1.;
  if(sc.x>0.&&sc.x<1.&&sc.y>0.&&sc.y<1.&&sc.z<1.) s=step(sc.z-.0015,unpackRGBAToDepth(texture2D(tShadow,sc.xy)));
  s*=1.-cloudAt(p);
  lit+=s*h; tot+=h; }
 // more towards the sun (forward scattering), a little everywhere
 float c=dot(rd,uLight), g=.55, ph=.35+.65*(1.-g*g)/pow(1.+g*g-2.*g*c,1.5)*.25;
 gl_FragColor=vec4(lit*st*uDens*ph,tot*st*uDens,0.,1.); }` });
        // contact shadow: where things stand close together (a figure on the ground, a stone in
        // the grass) less of the sky reaches. Worked out from the depth at half the size
        for (let i = 0; i < 2; i++) aoRT.push(new T.WebGLRenderTarget(960, 540, opt));
        aoMat = new T.ShaderMaterial({ depthTest: false, depthWrite: false, vertexShader: vs,
            uniforms: { tDepth: { value: rt.depthTexture }, uInvProj: { value: new T.Matrix4() }, uProjY: { value: 1 }, uS: US },
            fragmentShader: `uniform sampler2D tDepth; uniform mat4 uInvProj; uniform float uProjY,uS; varying vec2 vUv;
vec3 vpos(vec2 q){ float d=texture2D(tDepth,clamp(q,0.,1.)*uS).x; vec4 p=uInvProj*vec4(q*2.-1.,d*2.-1.,1.); return p.xyz/p.w; }
void main(){ if(texture2D(tDepth,vUv*uS).x>.99999){ gl_FragColor=vec4(1.); return; }
 const float R=62.; vec3 P=vpos(vUv), N=normalize(cross(dFdx(P),dFdy(P)));
 float rp=R*uProjY*.5/max(-P.z,1.), a=6.2832*fract(52.9829189*fract(dot(gl_FragCoord.xy,vec2(.06711056,.00583715)))), occ=0.;
 for(int i=0;i<12;i++){ float t=(float(i)+.5)/12., ang=a+t*18.85; vec3 v=vpos(vUv+vec2(cos(ang)*.5625,sin(ang))*rp*t)-P; float vv=dot(v,v);
  occ+=max(dot(N,v)-1.5,0.)/(vv+30.)*clamp(1.-vv/(R*R),0.,1.); }
 float ao=clamp(1.-occ*2.4*R/12./5.,0.,1.);
 gl_FragColor=vec4(vec3(mix(1.,ao,smoothstep(6500.,3200.,-P.z))),1.); }` });
        // without effects (F): the scene still goes through the multisampled target, then straight out
        copyMat = new T.ShaderMaterial({ depthTest: false, depthWrite: false, vertexShader: vs, uniforms: { tSrc: { value: rt.texture }, uS: US },
            fragmentShader: `uniform sampler2D tSrc; uniform float uS; varying vec2 vUv; void main(){ gl_FragColor=linearToOutputTexel(vec4(texture2D(tSrc,vUv*uS).rgb,1.)); }` });
        postScene = new T.Scene();
        postQuad = new T.Mesh(new T.PlaneGeometry(2, 2), post);
        postQuad.frustumCulled = false;
        postScene.add(postQuad);
        postCam = new T.OrthographicCamera(-1, 1, 1, -1, 0, 1);
        window.addEventListener('keydown', (e) => { if (e.repeat) return; if (e.code === 'KeyF') VIEW.setFx(!fxOn);
            if (e.code === 'KeyH') VIEW.setShadow(!SHADOW.on); if (e.code === 'KeyC') VIEW.setCam(!CAM.on); });
    }

    function init() {
        const cv = document.getElementById('three');
        // no multisampling on the canvas itself: only a full-screen quad is drawn there; the
        // scene is multisampled in its own target (rt), with or without effects
        renderer = new T.WebGLRenderer({ canvas: cv, antialias: false, powerPreference: 'high-performance' });
        renderer.setPixelRatio(1);
        renderer.setSize(1920, 1080, false);
        renderer.outputColorSpace = T.SRGBColorSpace;
        renderer.shadowMap.enabled = true;
        txLoad();
        renderer.shadowMap.type = T.PCFSoftShadowMap;
        renderer.useLegacyLights = true; // the world's lights were set up for r128's light units
        pmrem = new T.PMREMGenerator(renderer);
        camera = new T.PerspectiveCamera(FOV, 1920 / 1080, 20, 60000);
        scene = new T.Scene();
        initPost();
        perfInit();
    }

    function dispose(o) {
        o.traverse((c) => {
            if (c.geometry) c.geometry.dispose();
            if (c.material) (Array.isArray(c.material) ? c.material : [c.material]).forEach((m) => m.dispose());
        });
    }

    // how far each level turns to evening at the finish: the night levels hardly
    const DUSK = { forest: 1, desert: 0.9, snow: 1, volcano: 0.45, jungle: 1, space: 0, candy: 1, cyber: 0.15, hell: 0, rainbow: 1 };
    let dusk = null, duskMats = [];
    const DUSK_CLOUD = new T.Color(0.32, 0.16, 0.2), DUSK_CLOUD_TX = new T.Color(1, 0.62, 0.3); // photo clouds: warm, the screen's evening tint comes on top
    const DUSK_SUN = new T.Color(1, 0.42, 0.16), DUSK_FOG = new T.Color(0.09, 0.05, 0.1);
    function syncDusk(dt) {
        const want = (gameWon ? dusk.k : 0);
        if (Math.abs(want - dusk.now) < 1e-4 && want === 0) return;
        dusk.now += (want - dusk.now) * (1 - Math.exp(-dt * (want > dusk.now ? 1.2 : 3)));
        const e = dusk.now;
        sky.material.uniforms.uDusk.value = e;
        dusk.dl.intensity = dusk.dlI * (1 - 0.55 * e); dusk.dl.color.copy(dusk.dlC).lerp(DUSK_SUN, e * 0.8);
        dusk.hemi.intensity = dusk.hemiI * (1 - 0.6 * e);
        if (scene.fog) scene.fog.color.copy(dusk.fog).lerp(DUSK_FOG, e * 0.85);
        for (const [m, c, to] of duskMats) m.color.copy(c).lerp(to || DUSK_CLOUD, e * 0.8);
    }

    function build(level) {
        const cfg = levels[level], th = THEMES[cfg.theme] || THEMES.forest;
        seed = 1000 + level * 7919;
        TX.level = !!txOn(th); TX.set = TX.level ? { ...TX_D, ...th.tex } : null;
        if (world) { scene.remove(world); dispose(world); }
        if (sky) { scene.remove(sky); dispose(sky); }
        updaters = []; timed = []; duskMats = [];
        world = new T.Group();
        scene.add(world);
        ridge = mountainPoints.slice(0, mountainPoints.length - 2); // without the two closing points at y = 1080
        endX = ridge[ridge.length - 1].x;
        const xmin = -3500, xmax = endX + 5000;
        prepSmooth(xmin - 17000, xmax + 17000);

        fxNow = FX[cfg.theme] || FX.forest;
        sky = makeSky(th, cfg.skyColor1, cfg.skyColor2, fxNow);
        sunDirNow.set(...th.sun.dir).normalize();
        post.uniforms.uRayCol.value = lin(th.sun.color);
        scene.add(sky);
        scene.fog = new T.Fog(srgb(th.fog[0]), th.fog[1], th.fog[2]);
        const sunDir = new T.Vector3(...th.sun.dir).normalize();
        const dl = new T.DirectionalLight(srgb(th.light), th.lightI);
        dl.position.set(-sunDir.x * 1000, sunDir.y * 1000 + 300, 800);
        world.add(dl);
        const hemi = new T.HemisphereLight(srgb(th.hemi[0]), srgb(th.hemi[1]), th.hemi[2]);
        world.add(hemi);
        dusk = { k: DUSK[cfg.theme] ?? 0, now: 0, dl, dlI: dl.intensity, dlC: dl.color.clone(), hemi, hemiI: hemi.intensity,
                 fog: scene.fog.color.clone() };
        // a rim from behind and above in the level's own colour: an edge of
        // light round Rob and the figures, so they belong to this world and
        // stand out from a dark one
        const rim = new T.DirectionalLight(srgb(th.rim || th.sun.color), th.rimI ?? 1.1);
        rim.position.set(sunDir.x > 0 ? 500 : -500, 700, -900);
        world.add(rim);

        buildGround(th, xmin - 14000, xmax + 14000);
        buildMountains(th, xmin, xmax);
        buildTrees(th, xmin, xmax);
        if (txOn(th)) txGrass(xmin, xmax);
        if (th.falls) buildFalls(th, th.falls, 0, endX + 1500);
        if (th.volcanoes) buildVolcanoes(th, th.volcanoes, 0, endX + 2000);
        if (th.mesas) buildMesas(xmin, xmax, txOn(th));
        if (th.clouds) (txOn(th) ? txClouds : buildClouds)(th.clouds, xmin, xmax);
        if (th.mist) buildMistBanks(th.mist, xmin, xmax);
        if (th.planets) buildSpace(xmin, xmax);
        if (th.skyline) buildSkyline(xmin, xmax);
        if (th.snow) buildParticles('snow');
        if (th.embers) buildParticles('embers');
        if (th.ash) buildParticles('ash');
        if (th.fireflies) buildParticles('fireflies');
        if (th.candy) buildParticles('candy');
        if (th.sparkles) buildParticles('sparkles');
        if (th.data) buildParticles('data');
        timed.push(sky.material);
        figs = null;
        try { buildFigures(cfg, th); } catch (e) { console.error('RR3D figures', e); figs = null; }
        window.RR3D.figures = !!figs;
        fire = null;
        try { buildFire(th); } catch (e) { console.error('RR3D fire', e); fire = null; }
        window.RR3D.fire = !!fire;
        course = null;
        try { buildCourse(th); } catch (e) { console.error('RR3D course', e); course = null; }
        window.RR3D.course = !!course;
        soil = null;
        try { buildSoil(th); } catch (e) { console.error('RR3D soil', e); soil = null; }
        window.RR3D.soil = !!soil;
        shadowSetup(dl);
    }

    // ---- the sun's shadow: a real shadow map from the level's sun, cast by everything
    // solid (ground, trees, rocks, buildings, Rob, monsters, mines, saucers, Julians)
    // onto everything solid. One 2048 map whose box follows the camera, snapped to
    // whole texels so its edges do not crawl. H turns it off (and back to the soft
    // blobs under the figures); ?shadows=0 starts without.
    // The level's sun stands in front, on the camera's side, so its shadows fall away
    // behind what casts them. So the sunlight is split: part stays as it was, part
    // (SPLIT) comes from a sun further to the side and higher, and that part casts the
    // shadow, onto the ground beside and behind the figures and trees. Together they
    // light about as much as the one sun did.
    const SHADOW = { on: !/[?&]shadows=0/.test(location.search), dl: null, main: null, size: 2048, half: 2000, side: 1, elev: 0.8, back: -0.5 }; // the sun a little behind the trees: shadows fall towards the player, the haze lights up
    const SPLIT = 0.85;
    const _m4 = new T.Matrix4(), _v = new T.Vector3(), _dir = new T.Vector3(), _up = new T.Vector3(0, 1, 0);
    function solid(m) { return m && !m.transparent && (m.isMeshStandardMaterial || m.isMeshLambertMaterial || m.isMeshPhongMaterial || m.isMeshPhysicalMaterial); }
    function shadowize(root) {
        root.traverse(o => {
            if (!o.isMesh) return;
            const ms = Array.isArray(o.material) ? o.material : [o.material];
            if (ms.some(solid)) { o.castShadow = !o.userData.noCast; o.receiveShadow = true; }
        });
    }
    function shadowSetup(main) {
        const side = main.position.x >= 0 ? 1 : -1;
        const dl = new T.DirectionalLight(main.color.clone(), main.intensity * SPLIT);
        main.intensity *= 1 - SPLIT; if (dusk) dusk.dlI *= 1 - SPLIT;
        world.add(dl);
        SHADOW.dl = dl; SHADOW.main = main;
        SHADOW.side = side;
        _dir.set(side * 1.15, SHADOW.elev, SHADOW.back).normalize();
        dl.position.copy(_dir).multiplyScalar(5000);
        dl.shadow.mapSize.set(SHADOW.size, SHADOW.size);
        const c = dl.shadow.camera;
        c.left = -SHADOW.half; c.right = SHADOW.half; c.top = SHADOW.half * 0.75; c.bottom = -SHADOW.half * 0.75;
        c.near = 10; c.far = 12000; c.updateProjectionMatrix();
        dl.shadow.bias = -0.0004; dl.shadow.normalBias = 3; dl.shadow.radius = 3;
        world.add(dl.target);
        shadowize(world);
        dl.castShadow = SHADOW.on;
    }
    function shadowFollow() {
        const dl = SHADOW.dl;
        if (!dl || !SHADOW.on) return;
        _dir.set(SHADOW.side * 1.15, SHADOW.elev, SHADOW.back).normalize();
        // the middle of the box: where the camera looks, a little way into the world
        _v.set(CAM.tx, CAM.ty, -600);
        // to light space, onto the texel grid, and back
        _m4.lookAt(_dir, new T.Vector3(), _up);
        const inv = _m4.clone().invert();
        _v.applyMatrix4(inv);
        const tex = (SHADOW.half * 2) / SHADOW.size;
        _v.x = Math.round(_v.x / tex) * tex; _v.y = Math.round(_v.y / tex) * tex;
        _v.applyMatrix4(_m4);
        dl.target.position.copy(_v);
        dl.position.copy(_v).addScaledVector(_dir, 5000);
    }

    // ---------- quality: four settings and an automatic choice ----------
    // What a setting changes: the size the scene is drawn at (the canvas stays
    // 1920x1080: smaller is scaled up, larger scaled down = supersampling), the
    // multisampling, the sun's shadow map, and the glow and sun-ray passes. Q goes
    // round auto -> laag -> midden -> hoog -> ultra; ?q=laag etc. picks one; the
    // choice is remembered in this browser. "auto" measures this device once the
    // first level stands (the full-load test, shorter) and takes the best setting
    // that needs at most half of a screen refresh -- the other half is margin for
    // a phone that warms up and clocks down, and for the browser itself. While
    // playing it steps one setting down if frames keep coming late.
    const PRESETS = {
        laag:   { scale: 0.6, msaa: 0, shadow: 0,    bloom: true, rays: false, vol: 0 },
        midden: { scale: 0.8, msaa: 2, shadow: 1024, bloom: true, rays: true,  vol: 0, ao: true },
        hoog:   { scale: 1,   msaa: 4, shadow: 2048, bloom: true, rays: true,  vol: 0.25, steps: 24, ao: true },
        ultra:  { scale: 1.5, msaa: 4, shadow: 4096, bloom: true, rays: true,  vol: 0.5,  steps: 48, ao: true },
    };
    const ORDER = ['laag', 'midden', 'hoog', 'ultra'];
    const QUAL = { mode: 'auto', name: 'hoog', p: PRESETS.hoog, test: null, note: '', late: 0, seen: 0, lastDown: 0 };
    function qualStored() {
        const m = /[?&]q=(auto|laag|midden|hoog|ultra|2d)/.exec(location.search);
        if (m) return m[1];
        try { const v = localStorage.getItem('rr3d.quality'); if (v === 'auto' || v === '2d' || PRESETS[v]) return v; } catch (e) {}
        return 'auto';
    }
    function qualApply(name) {
        const p = PRESETS[name], gl2 = renderer.capabilities.isWebGL2;
        QUAL.name = name; QUAL.p = p;
        // only ultra draws every refresh of a fast screen; the others 60 pictures a second
        if (window.RRClock) RRClock.cap = name !== 'ultra';
        const w = Math.round(1920 * p.scale), h = Math.round(1080 * p.scale), ms = gl2 ? p.msaa : 0;
        if (rt.width !== w || rt.height !== h || rt.samples !== ms) {
            rt.samples = ms; rt.setSize(w, h); rt.dispose(); // made again at the next use
            for (const m of [post, brightMat, rayMat]) m.uniforms.uPx.value.set(1 / w, 1 / h);
        }
        if (p.vol) {
            const vw = Math.round(1920 * p.vol), vh = Math.round(1080 * p.vol);
            for (const r of volRT) if (r.width !== vw) r.setSize(vw, vh);
            if (volMat.defines.STEPS !== p.steps) { volMat.defines.STEPS = p.steps; volMat.needsUpdate = true; }
        }
        const dl = SHADOW.dl, size = p.shadow || SHADOW.size;
        if (size !== SHADOW.size) {
            SHADOW.size = size;
            if (dl) { dl.shadow.mapSize.set(size, size); if (dl.shadow.map) { dl.shadow.map.dispose(); dl.shadow.map = null; } }
        }
        SHADOW.on = p.shadow > 0;
        if (dl) dl.castShadow = SHADOW.on;
    }
    function qualSet(mode, why) {
        QUAL.mode = mode; QUAL.note = why || '';
        try { localStorage.setItem('rr3d.quality', mode); } catch (e) {}
        if (mode === 'auto') qualAuto(); else qualApply(mode);
        qualToast();
    }
    // the cost of one frame of the setting now in use, the GPU at full clock
    // (gl.finish does not really wait in every browser -- in Chrome on a Mac the GPU
    // still had work queued after it; reading back one pixel does wait for everything)
    const _px = new Uint8Array(4);
    function gpuWait(gl) { gl.readPixels(0, 0, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, _px); }
    function timeRenders(n = 8, k = 3) {
        const gl = PERF.gl, on = PERF.on; PERF.on = false;
        for (let i = 0; i < 3; i++) render(); gpuWait(gl);
        const ts = [];
        for (let j = 0; j < k; j++) {
            const t0 = performance.now();
            for (let i = 0; i < n; i++) render();
            gpuWait(gl); ts.push((performance.now() - t0) / n);
        }
        PERF.on = on; ts.sort((a, b) => a - b);
        return ts[k >> 1];
    }
    function qualAuto() {
        // never less than 60 Hz: while the page starts up (shaders, music, the first
        // level) frames come slowly and the clock may read 30, and a phone in power
        // saving runs at 30 -- neither is a reason for a heavier setting
        const hz = Math.max(60, window.RRClock ? RRClock.hz : 60), budget = 1000 / hz / 2;
        qualApply('hoog');
        const hi = timeRenders();
        // the other settings from the measured one: 0.6x for midden. Never ultra by
        // itself: it fits easily on a fast computer, but keeps its graphics chip busy
        // (2.25x the pixels, a 4096 shadow map) and warm -- ultra is a choice (Q)
        let pick = hi <= budget ? 'hoog' : hi * 0.6 <= budget ? 'midden' : 'laag';
        let ms = hi;
        if (pick !== 'hoog') {
            qualApply(pick); ms = timeRenders();
            if (pick === 'midden' && ms > budget) { pick = 'laag'; qualApply(pick); ms = timeRenders(); }
        }
        QUAL.test = { hz, budget, hi, ms, pick };
        QUAL.late = 0; QUAL.seen = 0; QUAL.lastDown = performance.now();
    }
    // while playing (auto only): a quarter of the frames late, twice in a row -> one setting down
    let qualStrike = 0;
    function qualWatch(gap) {
        if (QUAL.mode !== 'auto' || !QUAL.test || gap > 100 || document.hidden || (window.RRBot && RRBot.on)) return;
        const per = 1000 / QUAL.test.hz; // the refresh as measured before the load
        QUAL.seen++; if (gap > per * 1.5) QUAL.late++;
        if (QUAL.seen < 180) return;
        const bad = QUAL.late > QUAL.seen / 4;
        QUAL.seen = 0; QUAL.late = 0;
        qualStrike = bad ? qualStrike + 1 : 0;
        const i = ORDER.indexOf(QUAL.name), now = performance.now();
        if (qualStrike >= 2 && i > 0 && now - QUAL.lastDown > 10000) {
            qualApply(ORDER[i - 1]); QUAL.lastDown = now; qualStrike = 0;
            QUAL.note = 'stepped down: frames came too late'; qualToast();
        }
    }
    let toastEl = null, toastT = 0;
    function qualToast() {
        if (window.RRGhost && RRGhost.say) { RRGhost.say('quality: ' + qualLabel() + (QUAL.note ? ' - ' + QUAL.note : '')); return; }
        if (!toastEl) {
            toastEl = document.createElement('div');
            toastEl.style.cssText = 'position:fixed;left:50%;top:14px;transform:translateX(-50%);z-index:30;pointer-events:none;' +
                'font:15px monospace;color:#fff;background:rgba(0,0,0,.6);padding:6px 12px;border-radius:8px;transition:opacity .6s;opacity:0';
            document.body.appendChild(toastEl);
        }
        toastEl.textContent = 'quality: ' + qualLabel() + (QUAL.note ? ' — ' + QUAL.note : '') + '   (Q)';
        toastEl.style.opacity = 1; clearTimeout(toastT); toastT = setTimeout(() => { toastEl.style.opacity = 0; }, 3500);
    }
    const QNAME = { laag: 'Low', midden: 'Medium', hoog: 'High', ultra: 'Ultra' }; // the keys stay as saved before
    function qualLabel() { return QUAL.mode === 'auto' ? 'Auto (' + QNAME[QUAL.name] + ')' : QNAME[QUAL.name]; }
    // "2d": the original game as it is, on its own canvas. The page plays like that
    // whenever window.RR3D is missing, so it is put aside and given back; a level
    // loaded meanwhile is built when the 3D world returns.
    const FLAT = { api: null, built: -1 };
    function flatOn() {
        if (FLAT.api || !window.RR3D) return;
        FLAT.api = window.RR3D; window.RR3D = null;
        document.getElementById('three').style.visibility = 'hidden';
    }
    function flatOff() {
        if (!FLAT.api) return;
        window.RR3D = FLAT.api; FLAT.api = null;
        document.getElementById('three').style.visibility = '';
        if (FLAT.built !== currentLevel) window.RR3D.build(currentLevel);
        if (window.RRClock && RRClock.reset) RRClock.reset();
    }
    // Q: the next quality, V: the next view (origineel 2D, 3D, 3D met texturen)
    window.addEventListener('keydown', e => {
        if (e.repeat || !renderer || /^(INPUT|TEXTAREA|SELECT)$/.test(e.target.tagName || '')) return;
        if (e.code === 'KeyQ') { const all = ['auto', ...ORDER]; VIEW.setQuality(all[(all.indexOf(QUAL.mode) + 1) % all.length]); }
        if (e.code === 'KeyV') { const all = VIEW.views; VIEW.setView(all[(all.indexOf(VIEW.view) + 1) % all.length]); }
    });
    // ---- what the menu (menu.js) reads and sets: the view, the quality and the extras
    const VNAME = { '2d': 'Original 2D', '3d': '3D', tex: '3D with textures' };
    let view = 'tex';
    const VIEW = {
        ok: true,
        get views() { return TX.have ? ['2d', '3d', 'tex'] : ['2d', '3d']; },
        get view() { return view; },
        viewName: (v) => VNAME[v || view],
        setView(v, quiet) {
            if (!VNAME[v] || (v === 'tex' && !TX.have)) v = TX.have ? 'tex' : '3d';
            const was = view; view = v; prefSet('view', v);
            if (v === '2d') flatOn();
            else {
                const tx = v === 'tex', changed = TX.on !== tx;
                TX.on = tx; flatOff();
                if (changed && window.RR3D && FLAT.built === currentLevel) window.RR3D.build(currentLevel);
                // "auto" measures in 3D: not done yet if the page started in 2D
                if (QUAL.mode === 'auto' && !QUAL.test && !quiet) setTimeout(() => { try { if (window.RR3D) qualAuto(); } catch (e) {} }, 300);
            }
            if (!quiet && was !== v && window.RRGhost && RRGhost.say) RRGhost.say('view: ' + VNAME[v]);
        },
        get quality() { return QUAL.mode; },
        get qualityName() { return QUAL.name; },
        qualities: ['auto', ...ORDER],
        qualityLabel: (m) => m ? (m === 'auto' ? 'Auto' : QNAME[m]) : qualLabel(),
        setQuality(m) { if (m === 'auto' || PRESETS[m]) qualSet(m); },
        get fx() { return fxOn; }, setFx(v) { fxOn = !!v; prefSet('fx', fxOn); },
        get shadow() { return SHADOW.on; }, setShadow(v) { SHADOW.on = !!v; if (SHADOW.dl) SHADOW.dl.castShadow = SHADOW.on; },
        get cam() { return CAM.on; }, setCam(v) { CAM.on = !!v; prefSet('cam', CAM.on); },
        get perf() { return PERF.on; }, setPerf(v) { PERF.on = !!v; if (PERF.el) PERF.el.style.display = PERF.on ? 'block' : 'none'; PERF.acc = {}; PERF.n = 0; PERF.gaps = []; },
    };

    // The free camera. With nothing left on the 2D canvas the camera no longer
    // has to look straight at z = 0: it orbits the point the game's camera looks
    // at. It leans with Rob's speed, looks down a little, pulls back when he is
    // in the air, shakes with explosions and hard landings, looks up at a beam,
    // and closes in on Rob at the finish. Only the view changes; the game's
    // camera, its physics and its controls stay as they are. C (or ?cam=0)
    // gives the original straight view back.
    const CAM = { on: /[?&]cam=0/.test(location.search) ? false : prefGet('cam', true), yaw: 0, pitch: 0, zoom: 1, roll: 0, trauma: 0, air: 0, peak: 0, fx: 0, fy: 0 };
    function shake(a) { if (CAM.on) CAM.trauma = Math.min(1, CAM.trauma + a); }
    function camRig(t, dt) {
        const Lx = cameraX + 960, Ly = -(cameraY + 540);
        let yawT = 0, pitchT = 0, zoomT = 1, focusT = 0;
        if (CAM.on) {
            const vx = rob.vx || 0, h = Math.max(0, getTerrainHeight(rob.x) - rob.y - rob.radius);
            yawT = Math.max(-0.14, Math.min(0.14, vx * 0.011));
            pitchT = 0.08 + Math.min(0.07, h * 0.0002);
            zoomT = 1 + Math.min(0.12, h * 0.00028);
            // a hard landing: the fall's height sets the bump
            if (h > 4) { CAM.air = 1; CAM.peak = Math.max(CAM.peak, h); }
            else if (CAM.air) { if (CAM.peak > 160) shake(Math.min(0.35, CAM.peak / 1400)); CAM.air = 0; CAM.peak = 0; }
            if (beamActive) { pitchT -= 0.07; zoomT = 1.06; }
            if (gameWon) { zoomT = 0.6; yawT = Math.sin(t * 0.4) * 0.24; pitchT = 0.1; focusT = 1; }
            else if (gameOver) { zoomT = 0.82; focusT = 0.6; }
        }
        const k = 1 - Math.exp(-dt * 2.4);
        CAM.yaw += (yawT - CAM.yaw) * k; CAM.pitch += (pitchT - CAM.pitch) * k;
        CAM.zoom += (zoomT - CAM.zoom) * k; CAM.fx += (focusT - CAM.fx) * k;
        const tx = Lx + (rob.x - Lx) * CAM.fx, ty = Ly + (-rob.y + 110 - Ly) * CAM.fx, d = D * CAM.zoom;
        CAM.tx = tx; CAM.ty = ty;
        camera.position.set(tx + Math.sin(CAM.yaw) * Math.cos(CAM.pitch) * d, ty + Math.sin(CAM.pitch) * d, Math.cos(CAM.yaw) * Math.cos(CAM.pitch) * d);
        camera.up.set(0, 1, 0);
        camera.lookAt(tx, ty, 0);
        CAM.trauma = Math.max(0, CAM.trauma - dt * 1.3);
        const sh = CAM.trauma * CAM.trauma;
        if (sh > 0) {
            const n = (a, b) => Math.sin(t * a + b) * 0.6 + Math.sin(t * a * 2.3 + b * 1.7) * 0.4;
            camera.position.x += n(37, 1) * 22 * sh; camera.position.y += n(41, 4) * 18 * sh;
            camera.rotateZ(n(29, 7) * 0.035 * sh);
        }
    }

    // ---- performance panel: shift-P (or ?stats=1). GPU time per part from the timer query
    // extension where the browser has it (Chrome, Edge, Android Chrome; not Safari or
    // Firefox), else the time of render() with a gl.finish(), which waits for the GPU
    // (an upper bound, and only while the panel is open). Also the time between frames,
    // the JavaScript time of the 3D part, draw calls, triangles and the graphics chip.
    const PERF = { on: /[?&]stats=1/.test(location.search), el: null, ext: null, free: [], busy: [], acc: {}, n: 0, gaps: [], cpu: 0, fin: 0, at: 0, prev: 0, calls: 0, tris: 0, gpuName: '' };
    function perfInit() {
        const gl = renderer.getContext();
        PERF.gl = gl;
        PERF.ext = gl.getExtension('EXT_disjoint_timer_query_webgl2');
        const dbg = gl.getExtension('WEBGL_debug_renderer_info');
        PERF.gpuName = dbg ? gl.getParameter(dbg.UNMASKED_RENDERER_WEBGL) : gl.getParameter(gl.RENDERER);
        PERF.el = document.createElement('div'); PERF.el.id = 'perf3d';
        document.body.appendChild(PERF.el);
        PERF.el.style.display = PERF.on ? 'block' : 'none';
        window.addEventListener('keydown', e => { if (e.code === 'KeyO' && PERF.on) loadTest(); });
        PERF.el.addEventListener('pointerdown', e => { e.stopPropagation(); loadTest(); });
        window.addEventListener('keydown', e => { if (e.code === 'KeyP' && e.shiftKey && !e.repeat) VIEW.setPerf(!PERF.on); });
        renderer.info.autoReset = false;
    }
    // the full-load test (O, or a tap on the panel): the 3D picture drawn 3 x 20 times
    // straight after each other, waiting for the GPU each time; the middle of the three
    // is what one frame really costs this device (the GPU at full clock, which it is not
    // when the game asks for 60 frames and it could do many more). Takes a moment.
    function loadTest() {
        PERF.load = { ms: timeRenders(20, 3), fx: fxOn, sh: SHADOW.on, lv: currentLevel + 1, q: QUAL.name };
        PERF.at = 0;
    }
    let curQ = null;
    function gpuBegin(name) {
        if (!PERF.on || !PERF.ext) return;
        const gl = PERF.gl, q = PERF.free.pop() || gl.createQuery();
        gl.beginQuery(PERF.ext.TIME_ELAPSED_EXT, q); curQ = { q, name };
    }
    function gpuEnd() {
        if (!curQ) return;
        PERF.gl.endQuery(PERF.ext.TIME_ELAPSED_EXT); PERF.busy.push(curQ); curQ = null;
    }
    function gpuCollect() { // results come a frame or two later
        const gl = PERF.gl, ext = PERF.ext;
        if (!ext) return;
        const disjoint = gl.getParameter(ext.GPU_DISJOINT_EXT);
        while (PERF.busy.length && gl.getQueryParameter(PERF.busy[0].q, gl.QUERY_RESULT_AVAILABLE)) {
            const b = PERF.busy.shift(), ns = gl.getQueryParameter(b.q, gl.QUERY_RESULT);
            if (!disjoint) PERF.acc[b.name] = (PERF.acc[b.name] || 0) + ns / 1e6;
            if (b.name === 'post' || b.name === 'copy') PERF.gpuFrames = (PERF.gpuFrames || 0) + 1;
            PERF.free.push(b.q);
        }
    }
    function perfFrame(now, cpuMs) {
        if (PERF.prev) PERF.gaps.push(now - PERF.prev);
        PERF.prev = now; PERF.cpu += cpuMs; PERF.n++;
        PERF.calls += renderer.info.render.calls; PERF.tris += renderer.info.render.triangles;
        if (now - PERF.at < 1000 || !PERF.n) return;
        const g = PERF.gaps.slice().sort((a, b) => a - b), q = f => g.length ? g[Math.min(g.length - 1, Math.floor(f * g.length))] : 0;
        const fpsNow = g.length ? 1000 / (g.reduce((a, v) => a + v, 0) / g.length) : 0;
        const rows = [`<b>Performance</b> (shift-P) &nbsp; ${PERF.gpuName.replace(/[<>&]/g, '')}`,
            `picture ${renderer.domElement.width}x${renderer.domElement.height}${fxOn ? ', 4x MSAA + post-processing' : ', no fx'} &nbsp; ${fpsNow.toFixed(1)} fps`,
            `time between frames: median ${q(0.5).toFixed(1)} ms, 95% ${q(0.95).toFixed(1)} ms, longest ${q(1).toFixed(1)} ms`,
            `JavaScript 3D part: ${(PERF.cpu / PERF.n).toFixed(2)} ms per frame &nbsp; draw calls ${Math.round(PERF.calls / PERF.n)}, triangles ${Math.round(PERF.tris / PERF.n / 1000)}k`];
        if (PERF.ext) {
            const nf = PERF.gpuFrames || 1; let tot = 0;
            const parts = Object.entries(PERF.acc).map(([k, v]) => { tot += v / nf; return `${k} ${(v / nf).toFixed(2)}`; });
            rows.push(`GPU timer: ${tot.toFixed(2)} ms (${parts.join(', ')} ms) &nbsp;<i>at 60 fps the GPU clocks down, so this reads high</i>`);
        } else {
            rows.push(`GPU timer not available in this browser; render + waiting for the GPU: <b>${(PERF.fin / PERF.n).toFixed(2)} ms</b> (upper bound)`);
        }
        { // the screen's refresh rate (clock.js) and what is left of each refresh
            const hz = window.RRClock ? RRClock.hz : 60, per = 1000 / hz;
            rows.push(`screen ${hz.toFixed(0)} Hz, game 60 steps/s${window.RRClock && !RRClock.lock ? (RRClock.cap ? ', 3D 60 frames/s (ultra: every refresh)' : ' (in-between frames)') : ''}` +
                (PERF.ext ? '' : `: about ${Math.max(0, per - PERF.fin / PERF.n).toFixed(1)} ms left per frame (${per.toFixed(1)} ms)`));
        }
        if (PERF.load) { const L = PERF.load;
            rows.push(`<b>full load</b> (level ${L.lv}${L.fx ? ', fx' : ', no fx'}${L.sh ? ', shadows' : ', no shadows'}): <b>${L.ms.toFixed(2)} ms</b> per frame, up to ~${Math.floor(1000 / L.ms)} fps`);
        } else rows.push('measure full load: O or tap this panel');
        { const T0 = QUAL.test;
          rows.push(`dynamic resolution ${DYN.on ? 'on' : 'off'}: now ${Math.round(US.value * 100)}%, on average ${DYN.n ? Math.round(DYN.sum / DYN.n * 100) : 100}% of the pixels`);
          rows.push(`quality <b>${qualLabel()}</b> (Q): picture ${rt.width}x${rt.height}, ${rt.samples}x MSAA, shadows ${QUAL.p.shadow || 'off'}` +
            (T0 ? `<br>picked automatically at ${T0.hz.toFixed(0)} Hz: high ${T0.hi.toFixed(2)} ms, budget ${T0.budget.toFixed(1)} ms (half a frame) → ${QNAME[T0.pick] || T0.pick} ${T0.ms.toFixed(2)} ms` : '') +
            (QUAL.note ? `<br>${QUAL.note}` : '')); }
        rows.push(`shadows ${SHADOW.on ? 'on' : 'off'} (H) &nbsp; effects ${fxOn ? 'on' : 'off'} (F)`);
        PERF.el.innerHTML = rows.join('<br>');
        PERF.at = now; PERF.n = 0; PERF.cpu = 0; PERF.fin = 0; PERF.calls = 0; PERF.tris = 0; PERF.acc = {}; PERF.gpuFrames = 0; PERF.gaps = [];
    }

    // ---------- dynamic resolution ----------
    // While the camera moves fast (Rob at full speed, a launch, a shaking landing) a
    // softer picture does not show, so the scene is drawn smaller then: down to 70% of
    // the width and height (half the pixels), and back to full size when it calms down.
    // It follows the speed gradually, in steps of 2%, faster down than up. ?dyn=0 = off.
    const DYN = { on: !/[?&]dyn=0/.test(location.search), s: 1, speed: 0, min: 0.7, prev: new T.Vector3(), has: false, sum: 0, n: 0 };
    function dynStep(dt) {
        const cp = camera.position;
        if (DYN.has && dt > 0) {
            const v = cp.distanceTo(DYN.prev) / dt;
            DYN.speed += (Math.min(v, 6000) - DYN.speed) * Math.min(1, dt * 6);
        }
        DYN.prev.copy(cp); DYN.has = true;
        const k = DYN.on ? T.MathUtils.smoothstep(DYN.speed, 350, 1300) : 0;
        const want = 1 - (1 - DYN.min) * k;
        DYN.s += (want - DYN.s) * Math.min(1, dt * (want < DYN.s ? 5 : 2));
        const q = Math.min(1, Math.round(DYN.s * 50) / 50);
        US.value = q;
        rt.viewport.set(0, 0, Math.round(rt.width * q), Math.round(rt.height * q));
        DYN.sum += q * q; DYN.n++;
    }

    let frames = 0, fpsAt = performance.now(), fpsShown = 0;
    function render() {
        const c0 = performance.now();
        if (PERF.on) { renderer.info.autoReset = false; renderer.info.reset(); gpuCollect(); }
        const now = performance.now(), t = (now - t0) / 1000, dt = Math.min(0.05, t - last);
        if (last) qualWatch((t - last) * 1000);
        last = t;
        camRig(t, dt);
        dynStep(dt);
        shadowFollow();
        sky.position.copy(camera.position);
        for (const m of timed) m.uniforms.uTime.value = t;
        WIND.t.value = t;
        for (const u of updaters) u(t, dt);
        if (figs) syncFigures(t);
        if (fire) syncFire(t, dt);
        if (course) syncCourse(t, dt);
        if (soil && !fire) syncSoil(t, dt, null);
        sky.material.uniforms.uCamX.value = cameraX;
        if (dusk) syncDusk(dt);
        if (SHADOW.dl && SHADOW.main) { SHADOW.dl.color.copy(SHADOW.main.color); SHADOW.dl.intensity = SHADOW.main.intensity * SPLIT / (1 - SPLIT); }
        if (fxOn) {
            const f = fxNow, u = post.uniforms, v = new T.Vector3().copy(camera.position).addScaledVector(sunDirNow, 20000).project(camera);
            u.uTime.value = t; u.uSun.value.set(v.x * 0.5 + 0.5, v.y * 0.5 + 0.5);
            u.uSunVis.value = v.z < 1 ? T.MathUtils.smoothstep(1.7, 0.7, Math.max(Math.abs(v.x), Math.abs(v.y))) : 0;
            const dk = dusk ? dusk.now : 0; u.uDusk.value = dk;
            u.uRays.value = QUAL.p.rays ? (f.rays || 0) * (1 - 0.8 * dk) : 0; u.uBloom.value = QUAL.p.bloom ? f.bloom || 0 : 0; u.uHaze.value = (f.haze || 0) * (1 - 0.7 * dk);
            u.uCA.value = f.ca || 0; u.uScan.value = f.scan || 0;
            renderer.setRenderTarget(rt);
            gpuBegin('scene'); renderer.render(scene, camera); gpuEnd();
            u.uGrade.value = TX.level ? 1 : 0;
            u.uAO.value = QUAL.p.ao && TX.level && !/[?&]ao=0/.test(location.search) ? 1 : 0;
            if (u.uAO.value) {
                gpuBegin('contact shadow');
                const pass = (mat, target) => { postQuad.material = mat; renderer.setRenderTarget(target); renderer.render(postScene, postCam); };
                aoMat.uniforms.uInvProj.value.copy(camera.projectionMatrixInverse); aoMat.uniforms.uProjY.value = camera.projectionMatrix.elements[5];
                pass(aoMat, aoRT[0]);
                blurMat.uniforms.tSrc.value = aoRT[0].texture; blurMat.uniforms.uDir.value.set(1.2 / 960, 0); pass(blurMat, aoRT[1]);
                blurMat.uniforms.tSrc.value = aoRT[1].texture; blurMat.uniforms.uDir.value.set(0, 1.2 / 540); pass(blurMat, aoRT[0]);
                postQuad.material = post; u.tAO.value = aoRT[0].texture;
                gpuEnd();
            }
            const dl = SHADOW.dl, vq = QUAL.p.vol;
            u.uVol.value = vq && dl && SHADOW.on && dl.shadow.map ? (f.vol ?? f.rays ?? 0) * (1 + 0.6 * dk) : 0;
            if (u.uVol.value) {
                gpuBegin('light in the air');
                const vu = volMat.uniforms;
                vu.tShadow.value = dl.shadow.map.texture; vu.uShadowM.value.copy(dl.shadow.matrix);
                vu.uInvProj.value.copy(camera.projectionMatrixInverse); vu.uCamWorld.value.copy(camera.matrixWorld);
                vu.uCamPos.value.setFromMatrixPosition(camera.matrixWorld); vu.uRefY.value = CAM.ty - 300; vu.uTime.value = t;
                vu.uCloud.value = f.clouds ? 0.85 : 0.35;
                vu.uLight.value.copy(dl.position).sub(dl.target.position).normalize();
                u.uVolCol.value.copy(dl.color).multiplyScalar(dl.intensity * 0.9);
                const pass = (mat, target) => { postQuad.material = mat; renderer.setRenderTarget(target); renderer.render(postScene, postCam); };
                pass(volMat, volRT[0]);
                const W = volRT[0].width, H = volRT[0].height;
                blurMat.uniforms.tSrc.value = volRT[0].texture; blurMat.uniforms.uDir.value.set(1.5 / W, 0); pass(blurMat, volRT[1]);
                blurMat.uniforms.tSrc.value = volRT[1].texture; blurMat.uniforms.uDir.value.set(0, 1.5 / H); pass(blurMat, volRT[0]);
                postQuad.material = post; u.tVol.value = volRT[0].texture;
                gpuEnd();
            }
            if (u.uBloom.value) {
                gpuBegin('glow');
                const pass = (mat, target) => { postQuad.material = mat; renderer.setRenderTarget(target); renderer.render(postScene, postCam); };
                pass(brightMat, glowRT[0]);
                for (let k = 0; k < 2; k++) {
                    const sp = 1 + k * 1.5;
                    blurMat.uniforms.tSrc.value = glowRT[0].texture; blurMat.uniforms.uDir.value.set(sp / 480, 0); pass(blurMat, glowRT[1]);
                    blurMat.uniforms.tSrc.value = glowRT[1].texture; blurMat.uniforms.uDir.value.set(0, sp / 270); pass(blurMat, glowRT[0]);
                }
                postQuad.material = post;
                gpuEnd();
            }
            if (u.uRays.value > 0 && u.uSunVis.value > 0) {
                gpuBegin('sun rays'); postQuad.material = rayMat; renderer.setRenderTarget(rayRT); renderer.render(postScene, postCam); postQuad.material = post; gpuEnd();
            }
            renderer.setRenderTarget(null);
            gpuBegin('post'); renderer.render(postScene, postCam); gpuEnd();
        } else {
            renderer.setRenderTarget(rt);
            gpuBegin('scene (no fx)'); renderer.render(scene, camera); gpuEnd();
            gpuBegin('copy'); postQuad.material = copyMat; renderer.setRenderTarget(null); renderer.render(postScene, postCam); postQuad.material = post; gpuEnd();
        }
        if (PERF.on) {
            const c1 = performance.now();
            if (!PERF.ext) { gpuWait(PERF.gl); PERF.fin += performance.now() - c0; }
            perfFrame(now, c1 - c0);
        } else if (!renderer.info.autoReset) renderer.info.autoReset = true;
        frames++;
        if (now - fpsAt > 1000) { fpsShown = frames * 1000 / (now - fpsAt); frames = 0; fpsAt = now; }
    }

    try {
        init();
        window.RR3D = {
            figures: false, fire: false, course: false, soil: false,
            boom: (x, y) => { try { boom(x, y); } catch (e) { console.error('RR3D boom', e); } },
            confetti: (x, y) => { try { confettiBurst(x, y); } catch (e) { console.error('RR3D confetti', e); } },
            firework: (x, y) => { try { firework(x, y); } catch (e) { console.error('RR3D firework', e); } },
            build: (level) => { try { build(level); FLAT.built = level; } catch (e) { console.error('RR3D', e); window.RR3D = null; } },
            ghosts: (list) => { ghostList = list; },
            wall: (x) => { wallX = x; },
            render: () => { try { render(); } catch (e) { console.error('RR3D', e); window.RR3D = null; } },
            // where a game point (x, y, 2D coordinates) is on the 1920x1080 canvas with the
            // 3D camera as it is now, and how many canvas pixels r world pixels come to there
            debug: () => ({ scene, world, camera, renderer, SHADOW, QUAL, post, volMat, volRT, rt, DYN, US }), // for looking around from the console
            toScreen: (x, y, r = 0) => {
                const v = new T.Vector3(x, -y, 0).project(camera), w = new T.Vector3(x + r, -y, 0).project(camera);
                return { x: (v.x + 1) * 960, y: (1 - v.y) * 540, r: Math.hypot((w.x - v.x) * 960, (w.y - v.y) * 540) };
            },
        };
        // a module runs after the game's own script, which has loaded its first level already
        window.RR3D.build(currentLevel);
        // the quality setting: a fixed one at once, "auto" once the screen's refresh is known.
        // (Older pages kept the 2D view as a quality, "2d": now it is a view.)
        { let m = qualStored(), v = prefGet('view', TX.have ? 'tex' : '3d');
          const vq = /[?&]view=(2d|3d|tex)/.exec(location.search); if (vq) v = vq[1];
          if (/[?&]tex=0/.test(location.search) && v === 'tex') v = '3d';
          if (m === '2d') { m = 'auto'; v = '2d'; try { localStorage.setItem('rr3d.quality', 'auto'); } catch (e) {} }
          QUAL.mode = m;
          if (m === 'auto') setTimeout(() => { try { if (window.RR3D) qualAuto(); } catch (e) { console.error('RR3D quality', e); } }, 1500);
          else qualApply(m);
          if (m === 'auto' && v === '2d') qualApply('hoog');
          VIEW.setView(v, true); }
        window.RRView = VIEW;
    } catch (e) {
        console.error('RR3D: no WebGL', e);
    }
})();
