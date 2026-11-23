//! Le moteur (ccui-core) dans l'app : ce que faisait la coquille Tauri de ClaudeCodeUI (`desktop/src-tauri` :
//! `commands.rs`, `sink.rs`), sans web view. Un runtime tokio à part tient le moteur (`Arc<Core>`) ; l'interface egui
//! l'appelle directement (les 114 commandes Tauri deviennent des appels à `core`, lancés par `task` ou `fire`), et
//! ce que le moteur pousse (`Push` : patchs des sessions, résumé, préférences…) arrive dans un canal que la boucle
//! d'egui relève à chaque image (`drain`).

use std::future::Future;
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use std::sync::mpsc::{Receiver, Sender, channel};
use std::sync::{Arc, Mutex};

use ccui_core::Core;
use ccui_shared::push::Push;

/// Une notification que le moteur voulait montrer (fenêtre sans le focus) : l'app la montre en bandeau.
#[derive(Clone, Debug)]
pub struct Notice {
    pub session_id: String,
    pub title: String,
    pub body: String,
}

/// Le `UiSink` de l'app : les pushes dans un canal, une image d'egui demandée à chaque envoi.
struct ChannelSink {
    tx: Mutex<Sender<Push>>,
    notices: Mutex<Vec<Notice>>,
    ctx: Mutex<Option<egui::Context>>,
    focused: AtomicBool,
}

impl ChannelSink {
    fn wake(&self) {
        if let Some(c) = self.ctx.lock().unwrap().as_ref() {
            c.request_repaint();
        }
    }
}

impl ccui_core::UiSink for ChannelSink {
    fn emit(&self, push: Push) {
        let _ = self.tx.lock().unwrap().send(push);
        self.wake();
    }

    fn notify(&self, session_id: &str, title: &str, body: &str) {
        self.notices.lock().unwrap().push(Notice { session_id: session_id.into(), title: title.into(), body: body.into() });
        self.wake();
    }

    fn window_focused(&self) -> bool {
        self.focused.load(Ordering::Relaxed)
    }
}

/// Le résultat d'un appel au moteur, rempli par le runtime ; l'interface le relève (`take`) aux images suivantes.
pub struct Task<T>(Arc<Mutex<Option<T>>>);

impl<T> Clone for Task<T> {
    fn clone(&self) -> Self {
        Task(self.0.clone())
    }
}

impl<T> Task<T> {
    /// Une tâche déjà finie (les essais, les valeurs connues d'avance).
    pub fn ready(v: T) -> Task<T> {
        Task(Arc::new(Mutex::new(Some(v))))
    }
    pub fn is_done(&self) -> bool {
        self.0.lock().unwrap().is_some()
    }
    /// Le résultat, une seule fois (None : pas encore là, ou déjà pris).
    pub fn take(&self) -> Option<T> {
        self.0.lock().unwrap().take()
    }
}

pub struct Backend {
    pub core: Arc<Core>,
    pub rt: tokio::runtime::Runtime,
    rx: Receiver<Push>,
    sink: Arc<ChannelSink>,
    /// Les appels en cours (l'interface se redessine vite tant qu'il y en a).
    busy: Arc<AtomicUsize>,
    /// Les erreurs des appels lancés par `fire`, à montrer (bandeau d'erreur).
    errors: Arc<Mutex<Vec<String>>>,
    pub version: String,
}

impl Backend {
    /// Le runtime, le moteur (sa langue : la culture enregistrée, sinon celle du système, `LANG`).
    pub fn new() -> Backend {
        let rt = tokio::runtime::Builder::new_multi_thread()
            .worker_threads(2)
            .thread_name("ccui")
            .enable_all()
            .build()
            .expect("runtime tokio");
        let (tx, rx) = channel();
        let sink = Arc::new(ChannelSink {
            tx: Mutex::new(tx),
            notices: Mutex::new(Vec::new()),
            ctx: Mutex::new(None),
            focused: AtomicBool::new(true),
        });
        // (la langue : celle enregistrée dans les réglages de l'app, sinon LC_ALL ou LANG s'ils disent fr ou en, sinon le
        // français, la langue de Korin)
        let locale = std::env::var("LC_ALL")
            .ok()
            .or_else(|| std::env::var("LANG").ok())
            .filter(|l| l.starts_with("fr") || l.starts_with("en"));
        let core = {
            let _g = rt.enter();
            Core::new(sink.clone(), locale.or_else(|| Some("fr".into())))
        };
        Backend {
            core,
            rt,
            rx,
            sink,
            busy: Arc::new(AtomicUsize::new(0)),
            errors: Arc::new(Mutex::new(Vec::new())),
            version: env!("CARGO_PKG_VERSION").to_string(),
        }
    }

