//! korinspot -- librespot (le client libre du protocole de Spotify) pour Korin, en bibliothèque C
//! (docs/etude/MUSIQUE.md §1, voie B ; construite par tools/spotify/build.sh pour x86_64-unknown-korin).
//!
//! L'app (src/apps/spotify) se connecte avec un jeton OAuth (oauthd, portée « streaming »), charge un titre par son
//! URI (spotify:track:…), le pilote (lecture, pause, position, arrêt) et lit son son : du PCM 16 bits stéréo à
//! 44 100 Hz que librespot écrit dans un tampon borné (le « Sink » de Korin), et que l'app donne à AudioKit. L'état
//! (en lecture, position, durée, fin de titre, titre indisponible, erreur) vient des événements du lecteur.
//!
//! Tout passe par un runtime tokio à deux fils, fait au premier appel. Les fonctions ne paniquent pas vers le C.
use std::collections::VecDeque;
use std::ffi::{c_char, c_int, CStr};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Condvar, Mutex, OnceLock};
use std::time::Duration;

use librespot_core::authentication::Credentials;
use librespot_core::config::SessionConfig;
use librespot_core::session::Session;
use librespot_core::spotify_id::SpotifyId;
use librespot_playback::audio_backend::{Sink, SinkResult};
use librespot_playback::config::PlayerConfig;
use librespot_playback::convert::Converter;
use librespot_playback::decoder::AudioPacket;
use librespot_playback::mixer::NoOpVolume;
use librespot_playback::player::{Player, PlayerEvent};

const RATE: u64 = 44_100;
const PCM_CAP: usize = 44_100 * 2; // une seconde de stéréo

// ---- le son : un tampon borné entre le lecteur (qui écrit) et l'app (qui lit) ------------------------------------------

struct Pcm {
    buf: Mutex<VecDeque<i16>>,
    room: Condvar,
    flush: AtomicBool, // vider (chargement, saut, arrêt) : l'écrivain abandonne ce qu'il tient
}

fn pcm() -> &'static Arc<Pcm> {
    static P: OnceLock<Arc<Pcm>> = OnceLock::new();
    P.get_or_init(|| {
        Arc::new(Pcm { buf: Mutex::new(VecDeque::with_capacity(PCM_CAP)), room: Condvar::new(), flush: AtomicBool::new(false) })
    })
}

fn pcm_clear() {
    let p = pcm();
    p.flush.store(true, Ordering::SeqCst);
    p.buf.lock().unwrap().clear();
    p.room.notify_all();
}

struct KorinSink(Arc<Pcm>);

impl Sink for KorinSink {
    fn write(&mut self, packet: AudioPacket, converter: &mut Converter) -> SinkResult<()> {
        let samples = match packet {
            AudioPacket::Samples(s) => converter.f64_to_s16(&s),
            AudioPacket::Raw(_) => return Ok(()),
        };
        self.0.flush.store(false, Ordering::SeqCst);
        let mut off = 0;
        while off < samples.len() {
            let mut b = self.0.buf.lock().unwrap();
            while b.len() >= PCM_CAP && !self.0.flush.load(Ordering::SeqCst) {
                b = self.0.room.wait_timeout(b, Duration::from_millis(50)).unwrap().0;
            }
            if self.0.flush.load(Ordering::SeqCst) {
                return Ok(()); // (un saut, un autre titre : ce morceau ne sert plus)
            }
            let n = (PCM_CAP - b.len()).min(samples.len() - off);
            b.extend(&samples[off..off + n]);
            off += n;
        }
        Ok(())
    }
}

// ---- l'état ---------------------------------------------------------------------------------------------------------

#[derive(Default, Clone)]
struct Status {
    state: c_int,  // KS_*
    base_ms: u32,  // position annoncée par le lecteur au dernier Playing / Seeked
    read: u64,     // trames lues par l'app depuis
    duration_ms: u32,
    ended: u32,    // titres finis (compteur)
    unavailable: u32,
    error: String,
}

