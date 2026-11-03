// bench.js -- mesure grossiere de jsc sur ARMPI (docs/WEBKIT.md) : une ligne par essai, en ms.
//   jsc SD0:/webkit/bench.js
function t(name, f) { let a = Date.now(); let r = f(); print(name + ": " + (Date.now() - a) + " ms (" + r + ")"); }
t("fib(25)", () => { function fib(k) { return k < 2 ? k : fib(k - 1) + fib(k - 2); } return fib(25); });
t("2M additions", () => { let s = 0; for (let i = 0; i < 2000000; i++) s += i; return s; });
t("100k objects", () => { let a = []; for (let i = 0; i < 100000; i++) a.push({ i, s: "x" + i }); return a.length; });
t("30k string concat/split", () => { let s = ""; for (let i = 0; i < 30000; i++) s += i + ","; return s.split(",").length; });
t("sort 30k numbers", () => { let a = []; for (let i = 0; i < 30000; i++) a.push((i * 7919) % 30011); a.sort((x, y) => x - y); return a[100]; });
t("regexp 10k", () => { let n = 0, re = /(\d+)-(\w+)/; for (let i = 0; i < 10000; i++) if (re.test(i + "-abc")) n++; return n; });
t("JSON 2 x 200 KB", () => { let o = []; for (let i = 0; i < 4000; i++) o.push({ id: i, name: "item" + i, tags: ["a", "b"] }); let n = 0; for (let k = 0; k < 2; k++) n += JSON.parse(JSON.stringify(o)).length; return n; });
