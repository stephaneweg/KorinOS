// smoke.js -- essai de jsc sur ARMPI (docs/WEBKIT.md) : calcul, chaines, Date, JSON, Promise,
// expressions regulieres, Intl (ICU), classes, Map, tableaux types, minuteries.
//   jsc SD0:/webkit/smoke.js      -> derniere ligne : « smoke: ok (N checks) », code 0
let n = 0, bad = 0;
function check(name, got, want) {
    n++;
    if (got !== want) { bad++; print("FAIL " + name + ": " + got + " != " + want); }
}
function fib(k) { return k < 2 ? k : fib(k - 1) + fib(k - 2); }
check("fib", fib(20), 6765);
check("arith", (0.1 + 0.2).toFixed(2) + "/" + (2 ** 53) + "/" + (7 % -3), "0.30/9007199254740992/1");
check("bigint", (2n ** 100n).toString(), "1267650600228229401496703205376");
check("math", Math.round(Math.sqrt(2) * 1e6), 1414214);
let s = "Bonjour, ARMPI";
check("string", s.toUpperCase().split(", ").reverse().join("|"), "ARMPI|BONJOUR");
check("unicode", "é€😀".length + ":" + [..."é€😀"].length, "4:3");
check("normalize", "é".normalize("NFC") === "é", true);
check("template", `${1 + 1}-${"x".repeat(3)}`, "2-xxx");
check("regexp", "2026-10-29".replace(/(\d+)-(\d+)-(\d+)/, "$3/$2/$1"), "29/10/2026");
check("regexp-named", /(?<a>\w+)@(?<b>\w+)/.exec("moi@armpi").groups.b, "armpi");
let d = new Date(Date.UTC(2026, 9, 29, 12, 34, 56));
check("date-iso", d.toISOString(), "2026-10-29T12:34:56.000Z");
check("date-parse", Date.parse("2026-10-29T12:34:56Z"), d.getTime());
check("date-now", Date.now() > Date.UTC(2020, 0, 1), true);
let o = { a: [1, 2, { b: "c" }], d: null, e: 1.5 };
check("json", JSON.stringify(JSON.parse(JSON.stringify(o))), '{"a":[1,2,{"b":"c"}],"d":null,"e":1.5}');
check("intl-number", new Intl.NumberFormat("fr-FR").format(1234567.891), "1 234 567,891");
check("intl-date", new Intl.DateTimeFormat("en-US", { timeZone: "UTC", dateStyle: "long" }).format(d), "October 29, 2026");
check("collator", ["é", "e", "f"].sort(new Intl.Collator("fr").compare).join(""), "eéf");
class A { #x = 2; get x() { return this.#x; } static make() { return new A(); } }
check("class", A.make().x, 2);
let m = new Map(); for (let i = 0; i < 1000; i++) m.set("k" + i, i);
check("map", m.get("k999") + m.size, 1999);
let ta = new Float64Array(1000); for (let i = 0; i < ta.length; i++) ta[i] = i / 2;
check("typedarray", ta.reduce((a, b) => a + b, 0), 249750);
let arr = []; for (let i = 0; i < 100000; i++) arr.push({ i });
check("alloc", arr.length + arr[99999].i, 199999);
check("sort", [5, 3, 9, 1].sort((a, b) => a - b).join(), "1,3,5,9");
function* gen() { yield 1; yield 2; yield 3; }
check("generator", [...gen()].join(""), "123");
check("destructure", (({ a: [x, , { b }] }) => x + b)(o), "1c");

let order = [];
Promise.resolve(1).then(v => order.push("p" + v));
(async () => { await null; order.push("async"); })();
order.push("sync");
let t0 = Date.now();
setTimeout(() => {
    check("microtasks", order.join(","), "sync,p1,async");
    check("timer", Date.now() - t0 >= 40, true);
    new Promise((res, rej) => rej(new Error("x"))).catch(e => {
        check("reject", e.message, "x");
        print(bad ? "smoke: " + bad + " FAIL / " + n : "smoke: ok (" + n + " checks)");
        if (bad) throw new Error("smoke failed");
    });
}, 50);
