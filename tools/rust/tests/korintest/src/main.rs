//! korintest : les essais d'acceptation de la cible aarch64-unknown-korin (docs/RUST.md).
//!     korintest            tous les groupes
//!     korintest fs [dir]   un groupe (hello, env, threads, sync, fs, cmd, tcp, udp, panic, time)
//! Chaque essai affiche « ok » ou « ÉCHEC » ; le code de sortie est le nombre d'échecs.

use std::cell::{Cell, RefCell};
use std::collections::HashMap;
use std::io::{Read, Seek, SeekFrom, Write};
use std::net::{TcpListener, TcpStream, UdpSocket};
use std::panic;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::mpsc;
use std::sync::{Arc, Barrier, Condvar, Mutex, OnceLock, RwLock};
use std::time::{Duration, Instant, SystemTime};
use std::{env, fs, thread};

static FAILS: AtomicUsize = AtomicUsize::new(0);

fn check(name: &str, r: Result<(), String>) {
    match r {
        Ok(()) => println!("  ok      {name}"),
        Err(e) => {
            FAILS.fetch_add(1, Ordering::SeqCst);
            println!("  ÉCHEC   {name} : {e}");
        }
    }
}

macro_rules! ensure {
    ($c:expr, $($fmt:tt)*) => {
        if !$c {
            return Err(format!($($fmt)*));
        }
    };
}

fn e<E: std::fmt::Display>(x: E) -> String {
    x.to_string()
}

fn hello() {
    println!("hello from Rust on {} ({})", env::consts::OS, env::consts::ARCH);
    check("println, consts::OS", (|| {
        ensure!(env::consts::OS == "korin", "OS = {}", env::consts::OS);
        ensure!(env::consts::FAMILY == "unix", "FAMILY = {}", env::consts::FAMILY);
        Ok(())
    })());
    check("format!, Vec, HashMap (aléa de RandomState)", (|| {
        let mut m = HashMap::new();
        for i in 0..1000 {
            m.insert(format!("k{i}"), i * 2);
        }
        ensure!(m["k999"] == 1998, "valeur");
        let v: Vec<u64> = (1..=100).collect();
        ensure!(v.iter().sum::<u64>() == 5050, "somme");
        Ok(())
    })());
    check("stderr", (|| {
        eprintln!("  (stderr ok)");
        std::io::stderr().flush().map_err(e)
    })());
}

fn envs() {
    check("args", (|| {
        let a: Vec<String> = env::args().collect();
        ensure!(!a.is_empty(), "argv vide");
        println!("    argv = {a:?}");
        Ok(())
    })());
    check("set_var / var / vars / remove_var", (|| {
        unsafe { env::set_var("KORIN_RUST_ESSAI", "été 42") };
        ensure!(env::var("KORIN_RUST_ESSAI").map_err(e)? == "été 42", "valeur");
        ensure!(env::vars().any(|(k, _)| k == "KORIN_RUST_ESSAI"), "absente de vars()");
        unsafe { env::remove_var("KORIN_RUST_ESSAI") };
        ensure!(env::var("KORIN_RUST_ESSAI").is_err(), "pas retirée");
        Ok(())
    })());
    check("current_dir, current_exe, temp_dir, home_dir", (|| {
        let cwd = env::current_dir().map_err(e)?;
        let exe = env::current_exe().map_err(e)?;
        println!("    cwd = {}, exe = {}, tmp = {}, home = {:?}", cwd.display(), exe.display(),
                 env::temp_dir().display(), env::home_dir());
        // l'arbre POSIX des chemins (étape 6, docs/POSIX.md) : des chemins absolus pour Path
        ensure!(cwd.is_absolute() && exe.is_absolute(), "pas absolus : {} {}", cwd.display(), exe.display());
        ensure!(env::home_dir().is_some_and(|h| h.is_absolute()), "HOME pas absolu");
        Ok(())
    })());
    check("available_parallelism", (|| {
        let n = thread::available_parallelism().map_err(e)?;
        println!("    {n} cœurs");
        Ok(())
    })());
}

fn time() {
    check("Instant, sleep 50 ms", (|| {
        let t = Instant::now();
        thread::sleep(Duration::from_millis(50));
        let d = t.elapsed();
        ensure!(d >= Duration::from_millis(50) && d < Duration::from_secs(2), "durée {d:?}");
        Ok(())
    })());
    check("SystemTime après 2026", (|| {
        let s = SystemTime::now().duration_since(SystemTime::UNIX_EPOCH).map_err(e)?;
        println!("    {} s depuis 1970", s.as_secs());
        ensure!(s.as_secs() > 1_767_225_600, "horloge en 1970 ?");
        Ok(())
    })());
}

