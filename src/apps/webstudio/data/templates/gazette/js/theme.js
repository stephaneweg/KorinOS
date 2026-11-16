// La date du jour dans l'édition, et l'abonnement envoyé par courriel.
const today = document.getElementById('today');
if (today) {
  const d = new Date();
  today.textContent = 'Édition du ' + d.toLocaleDateString('fr-FR', { weekday: 'long', day: 'numeric', month: 'long', year: 'numeric' });
}
const abo = document.getElementById('abo');
abo?.addEventListener('submit', e => {
  e.preventDefault();
  // à remplacer par l'adresse de la rédaction
  location.href = 'mailto:redaction@exemple.be?subject=' + encodeURIComponent('Abonnement') + '&body=' + encodeURIComponent('Inscrire : ' + new FormData(abo).get('mail'));
});
