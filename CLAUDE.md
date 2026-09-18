# anime-dm — instructions projet

**Anime Download Manager** : gestionnaire de téléchargement type IDM pour sites d'animés.
Cette branche est le **port Win32 en C++ qui remplace l'application Tauri**. Les sources
(sites) vivent dans des **DLL natives** chargées à travers une ABI C ; l'application ne
contient aucune logique de source.

## Deux dépôts, côte à côte sous `rust-project/`

- **`anime-dm`** (ce dépôt) : l'application Win32. Zéro logique de source.
- **`anime-dm-addons`** (`github.com/goddivor/anime-dm-addons`) : les trois sources en Rust,
  compilées en DLL. `addons/<lang>/<nom>/` (1 crate = 1 DLL). La branche `repo-win32` sert
  l'index publié ; `scripts/build-repo.sh` l'assemble.

## Branches

- **`feature/win32-cpp`** : la branche de travail. Tout se passe ici.
- **`dev`** et **`master`** : l'ancienne application **Tauri 2 + React 19** (v0.2.1), gardée
  comme référence fonctionnelle. Ne plus y développer, mais **s'y référer** pour savoir ce
  qu'une fonctionnalité fait avant de la porter (`git show dev:src/components/…`).
- Pour toute tâche : sous-branche `feature/* | fix/* | refactor/* | chore/* | docs/*` depuis
  `feature/win32-cpp`, commits locaux, puis **s'arrêter et demander** avant `git push` ou
  `gh pr create`. Validation par l'utilisateur, puis PR ciblant `feature/win32-cpp` et merge.

## Compiler, lancer, vérifier

Prérequis : **MinGW-w64** (`C:\mingw64`), CMake, et Rust avec la cible
`x86_64-pc-windows-gnu` pour les addons. Pas de Visual Studio sur la machine.

```
cmake -S . -B build
cmake --build build --target anime-dm     # doit finir sans erreur NI warning
cmake --build build --target addon-smoke  # sonde de l'ABI des addons
build\addon-smoke.exe <addon.dll> [url]   # métadonnées, réglages, puis la chaîne de lecture
cmake --build build --target download-smoke
build\download-smoke.exe <id> <url épisode> <sortie> [lecteur]   # toute la chaîne, jusqu'au fichier
build\download-smoke.exe --url <url vidéo> <sortie> [referer]    # le transfert seul
```

## Architecture

- **`app/core/`** : ce qui ne touche pas à l'écran.
  - `Http` : le client WinHTTP **unique**. Les addons n'ouvrent jamais de socket eux-mêmes.
  - `Addon` : charge une DLL, vérifie sa version d'ABI, lui remet la table de services et la
    configuration, déballe l'enveloppe `{"ok":…}` / `{"error":…}`.
  - `AddonStore` : registre disque, index du magasin, installation.
  - `Downloader` : l'ordonnanceur des téléchargements (trois à la fois, huit connexions
    chacun) ; il résout l'épisode par la source, puis délègue à `Transfer` et remonte des
    `DownloadEvent` à la fenêtre par `PostMessage`.
  - `Transfer` : les deux façons de rapatrier une vidéo, décrites plus bas. `Playlist`
    analyse le sous-ensemble HLS utile ; `Cipher` déchiffre l'AES-128 des segments.
  - `Queue` : la file entre deux sessions (`downloads.json`), épisodes et groupes d'animés.
    Les affiches vivent à côté, dans `posters/`, nommées d'après le SHA-256 de la page.
  - `Settings` : `settings.json` (icônes de dossier, modèle, Aniyomi, thème, langue, peau de
    la barre d'outils, presse-papiers, dossier à rappeler, démarrage avec Windows, navigateurs
    intégrés, limites du moteur).
  - `Bridge` : ce que l'application fait pour l'extension de navigateur, décrit plus bas.
    `Autostart` : la valeur `Run` de HKCU qui lance l'application à l'ouverture de session.
  - `FolderIcon` : l'icône du dossier d'un animé, décrite plus bas.
  - `Digest` (SHA-256 par BCrypt), `Image` (décodage par GDI+), `Paths` (`%APPDATA%`), `Text`
    (conversions UTF-8 / UTF-16).
  - `adm_addon.h` : copie de l'en-tête ABI ; **doit rester en phase** avec celui du dépôt des
    addons. `third_party/json.hpp` : nlohmann, versionné faute de gestionnaire de paquets.
