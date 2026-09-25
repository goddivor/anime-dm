# Ce qu'il reste à faire

État au 18 septembre 2026, branche `feature/win32-cpp`. Chaque point est une entrée de menu
qui existe déjà sans comportement, ou un chantier ouvert ailleurs.

## Menu Tâches

- [ ] **Téléchargement manuel** : saisir une adresse de vidéo directe (ou une liste HLS), un
      nom de fichier et un dossier, sans passer par une source ; le moteur sait déjà tirer une
      adresse nue (`download-smoke --url`).
- [ ] **Téléchargement par lot depuis presse-papiers** (Ctrl+Maj+V) : lire toutes les adresses
      du presse-papiers et les passer à l'import (`importing::Parse` puis `Resolve`), comme un
      fichier texte ; l'accélérateur est déjà déclaré.

## Menu Téléchargement

- [ ] **Limitation de débit** (activer, désactiver, paramètres) : un plafond en octets par
      seconde partagé par les connexions de `Transfer`, réglable dans un petit dialogue et
      rappelé dans `settings.json`.
- [ ] **Booster de téléchargement** : à définir, ou à retirer du menu ; IDM y met un mode qui
      pousse le nombre de connexions.

## Menu Aide

- [ ] **Aide** (F1) : ouvrir une page d'aide, en ligne ou embarquée.
- [ ] **Vérifier les mises à jour** : interroger les versions publiées sur GitHub et proposer la
      nouvelle.

## Fenêtre d'ajout et extension

- [ ] **Choix de la file dans la fenêtre d'ajout** : *Plus tard* met toujours dans la file
      principale ; offrir la file du planificateur, comme le « Télécharger plus tard » d'IDM.
- [ ] **Choix de la file depuis l'extension** : le panneau envoie toujours dans la file
      principale ; une option dans *Personnalisation du panneau* pourrait viser le
      planificateur.
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

## Sources et suivi

- [ ] **Date des épisodes** : les sources laissent `date_upload` vide ; la renseigner
      permettrait de préremplir le jour et l'heure de sortie d'un animé suivi.

## Icônes de dossier

- [ ] Les **superpositions** note, genre et logo des gabarits (qui lisent un `.nfo`) ne sont pas
      portées, comme dans l'application Tauri.