thread_local! {
    static TLS: Cell<u32> = const { Cell::new(0) };
    static TLS_DROP: RefCell<Option<Dropper>> = const { RefCell::new(None) };
}
static DROPPED: AtomicUsize = AtomicUsize::new(0);
struct Dropper;
impl Drop for Dropper {
    fn drop(&mut self) {
        DROPPED.fetch_add(1, Ordering::SeqCst);
    }
}

fn threads() {
    check("8 threads, Arc<Mutex>", (|| {
        let c = Arc::new(Mutex::new(0u64));
        let hs: Vec<_> = (0..8)
            .map(|i| {
                let c = c.clone();
                thread::Builder::new()
                    .name(format!("t{i}"))
                    .spawn(move || {
                        for _ in 0..10_000 {
                            *c.lock().unwrap() += 1;
                        }
                        thread::current().name().map(|s| s.to_owned())
                    })
                    .unwrap()
            })
            .collect();
        for (i, h) in hs.into_iter().enumerate() {
            let n = h.join().map_err(|_| "join".to_string())?;
            ensure!(n.as_deref() == Some(format!("t{i}").as_str()), "nom {n:?}");
        }
        ensure!(*c.lock().unwrap() == 80_000, "compte {}", *c.lock().unwrap());
        Ok(())
    })());
    check("thread_local! par thread, destructeurs à la fin du thread", (|| {
        TLS.with(|t| t.set(7));
        let before = DROPPED.load(Ordering::SeqCst);
        let hs: Vec<_> = (0..4)
            .map(|i| {
                thread::spawn(move || {
                    TLS_DROP.with(|d| *d.borrow_mut() = Some(Dropper));
                    TLS.with(|t| {
                        ensure!(t.get() == 0, "valeur héritée {}", t.get());
                        t.set(i);
                        Ok(())
                    })?;
                    thread::sleep(Duration::from_millis(10));
                    TLS.with(|t| {
                        ensure!(t.get() == i, "valeur {}", t.get());
                        Ok(())
                    })
                })
            })
            .collect();
        for h in hs {
            h.join().map_err(|_| "panique".to_string())??;
        }
        ensure!(TLS.with(|t| t.get()) == 7, "thread principal");
        ensure!(DROPPED.load(Ordering::SeqCst) - before == 4, "destructeurs : {}",
                DROPPED.load(Ordering::SeqCst) - before);
        Ok(())
    })());
    check("thread::scope", (|| {
        let mut v = vec![1, 2, 3, 4];
        thread::scope(|s| {
            for x in v.iter_mut() {
                s.spawn(move || *x *= 10);
            }
        });
        ensure!(v == [10, 20, 30, 40], "{v:?}");
        Ok(())
    })());
    check("pile de 8 Mo (récursion profonde dans un thread)", (|| {
        fn rec(n: u32, buf: &mut [u8; 1024]) -> u32 {
            buf[0] = n as u8;
            if n == 0 { 0 } else { let mut b = [0u8; 1024]; rec(n - 1, &mut b) + 1 + buf[0] as u32 * 0 }
        }
        let h = thread::Builder::new().stack_size(8 << 20).spawn(|| rec(4000, &mut [0; 1024]))
            .map_err(e)?;
        ensure!(h.join().map_err(|_| "panique".to_string())? == 4000, "profondeur");
        Ok(())
    })());
    check("park / unpark", (|| {
        let t = thread::current();
        let h = thread::spawn(move || {
            thread::sleep(Duration::from_millis(20));
            t.unpark();
        });
        thread::park_timeout(Duration::from_secs(5));
        h.join().map_err(|_| "join".to_string())
    })());
}