- **`app/ui/`** : `MainWindow`, `MenuBar`, `Toolbar`, `Sidebar`, `DownloadsView`, les dialogues
  (`AddDialog`, `AddonsDialog`, `AddonConfigDialog`, `PosterDialog`, `SearchDialog`,
  `SettingsDialog`, `NoticeDialog`, `ConfirmDialog`, `HelpDialogs`), plus `Theme`, `Strings`,
  `Paint`, `Format`, `IconFactory`, `FileIcons`, `AddSelection`, `FolderPicker`.
- **`app/host/adm_host.cpp`** : l'hôte de messagerie native (`adm-host.exe`), décrit plus bas.
- **`extension/`** : l'extension de navigateur, une seule base de code pour Chromium et Firefox.
- **`tools/addon_smoke.cpp`** : éprouve l'ABI sans lancer l'application.
- **`tools/download_smoke.cpp`** : éprouve le moteur de téléchargement, avec ou sans source.

## Le moteur de téléchargement

Pas de ffmpeg : le moteur fait ce qu'IDM fait, en requêtes HTTP.

1. **Sondage** : une requête `Range: bytes=0-0` donne la taille et dit si l'hôte honore les
   plages.
2. **Segmentation dynamique** : le fichier part en un seul segment ; chaque connexion qui se
   libère **coupe en deux le plus gros segment restant** (jamais sous 1 Mo). Chaque segment
   écrit son propre fichier `<n>.part` dans `%APPDATA%\anime-dm\parts\<id>\`.
3. **Positions sauvegardées** toutes les trois secondes dans `state.json` : un arrêt, une
   fermeture ou une coupure reprennent là où ils en étaient. La vidéo est **re-résolue** à
   chaque reprise (les URL des hébergeurs expirent) ; la reprise n'est acceptée que si la
   taille n'a pas changé.
4. **Assemblage** : les parts sont concaténées dans l'ordre vers le fichier final, puis le
   dossier des parts est supprimé.

Une **liste HLS** (`.m3u8`) suit le même chemin : ses segments sont les parts, huit
connexions les tirent en parallèle, l'AES-128 est défait au passage, et le fichier final
porte `.ts` (ou `.mp4` s'il s'agit de MP4 fragmenté). Quand un hébergeur offre à la fois un
fichier direct et une liste, **le fichier direct passe en premier** : ce sont les plages
d'octets qui accélèrent.

Le moteur ne touche jamais à l'interface : tout remonte en `DownloadEvent` posté à la
fenêtre. **Arrêter** garde les parts ; **Supprimer** les jette.

## Les icônes de dossier

Les **recettes ImageMagick** de l'application Tauri (héritées de RightClickFolderIconTools)
sont reprises **à l'identique** dans `FolderIcon.cpp`, transcrites automatiquement depuis
`dev:src-tauri/src/foldericon.rs` ; les calques vivent dans `resources/folder-templates/images`
(cherchés à côté de l'exécutable, puis un cran au-dessus pour un build de développement).
L'application pilote `magick.exe` (embarqué dans `resources/folder-templates/bin`, sinon le
`PATH`) sans fenêtre, et **met en cache** chaque `.ico` par empreinte de l'affiche et recette
dans `icon-cache/`. La pose passe par `SHGetSetFolderCustomSettings` (voir les pièges) : `.ico` caché sous un
nom propre à la recette, `desktop.ini` caché et système, dossier en lecture seule. L'option **Aniyomi** écrit `cover.jpg`
et `.nomedia`. Tout cela tourne hors du fil d'interface et remonte par `PostMessage`.

## Le panneau Catégories

Un `TreeView` dont les lignes d'animé sont **dessinées à la main** (`Sidebar::DrawAnimeRow`,
sur `CDRF_SKIPDEFAULT`) : chevron, affiche 34 × 48 aux coins arrondis, titre, nombre
d'épisodes. Les autres lignes sont dessinées de même (`DrawSimpleRow`), une ligne d'épisode
portant l'icône que Windows donne à son type de fichier. Le panneau est **reconstruit depuis le modèle** à chaque changement de
structure ou d'état (jamais sur une simple progression) ; la sélection survit à la
reconstruction et `Busy()` fait taire les notifications qu'elle déclenche. Cliquer une ligne
filtre la liste ; le clic droit sur un animé ouvre, ouvre le dossier ou supprime l'animé.

## La liste des fichiers

Une `ListView` en mode rapport dont les lignes sont **peintes à la main**
(`MainWindow::DrawRow`, sur `CDRF_SKIPDEFAULT`) : le style visuel imposait sinon son bleu
pâle sur la ligne choisie. Chaque cellule est découpée sur la colonne que décrit l'en-tête,
la cellule d'état d'un transfert en cours devient une barre de progression, et le nom du
fichier est précédé de l'icône de son type. Le surlignage démarre après cette icône, comme
dans une liste de Windows. Les traits de grille sont tracés au post-dessin, par-dessus.

Les icônes de type viennent de la **liste d'images du shell** (`FileIcons`), celle
d'Explorer : `SHGetFileInfoW` avec `SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX`, donc
l'extension suffit et le fichier n'a pas besoin d'exister. Cette liste appartient au shell,
elle ne se détruit pas ; les index sont mis en cache par extension.

## L'ajout d'un téléchargement

Trois fenêtres, à la manière d'IDM (`AddDialog.cpp`, une structure `Flow` partagée) :

1. **L'adresse** (`IDD_ADD_URL`) : *Adresse* et *Source*, OK et Annuler à droite. Le lien du
   presse-papiers se colle à l'ouverture (réglage `clipboardUrl`, une case dans Options). La
   source se choisit d'après l'adresse : chaque bibliothèque est interrogée hors du fil
   d'interface pour connaître le site qu'elle sert (`baseUrl` de `adm_metadata`), et l'adresse
   saisie sélectionne celle qui le revendique ; une source qui ne correspond pas lève une
   confirmation. OK grise la fenêtre, son bouton n'affiche qu'une ellipse pendant la lecture
   de la page, et la suivante n'arrive qu'une fois les épisodes obtenus.
2. **Les informations** (`IDD_ADD_INFO`) : URL en lecture seule, icône de dossier, *Enregistrer
   sous*, *Rappeler ce chemin* (réglages `rememberPath` et `savePath`, le dossier proposé aux
   ajouts suivants), l'affiche et le nombre d'épisodes à droite, un bouton *Épisodes* qui
   ouvre la troisième fenêtre. *Plus tard* dépose la sélection dans la file sans la confier au
   moteur (`AddRequest::later`), *Démarrer* la lance.
3. **Les épisodes** (`IDD_ADD_EPISODES`) : une grille de cinq par ligne (`LVS_SMALLICON`, cases
   posées à la main par `ListView_SetItemPosition`, voir les pièges), le champ de plages
   (`1-5,12`) et les cases qui s'écrivent l'un l'autre sous un drapeau `filling`, le lecteur
   global et le lecteur par épisode au clic droit.

Les champs de ces fenêtres font 12 unités de haut, la hauteur d'une liste déroulante ; les
boutons 52 × 14.

## Le menu contextuel de la liste

Ses entrées suivent la sélection comme la barre d'outils : *Arrêter* pour un transfert en
cours, *Ouvrir* pour un fichier terminé, *Reprendre* pour un épisode arrêté, en échec ou
terminé. *Reprendre* est un sous-menu : *Automatique* puis les lecteurs que la source liste
pour l'épisode, le lecteur en cours coché. Le moteur remonte ces noms (`DownloadEvent::players`)
dès qu'il a la liste, avant même le premier essai ; l'épisode les garde (`players` dans
`downloads.json`). Choisir un lecteur relance les épisodes arrêtés ou en échec là où ils en
étaient, et **repart de zéro pour un épisode terminé** : un lecteur sert parfois une copie
tronquée là où un autre a la vidéo entière.

## La fenêtre Options

`SettingsDialog.cpp`, à la manière de la configuration d'IDM : une fenêtre « Configuration
d'Anime Download Manager » à **onglets dessinés à la main** (le `SysTabControl32` ne se laisse
pas assombrir), chaque page étant un dialogue enfant (`DS_CONTROL`) posé dans le corps encadré,
peint couleur `window` pour se lire comme la feuille sous l'onglet choisi. OK applique, Annuler
ne touche à rien.

1. **Général** : l'icône et le titre « Intégration au navigateur / Système » soulignés, le
   démarrage à l'ouverture de session (`Autostart`), le presse-papiers, la liste à cases
   des navigateurs auxquels l'hôte se déclare (`settings.browsers`, tous par défaut), puis le
   bouton *Éditer…* du panneau de téléchargement (`IDD_PANEL` : mode complet ou mini avec un
   aperçu peint de chacun, sur la page, au survol des liens).
2. **Enregistrer sous** : le dossier de téléchargement et sa case « Proposer ce dossier », les
   icônes de dossier avec leur modèle, Aniyomi.
3. **Téléchargements** : les téléchargements simultanés (1 à 10) et les connexions par
   téléchargement (1 à 16), avec leurs compteurs ; `Downloader::SetLimits` les reçoit, le
   premier joue aussitôt, le second aux prochains transferts.

La fenêtre principale applique ce qui sort du dialogue (`ApplySettings`) : registre `Run`,
`bridge::RegisterHost` par navigateur, limites du moteur, puis `settings::Save`.

## Le pont avec le navigateur

L'extension (`extension/`, MV3, Chromium et Firefox) ne parle jamais au réseau : elle
s'adresse à **l'hôte de messagerie native** `adm-host.exe` (`app/host/adm_host.cpp`), que le
navigateur lance à côté de l'extension et auquel il parle par stdin/stdout (quatre octets de
longueur puis un document JSON). L'hôte répond à trois requêtes : `ping`, `sources` (il relit
`%APPDATA%\anime-dm\sources.json`) et `add{url, episode}`.

- **Déclaration** : à chaque démarrage et après le magasin d'addons, `bridge::RegisterHost`
  écrit les manifestes sous `%APPDATA%\anime-dm\host` (`com.animedm.host.json` pour la famille
  Chromium, `.firefox.json` pour Firefox) et les clés `NativeMessagingHosts` de HKCU des
  navigateurs cochés dans Options ; les autres sont retirées. L'identifiant Chromium est fixé
  par la `key` du manifeste (`kajalpjiomebkclalgjggcgjeiibkfcg`, clé privée hors dépôt),
  celui de Firefox vaut `adm@animedm.app` (`BridgeProtocol.h`).
- **Installation de l'extension** : les mêmes cases demandent au navigateur d'installer
  l'extension, comme IDM. Famille Chromium : clé `<navigateur>\Extensions\<id>` avec
  l'`update_url` du Chrome Web Store ; le navigateur la télécharge à son prochain démarrage et
  demande à l'utilisateur de l'activer. Firefox : valeur `<id>` sous
  `Software\Mozilla\Firefox\Extensions` pointant sur le XPI **signé** livré dans
  `resources\extension\adm@animedm.app.xpi`, écrite seulement si le fichier existe. Tant que
  l'extension n'est pas publiée sur le Web Store ni signée par Mozilla, ces clés ne produisent
  rien : Chrome ignore sur Windows toute extension hors magasin, Firefox tout XPI non signé.
  Le bouton *Installer l'extension…* d'Options ouvre la page du magasin (`kChromiumStoreUrl`).
- **Les sites servis** : `bridge::WriteSources` charge chaque source hors du fil d'interface
  et écrit `sources.json` (id, nom, langue, `site`, `animePattern`, `episodePattern`,
  `animeFromEpisode`). Les deux motifs sont des expressions régulières sur le chemin de la
  page, déclarées par la source dans `adm_metadata` ; `animeFromEpisode` est le remplacement
  qui ramène une page d'épisode à son animé (`$1`). Une source sans motif voit tout son site
  proposé à l'ajout.
- **Dans la page** : `content.js` pose, dans une racine fantôme, un bouton dans le coin d'une
  page d'animé (« Télécharger avec ADM ») ou d'épisode (« Télécharger cet épisode avec ADM »),
  et le même panneau **au survol de tout lien** du site qui mène à l'un ou l'autre (affiche de
  la page d'accueil, numéro d'épisode d'une fiche), posé sur l'affiche que le lien enveloppe ou
  recouvre. Le réglage `panel` de `sources.json` (mode `full` ou `mini`, `onPage`, `onLinks`)
  vient d'Options › Général › Éditer… ; `background.js` garde sites et réglage une minute, pose
  le badge « ADM » et envoie `add`.
- **Remise à l'application** : l'hôte cherche la fenêtre `AnimeDmMainWindow` et lui remet un
  `WM_COPYDATA` (marque `ADM1`, JSON `{"kind":"add","url","episode"}`) ; si elle n'existe pas,
  il lance `anime-dm.exe --add <url> [--episode <url>]`. Le mutex `Local\AnimeDm.Instance`
  garantit l'instance unique : un second lancement transmet ses arguments au premier et
  s'efface. La fenêtre refuse un ajout tant qu'un dialogue modal est ouvert, sinon elle ouvre
  le flux d'ajout sur l'adresse, choisit la source qui revendique le site, lit la page sans
  attendre OK et ne coche que l'épisode nommé.

Pour éprouver dans Edge sans toucher au profil de l'utilisateur :
`msedge.exe --user-data-dir=<profil de test> --load-extension=<dépôt>\extension`. Edge lit
les scripts de l'extension **au chargement de celle-ci**, pas à chaque page : après une
modification, relancer Edge (et vider `Service Worker` dans le profil si le `background.js`
reste l'ancien).

## Le modèle d'addons

Une source est une **bibliothèque native**, pas un module WASM. Le bac à sable a été
abandonné parce que le magasin ne sert que les addons de l'auteur ; deux garde-fous rendent
cette promesse structurelle :

1. **L'adresse de l'index est figée dans le binaire** (`AddonStore.cpp`), pas une liste que
   l'utilisateur modifie.
2. **Chaque bibliothèque porte un SHA-256** dans l'index, vérifié avant même l'écriture sur
   le disque.

L'ABI (`adm-abi` côté Rust, `adm_addon.h` côté C++) tient en peu de chose : `adm_abi_version`,
`adm_init` (table de services de l'hôte), `adm_set_config`, `adm_free`, puis les neuf points
d'entrée de source (`adm_metadata`, `adm_preferences`, `adm_anime_details`, `adm_episode_list`,
`adm_hoster_list`, `adm_video_list`, `adm_popular`, `adm_latest`, `adm_search`). JSON à
l'aller comme au retour. **Ce qu'une bibliothèque alloue, elle seule le libère** : les deux
côtés ne partagent pas d'allocateur.

Registre sur disque, un dossier par source :
`%APPDATA%\anime-dm\addons\<id>\{addon.dll, meta.json, icon.png, config.json}`.

Les réglages qu'une source déclare (`adm_preferences`) engendrent le formulaire de
configuration ; l'application les stocke dans `config.json` et les réinjecte au chargement.

## Règles de travail (IMPORTANT)

- **Commits** : conventional commits **en anglais**, impératif ; staging sélectif (jamais
  `git add .`) ; **aucune** signature `Co-Authored-By` ou « Generated with » ; pas de push
  automatique. Toujours passer par le skill `/commit`.
- **Pas d'emoji dans le code**, ni dans les messages. Les icônes de l'interface sont des
  glyphes de la police système **Segoe Fluent Icons** (Segoe MDL2 Assets sur Windows 10),
  rendus par `IconFactory` dans des listes d'images ; les tracés **lucide** (licence ISC) de
  l'application React restent en repli si la police manque. La barre d'outils accepte aussi
  les **peaux d'IDM** (`.tbi` + bandes BMP de douze boutons) déposées dans
  `%APPDATA%\anime-dm\toolbar` ou `resources\toolbar`, choisies par Affichage › Barre
  d'outils (`ToolbarSkin.cpp`) ; Addons et Rechercher, qu'IDM n'a pas, gardent un glyphe.
  Enfin, les **packs de sprites** : un sous-dossier de `resources\toolbar` avec un `pack.json`
  (nom, `default`) et, par bouton, une planche BMP 24 bits de l'outil maison sprite-animator avec
  son JSON, nommée d'après le bouton (`add`, `resume`, `stop`, `stop-all`, `remove`,
  `remove-all`, `options`, `schedule`, `addons`, `search`). Chaque sprite joue vers sa dernière
  image au survol et se grise quand le bouton est désactivé. Le pack **Classique Rem** est celui
  par défaut ; il prête ses sprites Addons et Rechercher à tout autre choix (peaux IDM, police),
  et un bouton qu'il n'a pas encore garde son glyphe. Glyphes et sprites partagent une case de
  36 pixels. Exporter en bords **hard** : le mode matte laisse un liseré gris.
- **Commentaires** : code auto-documenté ; un commentaire **uniquement sur une fonction**,
  court et précis. Pas de `//` en fin de ligne.
