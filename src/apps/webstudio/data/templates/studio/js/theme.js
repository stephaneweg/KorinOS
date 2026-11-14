// Le filtre des projets, et le message envoyé par courriel.
const buttons = document.querySelectorAll('.filters button');
buttons.forEach(b => b.addEventListener('click', () => {
  buttons.forEach(x => x.classList.toggle('on', x === b));
  document.querySelectorAll('.all .project').forEach(p => { p.hidden = b.dataset.f !== 'tout' && p.dataset.cat !== b.dataset.f; });
}));
const form = document.getElementById('contact-form');
if (form) form.addEventListener('submit', e => {
  e.preventDefault();
  const f = new FormData(form);
  // remplacez l'adresse par celle du studio
  location.href = 'mailto:bonjour@exemple.org?subject=' + encodeURIComponent('Projet : ' + f.get('nom')) + '&body=' + encodeURIComponent(f.get('message') + '\n\n' + f.get('mail'));
});