    /// Le contexte d'egui à réveiller quand le moteur pousse quelque chose.
    pub fn set_context(&self, ctx: &egui::Context) {
        *self.sink.ctx.lock().unwrap() = Some(ctx.clone());
    }

    pub fn set_focused(&self, on: bool) {
        self.sink.focused.store(on, Ordering::Relaxed);
    }

    /// Les pushes arrivés depuis la dernière image.
    pub fn drain(&self) -> Vec<Push> {
        self.rx.try_iter().collect()
    }

    pub fn take_notices(&self) -> Vec<Notice> {
        std::mem::take(&mut *self.sink.notices.lock().unwrap())
    }

    pub fn take_errors(&self) -> Vec<String> {
        std::mem::take(&mut *self.errors.lock().unwrap())
    }

    /// Des appels sont en cours.
    pub fn busy(&self) -> bool {
        self.busy.load(Ordering::Relaxed) > 0
    }

    /// Lance `fut` sur le runtime ; son résultat se relève par `Task::take`.
    pub fn task<T, F>(&self, fut: F) -> Task<T>
    where
        T: Send + 'static,
        F: Future<Output = T> + Send + 'static,
    {
        let slot = Arc::new(Mutex::new(None));
        let out = slot.clone();
        let busy = self.busy.clone();
        let sink = self.sink.clone();
        busy.fetch_add(1, Ordering::Relaxed);
        self.rt.spawn(async move {
            let v = fut.await;
            *out.lock().unwrap() = Some(v);
            busy.fetch_sub(1, Ordering::Relaxed);
            sink.wake();
        });
        Task(slot)
    }

    /// Un travail bloquant (lecture de transcriptions, parcours de dossiers) sur les fils de `spawn_blocking`.
    pub fn blocking<T, F>(&self, f: F) -> Task<T>
    where
        T: Send + 'static,
        F: FnOnce() -> T + Send + 'static,
    {
        self.task(async move { tokio::task::spawn_blocking(f).await.expect("tâche bloquante") })
    }

    /// Lance `fut` sans attendre son résultat ; une erreur va dans le bandeau des erreurs (préfixée de `what`).
    pub fn fire<F>(&self, what: &str, fut: F)
    where
        F: Future<Output = Result<(), String>> + Send + 'static,
    {
        let errors = self.errors.clone();
        let what = what.to_string();
        let busy = self.busy.clone();
        let sink = self.sink.clone();
        busy.fetch_add(1, Ordering::Relaxed);
        self.rt.spawn(async move {
            if let Err(e) = fut.await {
                errors.lock().unwrap().push(if what.is_empty() { e } else { format!("{what} : {e}") });
            }
            busy.fetch_sub(1, Ordering::Relaxed);
            sink.wake();
        });
    }

    /// Attend `fut` sur place (appels courts : lire une préférence, un fichier de réglages).
    pub fn block<T>(&self, fut: impl Future<Output = T>) -> T {
        self.rt.block_on(fut)
    }

    /// Le moteur pousse une préférence changée (ce que faisaient `app_pref_set` et `app_flag_set` de Tauri).
    pub fn set_pref(&self, name: &str, value: Option<&str>) {
        match ccui_core::app_flag::set_pref(name, value) {
            Ok(true) => self.core.sink.emit(Push::PrefChanged { name: name.into(), value: value.map(String::from) }),
            Ok(false) => {}
            Err(e) => self.errors.lock().unwrap().push(e),
        }
    }

    pub fn set_flag(&self, name: &str, on: bool) {
        match ccui_core::app_flag::set_flag(name, on) {
            Ok(()) => self.core.sink.emit(Push::PrefChanged { name: name.into(), value: on.then(String::new) }),
            Err(e) => self.errors.lock().unwrap().push(e),
        }
    }

    pub fn error(&self, e: impl Into<String>) {
        self.errors.lock().unwrap().push(e.into());
    }

    /// La session vivante `id` (une erreur traduite sinon, comme `session()` des commandes Tauri).
    pub fn session(&self, id: &str) -> Result<Arc<ccui_core::live::LiveSession>, String> {
        self.core.sessions.get(id).ok_or_else(|| ccui_shared::t!("Session.NotFound"))
    }

    /// À la fermeture : chaque session libère son process (comme `shutdown` de la coquille Tauri).
    pub fn shutdown(&self) {
        let core = self.core.clone();
        self.rt.block_on(async move {
            let _ = tokio::time::timeout(std::time::Duration::from_secs(4), core.shutdown()).await;
        });
    }
}

/// Ouvre une adresse (http, https, mailto) ou un dossier : par la fenêtre (host.rs : Jet sous Korin, `xdg-open`
/// sur l'hôte de développement).
pub fn open_external(target: &str) {
    crate::host::open(target);
}
