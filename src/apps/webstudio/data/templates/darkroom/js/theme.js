// La visionneuse (flèches, Échap) et le message envoyé par courriel.
const viewer = document.querySelector('.viewer');
if (viewer) {
  const figs = [...document.querySelectorAll('.cols figure')];
  const img = viewer.querySelector('img'), cap = viewer.querySelector('figcaption');
  let cur = 0;
  const show = i => { cur = (i + figs.length) % figs.length; img.src = figs[cur].querySelector('img').src; cap.textContent = figs[cur].querySelector('figcaption').textContent; viewer.hidden = false; };
  figs.forEach((f, i) => f.addEventListener('click', () => show(i)));
  viewer.querySelector('.prev').addEventListener('click', () => show(cur - 1));
  viewer.querySelector('.next').addEventListener('click', () => show(cur + 1));
  viewer.querySelector('.close').addEventListener('click', () => (viewer.hidden = true));
  document.addEventListener('keydown', e => {
    if (viewer.hidden) return;
    if (e.key === 'Escape') viewer.hidden = true;
    if (e.key === 'ArrowRight') show(cur + 1);
    if (e.key === 'ArrowLeft') show(cur - 1);
  });
}
const form = document.getElementById('contact');
form?.addEventListener('submit', e => {
  e.preventDefault();
  const f = new FormData(form);
  // à remplacer par votre adresse
  location.href = 'mailto:atelier@exemple.be?subject=' + encodeURIComponent('Message de ' + f.get('nom')) + '&body=' + encodeURIComponent(f.get('message') + '\n\n' + f.get('mail'));
});