const KS_STOPPED: c_int = 0;
const KS_LOADING: c_int = 1;
const KS_PLAYING: c_int = 2;
const KS_PAUSED: c_int = 3;

fn status() -> &'static Mutex<Status> {
    static S: OnceLock<Mutex<Status>> = OnceLock::new();
    S.get_or_init(|| Mutex::new(Status::default()))
}

fn set_error(e: impl ToString) {
    status().lock().unwrap().error = e.to_string();
}

struct Core {
    rt: tokio::runtime::Runtime,
    player: Mutex<Option<Arc<Player>>>,
    session: Mutex<Option<Session>>,
}

fn core() -> Option<&'static Core> {
    static C: OnceLock<Option<Core>> = OnceLock::new();
    C.get_or_init(|| {
        tokio::runtime::Builder::new_multi_thread()
            .worker_threads(2)
            .enable_all()
            .build()
            .ok()
            .map(|rt| Core { rt, player: Mutex::new(None), session: Mutex::new(None) })
    })
    .as_ref()
}

fn player() -> Option<Arc<Player>> {
    core().and_then(|c| c.player.lock().unwrap().clone())
}

unsafe fn cstr<'a>(p: *const c_char) -> Option<&'a str> {
    if p.is_null() {
        None
    } else {
        CStr::from_ptr(p).to_str().ok()
    }
}

fn on_event(ev: PlayerEvent) {
    let mut s = status().lock().unwrap();
    match ev {
        PlayerEvent::Loading { .. } => s.state = KS_LOADING,
        PlayerEvent::Playing { position_ms, .. } => {
            s.state = KS_PLAYING;
            s.base_ms = position_ms;
            s.read = 0;
        }
        PlayerEvent::Paused { position_ms, .. } => {
            s.state = KS_PAUSED;
            s.base_ms = position_ms;
            s.read = 0;
        }
        PlayerEvent::Seeked { position_ms, .. } | PlayerEvent::PositionCorrection { position_ms, .. } => {
            s.base_ms = position_ms;
            s.read = 0;
        }
        PlayerEvent::TrackChanged { audio_item } => s.duration_ms = audio_item.duration_ms,
        PlayerEvent::EndOfTrack { .. } => {
            s.state = KS_STOPPED;
            s.ended += 1;
        }
        PlayerEvent::Unavailable { .. } => {
            s.state = KS_STOPPED;
            s.unavailable += 1;
        }
        PlayerEvent::Stopped { .. } => s.state = KS_STOPPED,
        _ => {}
    }
}

// ---- l'interface C (korinspot.h) --------------------------------------------------------------------------------------

/// Connexion avec un jeton d'accès OAuth (portée « streaming ») -> 0, -1 (ks_error dit pourquoi). Attend la réponse
/// de Spotify (quelques secondes au plus) : à appeler hors du fil de la fenêtre.
#[no_mangle]
pub unsafe extern "C" fn ks_login(token: *const c_char) -> c_int {
    let (Some(c), Some(token)) = (core(), cstr(token)) else {
        set_error("pas de runtime, ou jeton illisible");
        return -1;
    };
    let token = token.to_string();
    let r = c.rt.block_on(async move {
        let session = Session::new(SessionConfig::default(), None);
        session.connect(Credentials::with_access_token(token), false).await.map(|_| session)
    });
    let session = match r {
        Ok(s) => s,
        Err(e) => {
            set_error(e);
            return -1;
        }
    };
    let _g = c.rt.enter();
    let p = pcm().clone();
    let pl = Player::new(PlayerConfig::default(), session.clone(), Box::new(NoOpVolume), move || {
        Box::new(KorinSink(p)) as Box<dyn Sink>
    });
    let mut events = pl.get_player_event_channel();
    c.rt.spawn(async move {
        while let Some(ev) = events.recv().await {
            on_event(ev);
        }
    });
    if let Some(old) = c.player.lock().unwrap().replace(pl) {
        old.stop();
    }
    *c.session.lock().unwrap() = Some(session);
    status().lock().unwrap().error.clear();
    0
}

