// jitbench.js -- le JIT de JavaScriptCore sur Korin (docs/etude/PC-PILOTES.md, "Jet pour le PC") : des boucles
// numeriques et des objets, assez longues pour que le DFG et le FTL les prennent. Une ligne par essai, en ms,
// puis le total. A comparer :
//   jsc /webkit/jitbench.js                      (le JIT : de base, DFG, FTL)
//   jsc --useJIT=false /webkit/jitbench.js       (le LLInt seul ; ou JSC_useJIT=false dans l'environnement)
//   jsc --useFTLJIT=false /webkit/jitbench.js    (sans le FTL)
let total = 0;
function t(name, f) {
	let a = Date.now();
	let r = f();
	let ms = Date.now() - a;
	total += ms;
	print(name + ": " + ms + " ms (" + r + ")");
}
t("fib(27)", () => { function fib(k) { return k < 2 ? k : fib(k - 1) + fib(k - 2); } return fib(27); });
t("20M additions", () => { let s = 0; for (let i = 0; i < 20000000; i++) s = (s + i) | 0; return s; });
t("sieve 2M x 3", () => {
	let n = 0;
	for (let k = 0; k < 3; k++) {
		let a = new Uint8Array(2000000);
		n = 0;
		for (let i = 2; i < a.length; i++) {
			if (a[i]) continue;
			n++;
			for (let j = i * 2; j < a.length; j += i) a[j] = 1;
		}
	}
	return n;
});
t("mandelbrot 300x300", () => {
	let c = 0;
	for (let y = 0; y < 300; y++)
		for (let x = 0; x < 300; x++) {
			let cr = x / 150 - 1.5, ci = y / 150 - 1, zr = 0, zi = 0, i = 0;
			for (; i < 200 && zr * zr + zi * zi < 4; i++) { let t = zr * zr - zi * zi + cr; zi = 2 * zr * zi + ci; zr = t; }
			if (i == 200) c++;
		}
	return c;
});
t("nbody 200k steps", () => {
	let b = [];
	for (let i = 0; i < 5; i++) b.push({ x: i, y: i * 0.5, z: -i, vx: 0, vy: 0.1 * i, vz: 0, m: 1 + i });
	for (let s = 0; s < 200000; s++) {
		for (let i = 0; i < 5; i++) for (let j = i + 1; j < 5; j++) {
			let p = b[i], q = b[j], dx = p.x - q.x, dy = p.y - q.y, dz = p.z - q.z;
			let d2 = dx * dx + dy * dy + dz * dz + 0.01, mag = 0.001 / (d2 * Math.sqrt(d2));
			p.vx -= dx * q.m * mag; p.vy -= dy * q.m * mag; p.vz -= dz * q.m * mag;
			q.vx += dx * p.m * mag; q.vy += dy * p.m * mag; q.vz += dz * p.m * mag;
		}
		for (let i = 0; i < 5; i++) { let p = b[i]; p.x += 0.001 * p.vx; p.y += 0.001 * p.vy; p.z += 0.001 * p.vz; }
	}
	return b[0].x.toFixed(6);
});
t("objects 1M", () => { let s = 0; for (let i = 0; i < 1000000; i++) { let o = { a: i, b: i * 2 }; s += o.a + o.b; } return s; });
t("sort 200k numbers", () => { let a = []; for (let i = 0; i < 200000; i++) a.push((i * 7919) % 200003); a.sort((x, y) => x - y); return a[100]; });
t("string build 300k", () => { let s = ""; for (let i = 0; i < 300000; i++) s += String.fromCharCode(97 + i % 26); return s.length; });
print("total: " + total + " ms");
