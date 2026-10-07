// W4M's land sampler (docs/w4m/formats.md §23): a solid cell's plan corners are cut when no other cell touches them, and with EdgeFalloff
// its top is rounded. Rebuilt as convex pieces, one per plan quadrant, corners numbered as a cell's (bit 1 +x, 2 +y, 4 +z).
type V3 = [f32; 3];

// W4M 0x81c098 / 0x81c0f8 (the table at 0x81c1b8 holds the same values): a vertex's top height by its neighbour count (A), an edge
// midpoint's by its two vertices' counts added (B), the cell centre's by half the four counts added (B)
const A: [f32; 8] = [0.0, 0.6, 0.75, 0.95, 1.0, 1.0, 1.0, 1.0];
const B: [f32; 16] = [0.0, 0.0, 0.6, 0.7, 0.8, 0.9, 0.95, 0.97, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0];

// W4M 0x467070: byte of the top vertex (x, y + 1, z) of cell layer y: bit k = the cell (x - kx, z - kz) of layer y + 1, bit 4 + k the same
// cell of layer y, k = kx + 2 kz. 0x445760 turns it into the count 0..4, or 7 when both layers have cells.
pub fn usage(solid: impl Fn(i32, i32, i32) -> bool, x: i32, y: i32, z: i32) -> u8 {
    (0..8).filter(|&b| solid(x - (b & 1), y + 1 - (b >> 2), z - (b >> 1 & 1))).fold(0, |m, b| m | 1 << b)
}
fn count(b: u8) -> usize { if b & 0xf != 0 && b & 0xf0 != 0 { 7 } else { b.count_ones() as usize } }

fn trilinear(c: &[V3; 8], p: V3) -> V3 {
    let w = |i: usize, t: f32| if i == 0 { 1.0 - t } else { t };
    (0..8).fold([0.0; 3], |s, n| {
        let k = w(n & 1, p[0]) * w(n >> 1 & 1, p[1]) * w(n >> 2, p[2]);
        [s[0] + c[n][0] * k, s[1] + c[n][1] * k, s[2] + c[n][2] * k]
    })
}

// v: usage bytes of the cell's top vertices (0,0) (1,0) (0,1) (1,1) in (x, z); c: the cell's 8 corners
pub fn pieces(c: &[V3; 8], v: [u8; 4], falloff: bool) -> Vec<[V3; 8]> {
    let n = v.map(count);
    // W4M 0x467dc0: a corner triangle (within 0.5 of the vertex, L1) is empty when the vertex's byte holds only its own column's bits
    let cut: Vec<bool> = (0..4).map(|k| v[k] & !(0x11 << k) == 0).collect();
    // top heights on the 3x3 plan grid: vertices, edge midpoints, centre
    let h = |i: usize, j: usize| -> f32 {
        if !falloff { return 1.0; }
        let at = |a: usize, b: usize| n[a / 2 + 2 * (b / 2)];
        match (i % 2, j % 2) {
            (0, 0) => A[at(i, j)],
            (1, 0) => B[at(i - 1, j) + at(i + 1, j)],
            (0, 1) => B[at(i, j - 1) + at(i, j + 1)],
            _ => B[n.iter().sum::<usize>() >> 1],
        }
    };
    if !cut.iter().any(|&c| c) && (0..9).all(|k| h(k % 3, k / 3) >= 1.0) { return vec![*c]; }
    (0..4).map(|q| {
        let (qx, qz) = (q & 1, q >> 1);
        // the quadrant's plan corners (x, z bits); a cut vertex corner folds onto its neighbour along x: the quad is the triangle left
        let corner = |m: usize| -> (usize, usize) {
            let (bx, bz) = (m & 1, m >> 1);
            if cut[q] && (bx, bz) == (qx, qz) { (qx ^ 1, qz) } else { (bx, bz) }
        };
        let mut o = [[0.0; 3]; 8];
        for (m, o) in o.iter_mut().enumerate() {
            let (bx, bz) = corner(m & 1 | m >> 2 << 1);
            let (gx, gz) = (qx + bx, qz + bz);
            let top = m >> 1 & 1 == 1;
            *o = trilinear(c, [gx as f32 * 0.5, if top { h(gx, gz) } else { 0.0 }, gz as f32 * 0.5]);
        }
        o
    }).collect()
}

#[cfg(test)]
mod tests {
    use super::*;
    const UNIT: [V3; 8] = [[0., 0., 0.], [1., 0., 0.], [0., 1., 0.], [1., 1., 0.], [0., 0., 1.], [1., 0., 1.], [0., 1., 1.], [1., 1., 1.]];
    fn cell(solid: impl Fn(i32, i32, i32) -> bool, falloff: bool) -> Vec<[V3; 8]> {
        let v = [(0, 0), (1, 0), (0, 1), (1, 1)].map(|(x, z)| usage(&solid, x, 0, z));
        pieces(&UNIT, v, falloff)
    }
    #[test]
    fn lone_cell() {
        let lone = |x: i32, y: i32, z: i32| (x, y, z) == (0, 0, 0);
        assert_eq!(cell(lone, false).len(), 4);
        // every vertex has one cell: A[1] = 0.6 at the cut corners' neighbours, the centre at B[(4) >> 1 = 2] = 0.6
        let top: f32 = cell(lone, true).iter().flat_map(|p| p.iter().filter(|c| c[1] > 0.0).map(|c| c[1])).fold(0.0, f32::max);
        assert!((top - 0.6).abs() < 1e-6, "{top}");
    }
    #[test]
    fn interior_cell_stays_whole() {
        let slab = |x: i32, y: i32, z: i32| (-1..=1).contains(&x) && y == 0 && (-1..=1).contains(&z);
        assert_eq!(cell(slab, true), vec![UNIT]);
    }
}