fn sync() {
    check("mpsc : 4 producteurs, 4000 messages", (|| {
        let (tx, rx) = mpsc::channel();
        for p in 0..4 {
            let tx = tx.clone();
            thread::spawn(move || {
                for i in 0..1000 {
                    tx.send(p * 1000 + i).unwrap();
                }
            });
        }
        drop(tx);
        let mut v: Vec<i32> = rx.iter().collect();
        v.sort();
        ensure!(v.len() == 4000 && v[3999] == 3999, "reçus {}", v.len());
        Ok(())
    })());
    check("sync_channel(0) (rendez-vous)", (|| {
        let (tx, rx) = mpsc::sync_channel(0);
        let h = thread::spawn(move || (0..100).for_each(|i| tx.send(i).unwrap()));
        let s: i32 = rx.iter().sum();
        h.join().map_err(|_| "join".to_string())?;
        ensure!(s == 4950, "somme {s}");
        Ok(())
    })());
    check("Condvar, wait_timeout", (|| {
        let p = Arc::new((Mutex::new(false), Condvar::new()));
        let p2 = p.clone();
        thread::spawn(move || {
            thread::sleep(Duration::from_millis(30));
            *p2.0.lock().unwrap() = true;
            p2.1.notify_all();
        });
        let mut g = p.0.lock().unwrap();
        while !*g {
            g = p.1.wait(g).unwrap();
        }
        let (_g, r) = p.1.wait_timeout(g, Duration::from_millis(50)).unwrap();
        ensure!(r.timed_out(), "pas de délai écoulé");
        Ok(())
    })());
    check("RwLock, Barrier, OnceLock", (|| {
        let l = Arc::new(RwLock::new(0));
        let b = Arc::new(Barrier::new(4));
        static ONCE: OnceLock<u32> = OnceLock::new();
        let hs: Vec<_> = (0..4)
            .map(|i| {
                let (l, b) = (l.clone(), b.clone());
                thread::spawn(move || {
                    ONCE.get_or_init(|| 42);
                    b.wait();
                    *l.write().unwrap() += i;
                    let _r = l.read().unwrap();
                })
            })
            .collect();
        for h in hs {
            h.join().map_err(|_| "join".to_string())?;
        }
        ensure!(*l.read().unwrap() == 6 && ONCE.get() == Some(&42), "valeurs");
        Ok(())
    })());
}

fn fs_tests(base: &Path) {
    let d = base.join(format!("korintest-{}", std::process::id()));
    let _ = fs::remove_dir_all(&d);
    check("create_dir_all", fs::create_dir_all(d.join("a/b")).map_err(e));
    let f = d.join("a/b/f.txt");
    check("write / read_to_string", (|| {
        fs::write(&f, "bonjour\n").map_err(e)?;
        ensure!(fs::read_to_string(&f).map_err(e)? == "bonjour\n", "contenu");
        Ok(())
    })());
    check("append, seek, set_len", (|| {
        let mut h = fs::OpenOptions::new().append(true).open(&f).map_err(e)?;
        h.write_all(b"suite\n").map_err(e)?;
        drop(h);
        let mut h = fs::OpenOptions::new().read(true).write(true).open(&f).map_err(e)?;
        h.seek(SeekFrom::Start(3)).map_err(e)?;
        let mut s = String::new();
        h.read_to_string(&mut s).map_err(e)?;
        ensure!(s == "jour\nsuite\n", "lu {s:?}");
        h.set_len(3).map_err(e)?;
        drop(h);
        ensure!(fs::read(&f).map_err(e)? == b"bon", "après set_len");
        Ok(())
    })());
    check("metadata : taille, type, date", (|| {
        let m = fs::metadata(&f).map_err(e)?;
        ensure!(m.len() == 3 && m.is_file() && !m.is_dir(), "{m:?}");
        let age = SystemTime::now().duration_since(m.modified().map_err(e)?).unwrap_or_default();
        ensure!(age < Duration::from_secs(120), "date {age:?}");
        ensure!(fs::metadata(d.join("a")).map_err(e)?.is_dir(), "dossier");
        Ok(())
    })());
    check("read_dir", (|| {
        for i in 0..5 {
            fs::write(d.join(format!("a/n{i}")), [i as u8]).map_err(e)?;
        }
        let mut names: Vec<String> = fs::read_dir(d.join("a"))
            .map_err(e)?
            .map(|x| x.map(|x| x.file_name().to_string_lossy().into_owned()))
            .collect::<Result<_, _>>()
            .map_err(e)?;
        names.sort();
        ensure!(names == ["b", "n0", "n1", "n2", "n3", "n4"], "{names:?}");
        let ft = fs::read_dir(d.join("a")).map_err(e)?.next().unwrap().map_err(e)?.file_type()
            .map_err(e)?;
        ensure!(ft.is_dir() || ft.is_file(), "type");
        Ok(())
    })());
    check("rename, copy, remove_file", (|| {
        let g = d.join("a/g.txt");
        fs::rename(&f, &g).map_err(e)?;
        ensure!(!f.exists() && g.exists(), "rename");
        fs::copy(&g, &f).map_err(e)?;
        ensure!(fs::read(&f).map_err(e)? == b"bon", "copy");
        fs::remove_file(&g).map_err(e)?;
        ensure!(!g.exists(), "remove_file");
        Ok(())
    })());
    check("erreurs : NotFound, AlreadyExists", (|| {
        let r = fs::read(d.join("absent"));
        ensure!(matches!(&r, Err(x) if x.kind() == std::io::ErrorKind::NotFound), "{r:?}");
        let r = fs::create_dir(d.join("a"));
        ensure!(matches!(&r, Err(x) if x.kind() == std::io::ErrorKind::AlreadyExists), "{r:?}");
        Ok(())
    })());
    check("canonicalize", (|| {
        let c = fs::canonicalize(d.join("a/b/../b")).map_err(e)?;
        println!("    {}", c.display());
        ensure!(c.ends_with("a/b"), "{}", c.display());
        Ok(())
    })());
    check("remove_dir_all", (|| {
        fs::remove_dir_all(&d).map_err(e)?;
        ensure!(!d.exists(), "reste");
        Ok(())
    })());
}

