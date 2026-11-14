// Le filtre par étiquette : une carte reste si son data-tags contient l'étiquette choisie.
const buttons = document.querySelectorAll('.tags button');
const posts = document.querySelectorAll('.post');
buttons.forEach(b => b.addEventListener('click', () => {
  buttons.forEach(x => x.classList.toggle('on', x === b));
  const tag = b.dataset.tag;
  posts.forEach(p => { p.hidden = tag !== 'tout' && !p.dataset.tags.split(' ').includes(tag); });
}));
