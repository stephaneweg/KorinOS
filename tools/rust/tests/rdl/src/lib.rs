//! rdl : une cdylib Rust minimale chargée par dlopen depuis un hôte C (rdlhost.c, docs/RUST.md).
//! Ce qu'un module pyo3 fera : thread_local! (TLS du module, réserve de l'hôte), std (allocation,
//! formatage, sortie standard par la libc de l'hôte), panique rattrapée dans le module (le
//! .eh_frame du module inscrit par dlopen), constructeur de module.

use std::cell::{Cell, RefCell};
use std::ffi::{c_char, c_int, CStr};
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::Mutex;

thread_local! {
    static COUNTER: Cell<u32> = const { Cell::new(0) };
    static NAME: RefCell<String> = RefCell::new(String::from("vide"));
    static GUARD: Guard = Guard;
}

static DROPS: AtomicU32 = AtomicU32::new(0);
static LOG: Mutex<Vec<String>> = Mutex::new(Vec::new());
static CTOR: AtomicU32 = AtomicU32::new(0);

struct Guard;
impl Drop for Guard {
    fn drop(&mut self) {
        DROPS.fetch_add(1, Ordering::SeqCst);
    }
}

// un constructeur (.init_array) : dlopen doit l'appeler
#[used]
#[unsafe(link_section = ".init_array")]
static INIT: extern "C" fn() = {
    extern "C" fn init() {
        CTOR.store(1234, Ordering::SeqCst);
    }
    init
};

/// Incrémente le compteur du thread et le rend ; arme aussi le destructeur du thread.
#[unsafe(no_mangle)]
pub extern "C" fn rdl_bump() -> u32 {
    GUARD.with(|_| ());
    COUNTER.with(|c| {
        c.set(c.get() + 1);
        c.get()
    })
}

/// Range un nom dans la TLS du thread, le relit, le note dans un journal partagé.
#[unsafe(no_mangle)]
pub extern "C" fn rdl_name(s: *const c_char) -> c_int {
    let s = unsafe { CStr::from_ptr(s) }.to_string_lossy().into_owned();
    NAME.with(|n| *n.borrow_mut() = s.clone());
    let back = NAME.with(|n| n.borrow().clone());
    LOG.lock().unwrap().push(back.clone());
    (back == s) as c_int
}

/// Le nombre d'entrées du journal et le nombre de destructeurs de TLS passés.
#[unsafe(no_mangle)]
pub extern "C" fn rdl_stats(log: *mut u32, drops: *mut u32, ctor: *mut u32) {
    unsafe {
        *log = LOG.lock().unwrap().len() as u32;
        *drops = DROPS.load(Ordering::SeqCst);
        *ctor = CTOR.load(Ordering::SeqCst);
    }
}

/// Lève une panique et la rattrape dans le module : 1 si rattrapée avec le bon message.
#[unsafe(no_mangle)]
pub extern "C" fn rdl_panic() -> c_int {
    std::panic::set_hook(Box::new(|_| {}));
    let r = std::panic::catch_unwind(|| {
        let v: Vec<u32> = Vec::new();
        if v.is_empty() {
            panic!("panique dans la cdylib");
        }
        v[0]
    });
    let _ = std::panic::take_hook();
    match r {
        Err(p) => (p.downcast_ref::<&str>() == Some(&"panique dans la cdylib")) as c_int,
        Ok(_) => 0,
    }
}

/// println! depuis le module, et une chaîne formatée rendue à l'hôte (à libérer par rdl_free).
#[unsafe(no_mangle)]
pub extern "C" fn rdl_hello(n: u32) -> *mut c_char {
    println!("  rdl : bonjour depuis la cdylib Rust ({} threads de std, {:?})", n,
             std::thread::current().id());
    std::ffi::CString::new(format!("rdl:{}", n * 2)).unwrap().into_raw()
}

#[unsafe(no_mangle)]
pub extern "C" fn rdl_free(p: *mut c_char) {
    drop(unsafe { std::ffi::CString::from_raw(p) });
}

/// Un thread de std lancé depuis le module (pthread_create de l'hôte), avec sa TLS à lui.
#[unsafe(no_mangle)]
pub extern "C" fn rdl_spawn() -> u32 {
    std::thread::spawn(|| {
        rdl_bump();
        rdl_bump()
    })
    .join()
    .unwrap_or(0)
}