- **Traduction** : tout texte visible passe par `Str(STR_…)`. La table vit dans
  `Strings.h` / `Strings.cpp` ; **le nombre d'identifiants et le nombre d'entrées doivent
  toujours concorder** (le vérifier après chaque ajout). Français et anglais restent synchrones.
- **Couleurs** : toujours `ActiveTheme().Colors()`, jamais de valeur en dur. La palette porte
  `window`, `panel`, `menu`, `surface`, `text`, `line`, `accent`, `accentText`, `hover`,
  `muted`, `ok`, `bad`, `header`, `frame`, `panelFrame`. **Chaque valeur est relevée sur une capture d'IDM
  6.43**, au pixel : sombre `window` et `surface` `#393939`, `panel` et `menu` `#202020`,
  `header` `#191919`, `line` `#565656`, texte `#FFFFFF`, contour de liste `#7A7E86`,
  contour du panneau `#CBCBCB`, survol `#384858` ;
  clair `window`, `panel`, `menu` et `header` `#FFFFFF`, `surface` `#F0F0F0`, `line` `#D8D8D8`.
  La sélection vaut `#0078D7` sur texte blanc dans les deux thèmes.
  Ne pas inventer une teinte : la mesurer sur IDM.
- **Réseau** : jamais sur le fil d'interface. Fil séparé, résultat renvoyé par `PostMessage`,
  boutons grisés pendant l'opération.
