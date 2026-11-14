// Le jour même en évidence dans les horaires, et la réservation envoyée par courriel.
const today = new Date().getDay();
document.querySelector(`tr[data-day="${today}"]`)?.classList.add('today');

const form = document.getElementById('booking');
form.date.min = new Date().toISOString().slice(0, 10);
form.addEventListener('submit', e => {
  e.preventDefault();
  const d = new FormData(form);
  const body = `Nom : ${d.get('nom')}\nCouverts : ${d.get('couverts')}\nDate : ${d.get('date')} à ${d.get('heure')}\n\n${d.get('mot') || ''}`;
  // à remplacer par l'adresse du restaurant
  location.href = 'mailto:reservation@exemple.be?subject=' + encodeURIComponent('Réservation') + '&body=' + encodeURIComponent(body);
});
