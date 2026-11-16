// La visionneuse des photos, et la carte postale envoyée par courriel.
const box = document.querySelector('.lightbox');
if (box) {
  const img = box.querySelector('img'), cap = box.querySelector('figcaption');
  document.querySelectorAll('.wall .polaroid').forEach(p => p.addEventListener('click', () => {
    const i = p.querySelector('img');
    img.src = i.src; img.alt = i.alt; cap.textContent = p.querySelector('figcaption').textContent;
    box.hidden = false;
  }));
  box.addEventListener('click', () => (box.hidden = true));
  document.addEventListener('keydown', e => { if (e.key === 'Escape') box.hidden = true; });
}
const card = document.getElementById('contact');
card?.addEventListener('submit', e => {
  e.preventDefault();
  const f = new FormData(card);
  // à remplacer par votre adresse
  location.href = 'mailto:bonjour@exemple.be?subject=' + encodeURIComponent('Une carte de ' + f.get('nom')) +
    '&body=' + encodeURIComponent(f.get('message') + '\n\n' + f.get('nom') + ' (' + f.get('mail') + ')');
});
