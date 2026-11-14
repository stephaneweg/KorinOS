// Mensuel ou annuel : les prix suivent le bouton.
const buttons = document.querySelectorAll('.toggle button');
buttons.forEach(b => b.addEventListener('click', () => {
  buttons.forEach(x => x.classList.toggle('on', x === b));
  const period = b.dataset.period;
  document.querySelectorAll('.price span').forEach(s => {
    s.textContent = period === 'year' ? s.dataset.year : s.dataset.month;
  });
}));