- **Captures d'écran** : les prendre soi-même via le MCP (voir plus bas), ou laisser
  l'utilisateur les fournir. Ne jamais affirmer un rendu sans l'avoir vu.

## Piloter la machine Windows (MCP `perso`)

Le MCP passe par SSH et tourne en **session 0** (service, sans bureau) ; la session graphique
de l'utilisateur est la **session 2**. Conséquences :

- **`open_app`** lance un programme dans la session 2 (c'est le seul chemin pour qu'une
  fenêtre s'affiche). **`close_app`** le ferme. **`screenshot`** fonctionne.
- `run_powershell` sert à compiler, lire des fichiers, inspecter ; ses fenêtres n'apparaissent
  jamais à l'écran.
- **Le Planificateur de tâches de cette machine est bloqué** : il accepte les commandes et
  n'exécute rien. Ne pas bâtir dessus.
- **Pousser depuis Windows** n'est possible que par `open_app` : le gestionnaire
  d'identifiants refuse de s'ouvrir en session 0.
- Fermer l'application avant de recompiler, sinon l'éditeur de liens bute sur le fichier
  verrouillé (« Permission denied »).
- **Tout processus lancé par `run_powershell` meurt avec le script** : un test long
  (`download-smoke`, une compilation complète) doit partir par `open_app`
  (`powershell.exe -WindowStyle Hidden -Command … *> journal`) et se lire ensuite dans son journal.
- **Capturer la fenêtre, pas l'écran** : l'utilisateur travaille souvent sur la machine. Un
  script lancé par `open_app` capture la fenêtre de l'application et ses dialogues par
  `PrintWindow` (drapeau `PW_RENDERFULLCONTENT`), même cachés derrière d'autres fenêtres, sans
  toucher au focus ni au pointeur ; un menu contextuel ne se laisse pas imprimer et se prend par
  `CopyFromScreen` sur son rectangle, dans le même script que celui qui l'a ouvert, car tout
  nouveau lancement le referme.
- **Remplir un champ d'un autre processus** passe par `SendMessage(WM_SETTEXT)`, pas par
  `SetWindowText` : celui-ci n'atteint pas le contrôle, seule sa copie côté système change, et
  `GetWindowText` la relit sans que l'application voie rien. Les coordonnées d'un script
  PowerShell ne concordent avec celles de l'application (PerMonitorV2) qu'après
  `SetProcessDPIAware`, et un `.ps1` sans BOM est lu en ANSI par PowerShell 5 : pas d'accents
  dans les comparaisons de titres.

## Pièges de la chaîne, durement acquis

- **MinGW n'a pas la surcharge `wstring` des flux de fichiers** (extension MSVC) : passer par
  `std::filesystem::path`.
- **`BCryptHash` n'est pas déclaré** par les en-têtes MinGW : utiliser
  `BCryptCreateHash` / `BCryptHashData` / `BCryptFinishHash`.
- **`WIN32_LEAN_AND_MEAN` exclut les en-têtes COM** : inclure `objbase.h` et lier `ole32`.
- **GCC exige `-municode`** pour accepter `wWinMain` ; MSVC le détecte seul.
- **Un contrôle `STATIC` répond `HTTRANSPARENT`** : la souris le traverse. Pour recevoir les
  clics, intercepter `WM_NCHITTEST` dans le sous-classement et renvoyer `HTCLIENT`.
- **`AlphaBlend` vit dans `msimg32`**, à lier explicitement.
- **La barre de menus n'est jamais assombrie** par `SetPreferredAppMode`, quel que soit le
  réglage : ses entrées de premier niveau sont en dessin propriétaire.
- **L'en-tête d'une `ListView` garde la couleur de texte du mode clair** malgré
  `DarkMode_ItemsView` : un sous-classement force la couleur au dessin personnalisé.
- **`LVS_EX_GRIDLINES` ne se dessine qu'en une couleur claire figée** : les séparateurs de
  colonnes sont tracés à la main au passage post-dessin.
- **`SetPreferredAppMode` et `FlushMenuThemes` sont non documentés** (ordinaux 135 et 136 de
  `uxtheme.dll`). C'est ce qu'emploient tous les logiciels Win32 à thème sombre ; le code se
  dégrade proprement s'ils disparaissent.
- **Les en-têtes GDI+ utilisent `min` et `max`** que `NOMINMAX` supprime : déclarer
  `using std::min; using std::max;` avant de les inclure.
- **`Header_GetItemRect` répond dans les coordonnées de l'en-tête**, et la liste déplace la
  fenêtre d'en-tête vers la gauche pour défiler : tout rectangle lu dans l'en-tête passe par
  `MapWindowPoints(header, list)` avant d'être dessiné. Sinon les cellules peintes à la main
  restent figées pendant que l'en-tête bouge, et les défilements par bande laissent des traînées.
  Liste et arbre repeignent d'ailleurs tout après chaque mouvement de barre, à travers leur
  double tampon : le contrôle copie ses pixels et ne repeint qu'une bande, et cette copie ne
  concorde jamais tout à fait avec un dessin à la main.
- **`LVM_SETICONSPACING` est borné par le contrôle** à la largeur qu'il juge nécessaire à une
  case (147 px pour « Ep 001 » en petites icônes) : pour une grille de cinq, poser les cases
  soi-même par `ListView_SetItemPosition`, sans `LVS_AUTOARRANGE`.
- **`HDN_ITEMCHANGING` permet de borner la largeur d'une colonne** en corrigeant `cxy` dans
  la notification, plutôt qu'en la refusant.
- **Un `desktop.ini` écrit à la main ne rafraîchit jamais Explorer à coup sûr** : il garde
  l'icône en cache par chemin du `.ico`, parfois des minutes. Ce que font FolderIco et l'onglet
  « Personnaliser » : `SHGetSetFolderCustomSettings` avec `FCS_FORCEWRITE`, qui écrit le fichier
  **et** met à jour l'état interne du shell. S'y ajoutent un nom de `.ico` par recette et par
  affiche, `SHCNE_UPDATEIMAGE` sur l'image système du dossier, les notifications par PIDL
  (attributs, élément, dossier parent) et `SHCNE_ASSOCCHANGED`, seul événement qui invalide le
  cache d'icônes.

## Ce qui manque encore

- Aucune **limitation de débit**, aucun **planificateur** : les entrées de menu existent,
  pas le comportement.
- Seules les actions de téléchargement (reprendre, arrêter, supprimer…) sont grisées selon
  l'état ; le reste du menu ne l'est pas encore.
- Les **superpositions** note, genre et logo des gabarits (qui lisent un `.nfo`) ne sont pas
  portées, comme dans l'application Tauri.