fn cmd() {
    check("Command::new(\"ls\").output()", (|| {
        let o = Command::new("ls").arg("/").output().map_err(e)?;
        let s = String::from_utf8_lossy(&o.stdout);
        println!("    ls / : {} octets, {}, premières lignes : {:?}", o.stdout.len(), o.status,
                 s.lines().take(4).collect::<Vec<_>>());
        ensure!(o.status.success(), "{}", o.status);
        ensure!(!o.stdout.is_empty(), "rien lu");
        Ok(())
    })());
    check("status() et code de sortie", (|| {
        let me = env::current_exe().map_err(e)?;
        let st = Command::new(&me).arg("exit").arg("3").status().map_err(e)?;
        ensure!(st.code() == Some(3), "{st}");
        Ok(())
    })());
    check("stdin, stdout, env, current_dir du fils", (|| {
        let me = env::current_exe().map_err(e)?;
        let mut c = Command::new(&me)
            .arg("echo")
            .env("KORIN_FILS", "oui")
            .current_dir("/tmp")
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .spawn()
            .map_err(e)?;
        c.stdin.take().unwrap().write_all(b"ping").map_err(e)?;
        let o = c.wait_with_output().map_err(e)?;
        let s = String::from_utf8_lossy(&o.stdout);
        ensure!(s.contains("ping") && s.contains("oui"), "{s:?}");
        println!("    {}", s.trim());
        Ok(())
    })());
    check("programme absent : NotFound", (|| {
        let r = Command::new("/bin/nexistepas-korin").status();
        ensure!(matches!(&r, Err(x) if x.kind() == std::io::ErrorKind::NotFound), "{r:?}");
        Ok(())
    })());
}

fn tcp() {
    check("TcpListener 127.0.0.1:0, TcpStream, écho", (|| {
        let l = TcpListener::bind("127.0.0.1:0").map_err(e)?;
        let addr = l.local_addr().map_err(e)?;
        println!("    écoute sur {addr}");
        let h = thread::spawn(move || -> Result<usize, String> {
            let (mut s, peer) = l.accept().map_err(e)?;
            println!("    accept : {peer}");
            let mut buf = vec![0; 100_000];
            let mut n = 0;
            while n < buf.len() {
                let k = s.read(&mut buf[n..]).map_err(e)?;
                if k == 0 {
                    break;
                }
                n += k;
            }
            s.write_all(&buf[..n]).map_err(e)?;
            Ok(n)
        });
        let mut c = TcpStream::connect(addr).map_err(e)?;
        c.set_nodelay(true).map_err(e)?;
        let data: Vec<u8> = (0..100_000).map(|i| (i % 251) as u8).collect();
        c.write_all(&data).map_err(e)?;
        let mut back = vec![0; data.len()];
        c.read_exact(&mut back).map_err(e)?;
        ensure!(back == data, "données différentes");
        ensure!(h.join().map_err(|_| "panique".to_string())?? == 100_000, "taille");
        Ok(())
    })());
    check("connect refusé : ConnectionRefused", (|| {
        let r = TcpStream::connect_timeout(&"127.0.0.1:1".parse().unwrap(), Duration::from_secs(3));
        ensure!(r.is_err(), "connecté ?");
        println!("    {:?}", r.err().map(|x| x.kind()));
        Ok(())
    })());
    check("read_timeout", (|| {
        let l = TcpListener::bind("127.0.0.1:0").map_err(e)?;
        let c = TcpStream::connect(l.local_addr().map_err(e)?).map_err(e)?;
        let (mut s, _) = l.accept().map_err(e)?;
        s.set_read_timeout(Some(Duration::from_millis(100))).map_err(e)?;
        let t = Instant::now();
        let r = s.read(&mut [0; 4]);
        ensure!(r.is_err(), "{r:?}");
        ensure!(t.elapsed() < Duration::from_secs(3), "{:?}", t.elapsed());
        drop(c);
        Ok(())
    })());
}

