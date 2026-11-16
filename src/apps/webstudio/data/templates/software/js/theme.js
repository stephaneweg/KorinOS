// Logiciel : l'apparition au défilement, la lueur sous la souris, le filtre et la visionneuse des captures,
// le système du visiteur mis en avant, la commande copiée.
document.documentElement.classList.add('js');
const seen = new IntersectionObserver(es => es.forEach(e => { if (e.isIntersecting) { e.target.classList.add('seen'); seen.unobserve(e.target); } }), { threshold: .12 });
document.querySelectorAll('.reveal').forEach(el => seen.observe(el));

document.querySelectorAll('.card').forEach(c => c.addEventListener('pointermove', e => {
  const r = c.getBoundingClientRect();
  c.style.setProperty('--mx', (e.clientX - r.left) + 'px');
  c.style.setProperty('--my', (e.clientY - r.top) + 'px');
}));

const tabs = document.querySelectorAll('.tabs button');
tabs.forEach(b => b.addEventListener('click', () => {
  tabs.forEach(x => x.classList.toggle('on', x === b));
  document.querySelectorAll('.shot').forEach(s => { s.hidden = b.dataset.f !== 'tout' && s.dataset.cat !== b.dataset.f; });
}));
const box = document.querySelector('.lightbox');
if (box) {
  document.querySelectorAll('.shot').forEach(s => s.addEventListener('click', () => {
    const img = s.querySelector('img');
    box.querySelector('img').src = img.src;
    box.querySelector('img').alt = img.alt;
    box.hidden = false;
  }));
  box.addEventListener('click', () => { box.hidden = true; });
  addEventListener('keydown', e => { if (e.key === 'Escape') box.hidden = true; });
}

const ua = navigator.userAgent;
const mine = /Windows/.test(ua) ? 'windows' : /Mac OS X/.test(ua) ? 'mac' : /Linux|X11/.test(ua) ? 'linux' : '';
document.querySelectorAll('.platform').forEach(p => p.classList.toggle('mine', p.dataset.os === mine));

document.querySelectorAll('[data-copy]').forEach(b => b.addEventListener('click', () => {
  const t = b.parentElement.querySelector('code').textContent;
  const done = () => { const old = b.innerHTML; b.textContent = 'Copié'; setTimeout(() => { b.innerHTML = old; }, 1400); };
  if (navigator.clipboard) navigator.clipboard.writeText(t).then(done, done); else done();
}));
