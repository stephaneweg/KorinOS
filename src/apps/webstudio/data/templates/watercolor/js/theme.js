// Le compte à rebours, la visionneuse des photos et la réponse envoyée par courriel.
const dateEl = document.getElementById('date');
if (dateEl && document.getElementById('d')) {
  const target = new Date(dateEl.dataset.date);
  const tick = () => {
    let s = Math.max(0, Math.floor((target - new Date()) / 1000));
    document.getElementById('d').textContent = Math.floor(s / 86400); s %= 86400;
    document.getElementById('h').textContent = Math.floor(s / 3600); s %= 3600;
    document.getElementById('m').textContent = Math.floor(s / 60);
  };
  tick(); setInterval(tick, 30000);
}
const box = document.querySelector('.lightbox');
if (box) {
  const img = box.querySelector('img');
  document.querySelectorAll('.gallery figure').forEach(f => f.addEventListener('click', () => { img.src = f.querySelector('img').src; box.hidden = false; }));
  box.addEventListener('click', () => (box.hidden = true));
  document.addEventListener('keydown', e => { if (e.key === 'Escape') box.hidden = true; });
}
const form = document.getElementById('rsvp');
form?.addEventListener('submit', e => {
  e.preventDefault();
  const f = new FormData(form);
  // à remplacer par votre adresse
  location.href = 'mailto:reponse@exemple.be?subject=' + encodeURIComponent('Réponse : ' + f.get('nom')) + '&body=' +
    encodeURIComponent(`${f.get('nom')} : ${f.get('present')}, ${f.get('nombre')} personne(s)\n\n${f.get('mot') || ''}`);
});
