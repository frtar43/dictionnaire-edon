# Dictionnaire Français-Latin — Georges Édon

Numérisation, océrisation et correction 'partielle' du 
*Dictionnaire français-latin* de Georges Édon.

## Présentation
Ce dépôt contient le texte complet et révisé par IA du dictionnaire 
Édon, nettoyé de ses coquilles d'OCR. C'est un projet 'expérimental'
et il se peut qu'il manque certains accents français ou certains 
mots latins, l'IA aurait pu se tromper, vérifiez vos recherches...

## Compilation
Mettre edon.db et intro.txt à côté de l'exécutable.

## Dépendances Linux
sudo apt install libsqlite3-dev
sudo apt install fonts-noto-color-emoji

### Avec Code::Blocks
1. Ouvrez le fichier `dictionnaire_edon.cbp` dans Code::Blocks.
2. Assurez-vous que les options du compilateur contiennent les flags GTK4 :
   - *Compiler Flags* : `` `pkg-config --cflags gtk4` ``
   - *Linker Flags* : `` `pkg-config --libs gtk4` ``
3. Compilez en mode **Release** (`F9` ou *Build and run*).

### En ligne de commande (GCC)
``bash
gcc -O2 src/main.c $(pkg-config --cflags --libs gtk4) -o 
dictionnaire-edon ./dictionnaire-edon

## Contenu du dépôt
* `EDON.txt` : Texte brut du dictionnaire complet
* `README.md` : Documentation du projet.
* `LICENSE` : Licence CC0 1.0 (Domaine public).

## Licence
Ce travail de transcription et de correction est mis à 
disposition selon les termes de la licence 
[CC0 1.0 Universelle (Domaine Public)](LICENSE). 
Vous pouvez copier, modifier, distribuer et utiliser ces données, 
même à des fins commerciales, sans demander d'autorisation.