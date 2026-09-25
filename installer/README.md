# Installateur

`anime-dm.iss` fabrique l'installateur Windows avec Inno Setup 6. La publication
(`.github/workflows/release-win32.yml`) le compile à chaque version ; pour le faire à la main :

```
ISCC /DAppVersion=0.01 /DBuildDir=..\build /DMagickDir=..\magick installer\anime-dm.iss
```

`MagickDir` est la version portable d'ImageMagick décompressée (`magick.exe` et ses fichiers de
configuration), téléchargée depuis les publications officielles.
