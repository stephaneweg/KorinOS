// Projet libre : copier une commande, agrandir une capture.
document.querySelectorAll('.code button').forEach(b => b.addEventListener('click', () => {
  const t = [...b.parentElement.querySelectorAll('pre')].map(p => p.innerText).join('\n')
    .split('\n').filter(l => !l.startsWith('#')).map(l => l.replace(/^\$ /, '')).join('\n');
  const done = () => { b.textContent = 'Copié'; setTimeout(() => { b.textContent = 'Copier'; }, 1400); };
  if (navigator.clipboard) navigator.clipboard.writeText(t).then(done, done); else done();
}));
const box = document.querySelector('.lightbox');
document.querySelectorAll('.shot img, .device img').forEach(i => i.addEventListener('click', () => {
  box.querySelector('img').src = i.src;
  box.hidden = false;
}));
box.addEventListener('click', () => { box.hidden = true; });
addEventListener('keydown', e => { if (e.key === 'Escape') box.hidden = true; });
