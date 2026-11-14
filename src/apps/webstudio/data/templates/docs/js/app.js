// La recherche dans la navigation, un bouton « Copier » sur chaque bloc de code, le sommaire qui suit la lecture.
const search = document.querySelector('.search');
search?.addEventListener('input', () => {
  const q = search.value.trim().toLowerCase();
  document.querySelectorAll('.sidebar a').forEach(a => { a.hidden = q && !a.textContent.toLowerCase().includes(q); });
});

document.querySelectorAll('pre').forEach(pre => {
  const b = document.createElement('button');
  b.className = 'copy';
  b.textContent = 'Copier';
  b.addEventListener('click', async () => {
    try { await navigator.clipboard.writeText(pre.querySelector('code').textContent); b.textContent = 'Copié'; }
    catch (e) { b.textContent = 'Sélectionnez le texte'; }
    setTimeout(() => (b.textContent = 'Copier'), 1500);
  });
  pre.appendChild(b);
});

const links = [...document.querySelectorAll('.toc a')];
const heads = links.map(a => document.querySelector(a.getAttribute('href'))).filter(Boolean);
addEventListener('scroll', () => {
  let cur = heads[0];
  for (const h of heads) if (h.getBoundingClientRect().top < 120) cur = h;
  links.forEach(a => a.classList.toggle('on', cur && a.getAttribute('href') === '#' + cur.id));
}, { passive: true });
