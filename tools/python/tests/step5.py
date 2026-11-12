"""Essais de l'etape 5 de Python sous Korin (docs/PYTHON.md §9, docs/PYTHON-PORT.md « Étape 5 ») :
les modules Rust (pyo3) en .so : pydantic-core, rpds-py, cryptography, dans le meme process.

    python3 /home/pytest/step5.py

Chaque essai affiche OK, ECHEC ou SAUTE avec la raison ; le code de sortie est le nombre d'echecs.
"""
import os
import sys
import time
import traceback

results = []


def check(name):
    def deco(fn):
        t = time.monotonic()
        try:
            r = fn()
            results.append(("OK", name, ("" if r is None else str(r)) + " [%.1f s]" % (time.monotonic() - t)))
        except Exception as e:
            results.append(("ECHEC", name, "%s: %s" % (type(e).__name__, e)))
            traceback.print_exc()
        sys.stdout.flush()
        return fn
    return deco


@check("import pydantic_core (.so Rust)")
def _():
    t = time.monotonic()
    import pydantic_core
    from pydantic_core import _pydantic_core
    assert _pydantic_core.__file__.endswith(".so"), _pydantic_core.__file__
    return "%s %s, import %.1f s" % (pydantic_core.__version__, _pydantic_core.__file__, time.monotonic() - t)


@check("pydantic.TypeAdapter(list[int]).validate_json('[1,2]')")
def _():
    import pydantic
    v = pydantic.TypeAdapter(list[int]).validate_json("[1,2]")
    assert v == [1, 2], v
    return "pydantic %s : %r" % (pydantic.VERSION, v)


@check("un modele pydantic, et son erreur de validation")
def _():
    from pydantic import BaseModel, Field, ValidationError

    class User(BaseModel):
        name: str
        age: int = Field(ge=0)
        tags: list[str] = []

    u = User.model_validate_json('{"name": "Korin", "age": "7", "tags": ["os"]}')
    assert u.age == 7 and u.tags == ["os"], u
    assert User.model_validate_json(u.model_dump_json()) == u
    try:
        User(name="x", age=-1)
    except ValidationError as e:
        err = e.errors()
        assert err[0]["type"] == "greater_than_equal" and err[0]["loc"] == ("age",), err
        return "%r ; erreur : %s" % (u, err[0]["msg"])
    raise AssertionError("pas d'erreur de validation")


@check("import rpds (.so Rust)")
def _():
    import rpds
    m = rpds.HashTrieMap({"a": 1}).insert("b", 2)
    s = rpds.HashTrieSet([1, 2]).insert(3)
    l = rpds.List([1, 2]).push_front(0)
    assert m["b"] == 2 and len(s) == 3 and list(l) == [0, 1, 2]
    return "%s : %r %r %r" % (rpds.rpds.__file__, m, s, l)


@check("jsonschema.validate (referencing, rpds)")
def _():
    import jsonschema
    schema = {"type": "object", "properties": {"n": {"type": "integer", "minimum": 1},
              "s": {"$ref": "#/$defs/s"}}, "required": ["n"], "$defs": {"s": {"type": "string"}}}
    jsonschema.validate({"n": 3, "s": "x"}, schema)
    try:
        jsonschema.validate({"n": 0, "s": 1}, schema)
    except jsonschema.ValidationError as e:
        return "valide, puis refus : %s" % e.message
    raise AssertionError("pas d'erreur")


@check("import cryptography (.so Rust, OpenSSL de python3)")
def _():
    import cryptography
    from cryptography.hazmat.bindings import _rust
    from cryptography.hazmat.backends.openssl import backend
    assert _rust.__file__.endswith(".so"), _rust.__file__
    return "%s %s, %s" % (cryptography.__version__, _rust.__file__, backend.openssl_version_text())


@check("cryptography : SHA-256")
def _():
    from cryptography.hazmat.primitives import hashes
    h = hashes.Hash(hashes.SHA256())
    h.update(b"korin")
    d = h.finalize().hex()
    import hashlib
    assert d == hashlib.sha256(b"korin").hexdigest(), d
    return d


@check("cryptography : signature Ed25519")
def _():
    from cryptography.exceptions import InvalidSignature
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
    k = Ed25519PrivateKey.generate()
    sig = k.sign(b"message")
    k.public_key().verify(sig, b"message")
    try:
        k.public_key().verify(sig, b"autre")
    except InvalidSignature:
        return "signature de %d octets, verifiee, fausse refusee" % len(sig)
    raise AssertionError("fausse signature acceptee")