/// Un titre (« spotify:track:… ») : chargé, joué tout de suite si play -> 0, -1
#[no_mangle]
pub unsafe extern "C" fn ks_load(uri: *const c_char, play: c_int, position_ms: u32) -> c_int {
    let Some(pl) = player() else {
        set_error("pas connecté");
        return -1;
    };
    let id = match cstr(uri).map(SpotifyId::from_uri) {
        Some(Ok(id)) => id,
        _ => {
            set_error("adresse de titre illisible");
            return -1;
        }
    };
    pcm_clear();
    {
        let mut s = status().lock().unwrap();
        s.state = KS_LOADING;
        s.base_ms = position_ms;
        s.read = 0;
        s.duration_ms = 0;
    }
    pl.load(id, play != 0, position_ms);
    0
}

#[no_mangle]
pub extern "C" fn ks_play() {
    if let Some(pl) = player() {
        pl.play();
    }
}

#[no_mangle]
pub extern "C" fn ks_pause() {
    if let Some(pl) = player() {
        pl.pause();
    }
}

#[no_mangle]
pub extern "C" fn ks_stop() {
    if let Some(pl) = player() {
        pl.stop();
    }
    pcm_clear();
}

#[no_mangle]
pub extern "C" fn ks_seek(position_ms: u32) {
    if let Some(pl) = player() {
        pcm_clear();
        let mut s = status().lock().unwrap();
        s.base_ms = position_ms;
        s.read = 0;
        drop(s);
        pl.seek(position_ms);
    }
}

/// Le son : jusqu'à frames trames stéréo (2 × i16) dans out -> trames rendues (0 : rien pour l'instant)
#[no_mangle]
pub unsafe extern "C" fn ks_read(out: *mut i16, frames: usize) -> usize {
    if out.is_null() || frames == 0 {
        return 0;
    }
    let p = pcm();
    let mut b = p.buf.lock().unwrap();
    let n = (b.len() / 2).min(frames) * 2;
    let dst = std::slice::from_raw_parts_mut(out, n);
    for (d, s) in dst.iter_mut().zip(b.drain(..n)) {
        *d = s;
    }
    drop(b);
    p.room.notify_all();
    if n > 0 {
        status().lock().unwrap().read += (n / 2) as u64;
    }
    n / 2
}

/// L'état, dans les champs de struct ks_status (korinspot.h) -> 0
#[repr(C)]
pub struct KsStatus {
    pub state: c_int,
    pub position_ms: u32,
    pub duration_ms: u32,
    pub ended: u32,
    pub unavailable: u32,
}

#[no_mangle]
pub unsafe extern "C" fn ks_status(out: *mut KsStatus) -> c_int {
    if out.is_null() {
        return -1;
    }
    let s = status().lock().unwrap().clone();
    let pos = s.base_ms as u64 + if s.state == KS_PLAYING { s.read * 1000 / RATE } else { 0 };
    *out = KsStatus {
        state: s.state,
        position_ms: pos.min(u32::MAX as u64) as u32,
        duration_ms: s.duration_ms,
        ended: s.ended,
        unavailable: s.unavailable,
    };
    0
}

/// La dernière erreur (texte UTF-8, terminé par 0) -> octets
#[no_mangle]
pub unsafe extern "C" fn ks_error(buf: *mut c_char, cap: usize) -> usize {
    if buf.is_null() || cap == 0 {
        return 0;
    }
    let e = status().lock().unwrap().error.clone();
    let n = e.len().min(cap - 1);
    std::ptr::copy_nonoverlapping(e.as_ptr() as *const c_char, buf, n);
    *buf.add(n) = 0;
    n
}