fn udp() {
    check("UdpSocket 127.0.0.1 aller-retour", (|| {
        let a = UdpSocket::bind("127.0.0.1:0").map_err(e)?;
        let b = UdpSocket::bind("127.0.0.1:0").map_err(e)?;
        a.send_to(b"datagramme", b.local_addr().map_err(e)?).map_err(e)?;
        let mut buf = [0; 64];
        b.set_read_timeout(Some(Duration::from_secs(3))).map_err(e)?;
        let (n, _from) = b.recv_from(&mut buf).map_err(e)?;
        ensure!(&buf[..n] == b"datagramme", "reçu {:?}", &buf[..n]);
        Ok(())
    })());
}

struct Noisy(&'static AtomicUsize);
impl Drop for Noisy {
    fn drop(&mut self) {
        self.0.fetch_add(1, Ordering::SeqCst);
    }
}

fn panics() {
    panic::set_hook(Box::new(|i| println!("    (crochet de panique : {i})")));
    check("catch_unwind d'un panic!", (|| {
        static D: AtomicUsize = AtomicUsize::new(0);
        let r = panic::catch_unwind(|| {
            let _n = Noisy(&D);
            panic!("exprès {}", 42);
        });
        let p = r.err().ok_or("pas de panique")?;
        // String (formaté) ou &'static str (format_args! réduit à une constante)
        let msg = p.downcast_ref::<String>().cloned()
            .or_else(|| p.downcast_ref::<&str>().map(|s| s.to_string())).unwrap_or_default();
        ensure!(msg == "exprès 42", "charge {msg:?}");
        ensure!(D.load(Ordering::SeqCst) == 1, "destructeur pendant le déroulement");
        Ok(())
    })());
    check("panique dans un thread : join rend Err", (|| {
        let r = thread::spawn(|| panic!("dans le thread")).join();
        ensure!(r.is_err(), "Ok ?");
        Ok(())
    })());
    check("dépassement et index hors bornes rattrapés", (|| {
        let v = vec![1, 2, 3];
        let i = std::hint::black_box(7);
        ensure!(panic::catch_unwind(|| v[i]).is_err(), "index");
        let x = std::hint::black_box(u32::MAX);
        let r = panic::catch_unwind(|| x.checked_add(1).expect("dépassement"));
        ensure!(r.is_err(), "expect");
        Ok(())
    })());
    check("Mutex empoisonné", (|| {
        let m = Arc::new(Mutex::new(0));
        let m2 = m.clone();
        let _ = thread::spawn(move || {
            let _g = m2.lock().unwrap();
            panic!("poison");
        })
        .join();
        ensure!(m.is_poisoned(), "pas empoisonné");
        Ok(())
    })());
    let _ = panic::take_hook();
}

fn main() {
    let args: Vec<String> = env::args().collect();
    match args.get(1).map(|s| s.as_str()) {
        // les fils de l'essai cmd
        Some("exit") => std::process::exit(args[2].parse().unwrap()),
        Some("echo") => {
            let mut s = String::new();
            std::io::stdin().read_to_string(&mut s).unwrap();
            println!("{s} KORIN_FILS={} cwd={}", env::var("KORIN_FILS").unwrap_or_default(),
                     env::current_dir().map(|p| p.display().to_string()).unwrap_or_default());
            return;
        }
        _ => {}
    }
    let base: PathBuf = args.get(2).map(PathBuf::from).unwrap_or_else(env::temp_dir);
    let all = args.len() < 2;
    let want = |g: &str| all || args[1] == g;
    let groups: [(&str, &dyn Fn()); 10] = [
        ("hello", &hello),
        ("env", &envs),
        ("time", &time),
        ("threads", &threads),
        ("sync", &sync),
        ("fs", &|| fs_tests(&base)),
        ("cmd", &cmd),
        ("tcp", &tcp),
        ("udp", &udp),
        ("panic", &panics),
    ];
    for (name, f) in groups.iter() {
        if want(name) {
            println!("[{name}]");
            f();
        }
    }
    let n = FAILS.load(Ordering::SeqCst);
    println!("korintest : {} échec(s)", n);
    std::process::exit(n as i32);
}
