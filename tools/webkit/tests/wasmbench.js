// wasmbench.js -- WebAssembly dans jsc (docs/WEBKIT.md, « WebAssembly ») : un module fait ici même, octet par
// octet (pas de .wasm à part), qui fait ce que fait une preuve de travail -- un hachage xorshift en boucle, écrit
// dans la mémoire du module --, contre la même boucle en JavaScript. Le JIT de WebAssembly (BBQ, OMG) ou
// l'interpréteur (IPInt) : jsc --useBBQJIT=false --useOMGJIT=false pour comparer dans un même binaire.
//   jsc SD0:/webkit/wasmbench.js
function leb(n) { const out = []; do { let b = n & 0x7f; n >>>= 7; if (n) b |= 0x80; out.push(b); } while (n); return out; }
function sleb(n) { const out = []; for (;;) { const b = n & 0x7f; n >>= 7; if ((n === 0 && !(b & 0x40)) || (n === -1 && (b & 0x40))) { out.push(b); return out; } out.push(b | 0x80); } }
function section(id, bytes) { return [id, ...leb(bytes.length), ...bytes]; }
function vec(items) { return [...leb(items.length), ...items.flat()]; }

const I32 = 0x7f, GET = 0x20, SET = 0x21, CONST = 0x41;
const body = [
	1, 2, I32,				// deux locaux i32 : i (1), h (2) ; le paramètre n (0)
	CONST, ...sleb(1), SET, 2,		// h = 1
	0x02, 0x40, 0x03, 0x40,			// block, loop
	GET, 1, GET, 0, 0x4f, 0x0d, 1,		// i >= n : sortir
	GET, 2, GET, 2, CONST, 13, 0x74, 0x73, SET, 2,	// h ^= h << 13
	GET, 2, GET, 2, CONST, 17, 0x76, 0x73, SET, 2,	// h ^= h >>> 17
	GET, 2, GET, 2, CONST, 5, 0x74, 0x73, SET, 2,	// h ^= h << 5
	GET, 1, CONST, ...sleb(4095), 0x71, CONST, 2, 0x74,	// adresse (i & 4095) * 4
	GET, 2, 0x36, 2, 0,			// i32.store h
	GET, 1, CONST, 1, 0x6a, SET, 1,		// i++
	0x0c, 0,				// recommencer
	0x0b, 0x0b,				// fin de loop, de block
	GET, 2, 0x0b,				// rendre h
];
const bytes = new Uint8Array([
	0x00, 0x61, 0x73, 0x6d, 1, 0, 0, 0,
	...section(1, vec([[0x60, 1, I32, 1, I32]])),		// (i32) -> i32
	...section(3, vec([[0]])),
	...section(5, vec([[0, 1]])),				// une page de mémoire
	...section(7, vec([[3, 0x72, 0x75, 0x6e, 0, 0]])),	// export "run"
	...section(10, vec([[...leb(body.length), ...body]])),
]);

const run = new WebAssembly.Instance(new WebAssembly.Module(bytes)).exports.run;
function runJS(n) {
	const mem = new Int32Array(4096);
	let h = 1;
	for (let i = 0; i < n; i++) {
		h ^= h << 13; h ^= h >>> 17; h ^= h << 5;
		mem[i & 4095] = h;
	}
	return h;
}

const N = 20000000;
run(1000); runJS(1000);			// (chauffe)
let t = Date.now(), r = run(N);
const wasm = Date.now() - t;
t = Date.now();
const rj = runJS(N);
const js = Date.now() - t;
print("wasmbench: " + N + " tours : WebAssembly " + wasm + " ms, JavaScript " + js + " ms, résultats " + (r === rj ? "égaux" : "DIFFÉRENTS " + r + " " + rj));