@check("cryptography : AES-GCM, X.509 auto-signe")
def _():
    import datetime
    from cryptography import x509
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM
    from cryptography.x509.oid import NameOID
    key = AESGCM.generate_key(bit_length=256)
    ct = AESGCM(key).encrypt(b"\0" * 12, b"secret", None)
    assert AESGCM(key).decrypt(b"\0" * 12, ct, None) == b"secret"
    k = ec.generate_private_key(ec.SECP256R1())
    n = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "korin")])
    now = datetime.datetime.now(datetime.timezone.utc)
    c = (x509.CertificateBuilder().subject_name(n).issuer_name(n).public_key(k.public_key())
         .serial_number(1).not_valid_before(now).not_valid_after(now + datetime.timedelta(days=1))
         .sign(k, hashes.SHA256()))
    c2 = x509.load_pem_x509_certificate(c.public_bytes(__import__("cryptography.hazmat.primitives.serialization",
                                                                   fromlist=["x"]).Encoding.PEM))
    assert c2.subject == n
    return "certificat %s" % c2.subject.rfc4514_string()


@check("un JWT par joserfc (ES256, EdDSA, HS256)")
def _():
    from joserfc import jwt
    from joserfc.errors import BadSignatureError
    from joserfc.jwk import ECKey, OctKey, OKPKey
    k = ECKey.generate_key("P-256")
    tok = jwt.encode({"alg": "ES256"}, {"sub": "korin"}, k)
    assert jwt.decode(tok, k).claims["sub"] == "korin"
    try:
        jwt.decode(tok, ECKey.generate_key("P-256"))
    except BadSignatureError:
        pass
    else:
        raise AssertionError("mauvaise cle acceptee")
    e = OKPKey.generate_key("Ed25519")      # EdDSA : hors des algorithmes recommandes, a permettre
    t = jwt.decode(jwt.encode({"alg": "EdDSA"}, {"n": 2}, e, algorithms=["EdDSA"]), e, algorithms=["EdDSA"])
    assert t.claims["n"] == 2
    o = OctKey.generate_key(256)
    assert jwt.decode(jwt.encode({"alg": "HS256"}, {"n": 1}, o), o).claims["n"] == 1
    return tok[:40] + "..."


@check("les trois modules Rust dans le meme process")
def _():
    names = ("pydantic_core._pydantic_core", "rpds.rpds", "cryptography.hazmat.bindings._rust")
    files = [sys.modules[n].__file__ for n in names]
    assert all(f.endswith(".so") for f in files), files
    # tout marche encore apres les trois chargements, dans des threads
    import threading
    import pydantic, rpds
    from cryptography.hazmat.primitives import hashes
    out = []

    def work(i):
        h = hashes.Hash(hashes.SHA256()); h.update(b"%d" % i)
        out.append((pydantic.TypeAdapter(int).validate_python(str(i)), len(rpds.List(range(i))),
                    h.finalize()[:1]))
    th = [threading.Thread(target=work, args=(i,)) for i in range(8)]
    for x in th: x.start()
    for x in th: x.join()
    assert sorted(o[0] for o in out) == list(range(8)), out
    return "%d threads ; %s" % (len(out), ", ".join(os.path.basename(f) for f in files))


@check("une panique Rust devient une exception Python")
def _():
    # une exception Python a travers le Rust (pydantic-core, rpds), puis un vrai panic! de Rust :
    # korin_panic_probe (tools/python/tests/panic), une cdylib pyo3 qui panique ; pyo3 la rattrape
    # (catch_unwind, le derouleur de l'hote) et leve PanicException
    from pydantic_core import PydanticCustomError, SchemaValidator, core_schema

    def boom(v):
        raise PydanticCustomError("boom", "boom {x}", {"x": 1})
    sv = SchemaValidator(core_schema.no_info_plain_validator_function(boom))
    try:
        sv.validate_python(1)
    except Exception as e:
        first = type(e).__name__
    else:
        raise AssertionError("pas d'exception")
    import rpds

    class Bad:
        def __hash__(self):
            raise RuntimeError("hash")
    try:
        rpds.HashTrieSet([Bad()])
    except RuntimeError:
        second = "RuntimeError"
    import korin_panic_probe
    try:
        korin_panic_probe.panic("korin")
    except BaseException as e:
        assert type(e).__name__ == "PanicException", type(e)
        third = "%s(%s)" % (type(e).__name__, e)
    else:
        raise AssertionError("la panique n'est pas remontee")
    assert korin_panic_probe.caught() == "rattrapée : true", korin_panic_probe.caught()
    return "%s ; %s ; panic! -> %s ; catch_unwind dans le module" % (first, second, third)


for st, name, info in results:
    print("%-6s %s : %s" % (st, name, info))
fails = sum(1 for r in results if r[0] == "ECHEC")
print("%d reussis, %d echecs" % (sum(1 for r in results if r[0] == "OK"), fails))
sys.exit(fails)
