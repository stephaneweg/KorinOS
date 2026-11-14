// Le compte à rebours jusqu'à data-date, et la réponse envoyée par courriel.
const target = new Date(document.getElementById('date').dataset.date);
const pad = n => String(n).padStart(2, '0');
function tick() {
  let s = Math.max(0, Math.floor((target - new Date()) / 1000));
  const d = Math.floor(s / 86400); s %= 86400;
  const h = Math.floor(s / 3600); s %= 3600;
  const m = Math.floor(s / 60); s %= 60;
  document.getElementById('d').textContent = d;
  document.getElementById('h').textContent = pad(h);
  document.getElementById('m').textContent = pad(m);
  document.getElementById('s').textContent = pad(s);
}
tick();
setInterval(tick, 1000);

const form = document.getElementById('rsvp');
form.addEventListener('submit', e => {
  e.preventDefault();
  const f = new FormData(form);
  const body = `Nom : ${f.get('nom')}\nPrésent : ${f.get('present')}\nPersonnes : ${f.get('nombre')}\nRégime : ${f.get('regime') || '—'}\n\n${f.get('mot') || ''}`;
  form.querySelector('.sent').hidden = false;
  // à remplacer par votre adresse
  location.href = 'mailto:reponse@exemple.be?subject=' + encodeURIComponent('Réponse à l\'invitation') + '&body=' + encodeURIComponent(body);
});
