# Extension de navigateur

Sur un site servi par une source installée, l'extension pose un bouton
« Télécharger avec ADM » ; un clic envoie la page à Anime Download Manager,
qui ouvre sa fenêtre d'ajout avec l'adresse et lit la page. L'icône de la barre
d'outils porte un badge « ADM » sur ces sites, et un clic dessus envoie la page
de l'onglet.

L'extension ne parle jamais au réseau : elle passe par le hôte natif
`adm-host.exe`, que l'application déclare aux navigateurs à son démarrage
(manifestes dans `%APPDATA%\anime-dm\host`, clés sous `HKCU`). Une même base de
code sert Chromium et Firefox ; le manifeste porte les deux formes de page
d'arrière-plan et chaque navigateur ignore celle de l'autre.

## Installer en mode développeur

L'application doit avoir été lancée au moins une fois, pour que le hôte soit
déclaré.

- **Chrome, Edge, Brave, Opera, Vivaldi** : page des extensions
  (`chrome://extensions`, `edge://extensions`, `brave://extensions`), activer le
  mode développeur, « Charger l'extension non empaquetée », choisir ce dossier.
  L'identifiant est fixé par la clé du manifeste
  (`kajalpjiomebkclalgjggcgjeiibkfcg`), c'est lui que le hôte autorise.
- **Firefox** : `about:debugging#/runtime/this-firefox`, « Charger un module
  complémentaire temporaire », choisir `manifest.json`. Le module disparaît à la
  fermeture de Firefox tant qu'il n'est pas signé.

## Fichiers

- `manifest.json` : permissions `nativeMessaging`, `storage`, `tabs` ; script de
  contenu sur toutes les pages, qui ne fait rien tant que le site n'est pas
  reconnu.
- `background.js` : demande la liste des sites au hôte (`{"kind":"sources"}`),
  la garde cinq minutes, répond aux scripts de contenu, envoie les adresses
  (`{"kind":"add","url":…}`), pose le badge.
- `content.js` : le bouton flottant, dans une racine fantôme pour rester à
  l'écart des styles de la page.
- `_locales` : les libellés en français et en anglais.
