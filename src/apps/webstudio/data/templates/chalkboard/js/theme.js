// La visionneuse des photos, le jour même dans les horaires, et la réservation envoyée par courriel.
const box = document.querySelector('.lightbox');
if (box) {
  const img = box.querySelector('img');
  document.querySelectorAll('.gallery .photo').forEach(p => p.addEventListener('click', () => {
    img.src = p.querySelector('img').src; box.hidden = false;
  }));
  box.addEventListener('click', () => (box.hidden = true));
  document.addEventListener('keydown', e => { if (e.key === 'Escape') box.hidden = true; });
}
document.querySelector(`.hours tr[data-day="${new Date().getDay()}"]`)?.classList.add('now');
const form = document.getElementById('booking');
if (form) {
  form.date.min = new Date().toISOString().slice(0, 10);
  form.addEventListener('submit', e => {
    e.preventDefault();
    const f = new FormData(form);
    // à remplacer par l'adresse du bistrot
    location.href = 'mailto:reservation@exemple.be?subject=' + encodeURIComponent('Réservation') + '&body=' +
      encodeURIComponent(`${f.get('nom')}, ${f.get('couverts')} couverts, le ${f.get('date')} à ${f.get('heure')}\n\n${f.get('mot') || ''}`);
  });
}
