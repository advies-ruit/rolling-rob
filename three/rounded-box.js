// A box with rounded edges, standing in for three's RoundedBoxGeometry add-on
// (cdnjs carries only the core module). Same arguments: width, height, depth,
// segments per rounded edge, radius. Each vertex of a subdivided box is
// pulled onto the surface of the inner box grown by `radius`; its normal
// points away from the inner box, so the rounding shades smoothly.
class RoundedBoxGeometry extends THREE.BoxGeometry {
    constructor(width = 1, height = 1, depth = 1, segments = 2, radius = 0.1) {
        segments = Math.max(1, Math.floor(segments));
        const S = segments * 2 + 2;
        super(1, 1, 1, S, S, S);
        const half = [width / 2, height / 2, depth / 2];
        radius = Math.min(radius, half[0], half[1], half[2]);
        const P = this.attributes.position, N = this.attributes.normal;
        const p = [0, 0, 0], inner = [0, 0, 0], n = [0, 0, 0];
        for (let i = 0; i < P.count; i++) {
            p[0] = P.getX(i); p[1] = P.getY(i); p[2] = P.getZ(i);
            for (let a = 0; a < 3; a++) {
                // step k of S along this axis: `segments` steps across each
                // rounded band, two across the flat middle
                const k = Math.round((p[a] + 0.5) * S), h = half[a], r = radius;
                let c;
                if (k <= segments) c = -h + r * (k / segments);
                else if (k >= S - segments) c = h - r * ((S - k) / segments);
                else c = -h + r + (2 * h - 2 * r) * ((k - segments) / (S - 2 * segments));
                p[a] = c;
                inner[a] = Math.max(-(h - r), Math.min(h - r, c));
                n[a] = c - inner[a];
            }
            const L = Math.hypot(n[0], n[1], n[2]);
            if (L > 1e-9) {
                for (let a = 0; a < 3; a++) { n[a] /= L; p[a] = inner[a] + n[a] * radius; }
                N.setXYZ(i, n[0], n[1], n[2]);
            }
            P.setXYZ(i, p[0], p[1], p[2]);
        }
        P.needsUpdate = N.needsUpdate = true;
        this.computeBoundingSphere();
    }
}
