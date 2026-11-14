// La visionneuse : un clic agrandit l'image, les flèches passent à la suivante, Échap ferme.
const works = [...document.querySelectorAll('.work')];
const box = document.querySelector('.lightbox');
const art = box.querySelector('.lb-art');
let current = 0;

function show(i) {
  current = (i + works.length) % works.length;
  const w = works[current];
  art.className = 'lb-art ' + w.querySelector('.art').className.replace('art ', '');
  box.querySelector('.lb-title').textContent = w.dataset.title;
  box.querySelector('.lb-meta').textContent = w.dataset.meta;
  box.hidden = false;
}

works.forEach((w, i) => {
  w.tabIndex = 0;
  w.addEventListener('click', () => show(i));
  w.addEventListener('keydown', e => { if (e.key === 'Enter') show(i); });
});
box.querySelector('.lb-close').addEventListener('click', () => (box.hidden = true));
box.querySelector('.lb-prev').addEventListener('click', () => show(current - 1));
box.querySelector('.lb-next').addEventListener('click', () => show(current + 1));
box.addEventListener('click', e => { if (e.target === box) box.hidden = true; });
document.addEventListener('keydown', e => {
  if (box.hidden) return;
  if (e.key === 'Escape') box.hidden = true;
  if (e.key === 'ArrowRight') show(current + 1);
  if (e.key === 'ArrowLeft') show(current - 1);
});
