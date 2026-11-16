// Scène : le lecteur (un clic sur un titre le joue, la barre suit et se règle), la liste d'envoi.
const audio = document.getElementById('audio');
const player = document.querySelector('.player');
const PLAY = '<svg viewBox="0 0 24 24"><path d="M7 4.5v15l13-7.5Z"/></svg>';
const PAUSE = '<svg viewBox="0 0 24 24"><path d="M6 4h4v16H6zM14 4h4v16h-4z"/></svg>';
const mmss = s => isFinite(s) ? Math.floor(s / 60) + ':' + String(Math.floor(s % 60)).padStart(2, '0') : '0:00';
if (player && audio) {
  const tracks = [...player.querySelectorAll('.track')];
  const toggle = player.querySelector('.toggle');
  const fill = player.querySelector('.seek i');
  const time = player.querySelector('.time');
  let cur = -1;
  const select = i => {
    cur = i;
    tracks.forEach((t, k) => t.classList.toggle('on', k === i));
    audio.src = tracks[i].dataset.src;
    audio.play();
  };
  tracks.forEach((t, i) => t.addEventListener('click', () => { if (i === cur) audio.paused ? audio.play() : audio.pause(); else select(i); }));
  toggle.addEventListener('click', () => { if (cur < 0) select(0); else audio.paused ? audio.play() : audio.pause(); });
  const state = () => {
    const on = !audio.paused;
    player.classList.toggle('playing', on);
    toggle.innerHTML = on ? PAUSE : PLAY;
    tracks.forEach((t, k) => { t.querySelector('.play').innerHTML = on && k === cur ? PAUSE : PLAY; });
  };
  audio.addEventListener('play', state);
  audio.addEventListener('pause', state);
  audio.addEventListener('timeupdate', () => {
    fill.style.width = (audio.duration ? audio.currentTime / audio.duration * 100 : 0) + '%';
    time.textContent = mmss(audio.currentTime) + ' / ' + mmss(audio.duration);
  });
  audio.addEventListener('ended', () => { if (cur + 1 < tracks.length) select(cur + 1); });
  player.querySelector('.seek').addEventListener('click', e => {
    if (!audio.duration) return;
    const r = e.currentTarget.getBoundingClientRect();
    audio.currentTime = (e.clientX - r.left) / r.width * audio.duration;
  });
}
const mail = document.getElementById('mail-form');
if (mail) mail.addEventListener('submit', e => {
  e.preventDefault();
  mail.innerHTML = '<p style="margin:14px 0;font-size:19px">Merci, à très vite.</p>';
});
