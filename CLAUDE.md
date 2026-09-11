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
  - `Settings` : `settings.json` (icônes de dossier, modèle, Aniyomi, thème, langue).
  - `FolderIcon` : l'icône du dossier d'un animé, décrite plus bas.
  - `Digest` (SHA-256 par BCrypt), `Image` (décodage par GDI+), `Paths` (`%APPDATA%`), `Text`
    (conversions UTF-8 / UTF-16).
  - `adm_addon.h` : copie de l'en-tête ABI ; **doit rester en phase** avec celui du dépôt des
    addons. `third_party/json.hpp` : nlohmann, versionné faute de gestionnaire de paquets.
- **`app/ui/`** : `MainWindow`, `MenuBar`, `Toolbar`, `Sidebar`, `DownloadsView`, les dialogues
  (`AddDialog`, `AddonsDialog`, `AddonConfigDialog`, `PosterDialog`, `SearchDialog`,
  `SettingsDialog`, `NoticeDialog`, `ConfirmDialog`, `HelpDialogs`), plus `Theme`, `Strings`,
  `Paint`, `Format`, `IconFactory`, `AddSelection`.
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
d'épisodes. Les autres lignes restent au dessin du contrôle, avec un glyphe d'état coloré
par épisode. Le panneau est **reconstruit depuis le modèle** à chaque changement de
structure ou d'état (jamais sur une simple progression) ; la sélection survit à la
reconstruction et `Busy()` fait taire les notifications qu'elle déclenche. Cliquer une ligne
filtre la liste ; le clic droit sur un animé ouvre, ouvre le dossier ou supprime l'animé.

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
  42 pixels. Exporter en bords **hard** : le mode matte laisse un liseré gris.
- **Commentaires** : code auto-documenté ; un commentaire **uniquement sur une fonction**,
  court et précis. Pas de `//` en fin de ligne.
- **Traduction** : tout texte visible passe par `Str(STR_…)`. La table vit dans
  `Strings.h` / `Strings.cpp` ; **le nombre d'identifiants et le nombre d'entrées doivent
  toujours concorder** (le vérifier après chaque ajout). Français et anglais restent synchrones.
- **Couleurs** : toujours `ActiveTheme().Colors()`, jamais de valeur en dur. La palette porte
  `window`, `surface`, `text`, `line`, `accent`, `accentText`, `hover`, `muted`, `ok`, `bad`,
  `header`, `frame`.
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
  (`download-smoke`) doit partir par `open_app` (`powershell.exe -WindowStyle Hidden -Command …
  *> journal`) et se lire ensuite dans son journal.

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
- **Un `desktop.ini` écrit à la main ne rafraîchit jamais Explorer à coup sûr** : il garde
  l'icône en cache par chemin du `.ico`, parfois des minutes. Ce que font FolderIco et l'onglet
  « Personnaliser » : `SHGetSetFolderCustomSettings` avec `FCS_FORCEWRITE`, qui écrit le fichier
  **et** met à jour l'état interne du shell. S'y ajoutent un nom de `.ico` par recette et par
  affiche, `SHCNE_UPDATEIMAGE` sur l'image système du dossier, les notifications par PIDL
  (attributs, élément, dossier parent) et `SHCNE_ASSOCCHANGED`, seul événement qui invalide le
  cache d'icônes.

## Ce qui manque encore

- Le **nombre de connexions** et de téléchargements simultanés sont des constantes de
  `Downloader` ; ils attendent le fichier de réglages pour devenir des options.
- Aucune **limitation de débit**, aucun **planificateur** : les entrées de menu existent,
  pas le comportement.
- Seules les actions de téléchargement (reprendre, arrêter, supprimer…) sont grisées selon
  l'état ; le reste du menu ne l'est pas encore.
- Les **superpositions** note, genre et logo des gabarits (qui lisent un `.nfo`) ne sont pas
  portées, comme dans l'application Tauri.
