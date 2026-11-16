// Artisan : la visionneuse de la galerie, l'estimation de la commande, la demande envoyée par courriel.
const box = document.querySelector('.lightbox');
if (box) {
  document.querySelectorAll('.masonry figure').forEach(f => f.addEventListener('click', () => {
    box.querySelector('img').src = f.querySelector('img').src;
    box.hidden = false;
  }));
  box.addEventListener('click', () => { box.hidden = true; });
  addEventListener('keydown', e => { if (e.key === 'Escape') box.hidden = true; });
}

const form = document.getElementById('order');
if (form) {
  const euro = n => n.toLocaleString('fr-FR') + ' €';
  const update = () => {
    const model = form.model.options[form.model.selectedIndex];
    const wood = form.querySelector('input[name=wood]:checked');
    const lines = [[model.text.split(' — ')[0], +model.value], ['Bois : ' + wood.dataset.name, +wood.value]];
    form.querySelectorAll('input[name=opt]:checked').forEach(o => lines.push([o.dataset.name, +o.value]));
    document.getElementById('lines').innerHTML = lines.map(l => '<li><span>' + l[0] + '</span><span>' + euro(l[1]) + '</span></li>').join('');
    document.getElementById('total').textContent = euro(lines.reduce((s, l) => s + l[1], 0));
    return lines;
  };
  form.addEventListener('change', update);
  update();
  form.addEventListener('submit', e => {
    e.preventDefault();
    const lines = update().map(l => '- ' + l[0] + ' : ' + euro(l[1])).join('\n');
    const body = 'Bonjour,\n\nJe voudrais commander :\n' + lines + '\nTotal estimé : ' + document.getElementById('total').textContent +
      '\n\n' + form.msg.value + '\n\n' + form.name.value + ' — ' + form.mail.value;
    // remplacez l'adresse par celle de l'atelier
    location.href = 'mailto:atelier@exemple.org?subject=' + encodeURIComponent('Commande : ' + form.name.value) + '&body=' + encodeURIComponent(body);
  });
}
