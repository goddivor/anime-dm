# Ce qu'il reste à faire

État au 18 septembre 2026, branche `feature/win32-cpp`. Chaque point est une entrée de menu
qui existe déjà sans comportement, ou un chantier ouvert ailleurs.

## Menu Aide

- [ ] **Aide** (F1) : ouvrir une page d'aide, en ligne ou embarquée.

## Fenêtre d'ajout et extension

- [ ] **Choix de la file dans la fenêtre d'ajout** : *Plus tard* met toujours dans la file
      principale ; offrir la file du planificateur, comme le « Télécharger plus tard » d'IDM.
- [ ] **Ajouter et retirer des fichiers depuis l'onglet Files** du Planificateur, comme IDM.

## Publication de l'extension

- [ ] **Chrome Web Store** (5 $ une fois, couvre Chrome, Edge, Brave, Vivaldi, Opera,
      Chromium) : envoyer le zip avec la `key` du manifeste pour garder l'identifiant
      `kajalpjiomebkclalgjggcgjeiibkfcg`.
- [ ] **Firefox** (gratuit) : signature auto-distribuée du XPI (`adm@animedm.app`), puis livrer
      le fichier signé dans `resources\extension\adm@animedm.app.xpi`.
- [ ] Vérifier `kChromiumStoreUrl` dans `BridgeProtocol.h` une fois la fiche publiée.

Tant que rien n'est publié ni signé, les clés d'installation qu'écrit la fenêtre Options
restent sans effet : Chrome ignore sur Windows toute extension hors magasin, Firefox tout XPI
non signé. En attendant, l'extension se charge en mode développeur (voir `extension/README.md`).

## Icônes de dossier

- [ ] Les **superpositions** note, genre et logo des gabarits (qui lisent un `.nfo`) ne sont pas
      portées, comme dans l'application Tauri.
