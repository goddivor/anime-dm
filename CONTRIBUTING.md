# Contribuer à Anime Download Manager

## Modèle de branches

| Branche | Rôle |
|---------|------|
| `master` | Production. **Protégée** : aucun push direct, uniquement via Pull Request. |
| `dev` | Intégration. **Protégée** également : aucun push direct, uniquement via Pull Request. |
| `feature/<slug>` | Nouvelle fonctionnalité (créée **depuis `dev`**). |
| `fix/<slug>` | Correction de bug. |
| `refactor/<slug>` | Refonte sans changement de comportement. |
| `chore/<slug>` | Outillage, dépendances, CI, divers. |
| `docs/<slug>` | Documentation. |

`<slug>` en **kebab-case**, court et descriptif :
`feature/headless-voe`, `fix/mailru-403`, `refactor/gui-split`.

## Flux de travail

```bash
# 1. Partir de dev à jour
git checkout dev && git pull

# 2. Créer la branche de travail
git checkout -b feature/ma-fonction

# 3. Développer + commiter (voir conventions ci-dessous), puis pousser
git push -u origin feature/ma-fonction

# 4. Ouvrir une Pull Request VERS dev (jamais vers master)
```

- Les branches de travail ciblent **toujours `dev`** (via PR).
- `dev` et `master` sont **toutes deux protégées** : aucun push direct, tout passe par PR.
- `master` ne reçoit que des PR **`dev` → `master`** (releases), une fois `dev` stable.

## Conventions de commit

- Format **Conventional Commits** : `type(scope): description`
- Types : `feat`, `fix`, `refactor`, `style`, `docs`, `chore`, `test`, `perf`
- Verbe à l'**impératif**, en **anglais**, minuscule après les deux-points
  - ✅ `feat(gui): add speed limiter dialog`
  - ❌ `Added a dialog` / `ajout du dialogue`
- **Pas** de signature `Co-Authored-By` ni `Generated with`
- **Staging sélectif** : jamais `git add .` / `git add -A`, on nomme les fichiers
- Ne jamais committer de fichiers sensibles (`.env*`, `*.key`, `credentials*`…)

## Commentaires dans le code

- Le code doit être **auto-documenté par le nommage** : pas de commentaire qui paraphrase ce que fait le code.
- Un commentaire n'est autorisé **que sur une fonction** (doc-comment `///`), et seulement si son intention n'est pas évidente.
- Tout commentaire doit être **court, précis, clair et non verbeux** — il dit le *pourquoi*, jamais le *quoi*.
- Pas de commentaire inline (`//`) ni de bloc descriptif en tête de module.

## Pull Requests

- Remplir le **template de PR** (`.github/PULL_REQUEST_TEMPLATE.md`).
- Lier l'issue concernée : `Closes #123`.
- S'assurer que **`cargo build` passe sans warning** et que `cargo test` est vert.
- Pour un changement visuel : joindre une **capture d'écran** avant/après.

## Issues

Deux modèles disponibles à la création (`.github/ISSUE_TEMPLATE/`) :
- 🐛 **Rapport de bug**
- ✨ **Demande de fonctionnalité**
