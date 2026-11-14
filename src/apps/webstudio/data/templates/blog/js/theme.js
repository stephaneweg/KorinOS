// Le thème clair ou sombre : celui du système, sauf si le lecteur en a choisi un (retenu dans le navigateur).
(function () {
  let t = null;
  try { t = localStorage.getItem('theme'); } catch (e) {}
  if (!t) t = matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
  document.documentElement.dataset.theme = t;
  document.addEventListener('DOMContentLoaded', () => {
    document.querySelector('.theme')?.addEventListener('click', () => {
      const next = document.documentElement.dataset.theme === 'dark' ? 'light' : 'dark';
      document.documentElement.dataset.theme = next;
      try { localStorage.setItem('theme', next); } catch (e) {}
    });
  });
})();
