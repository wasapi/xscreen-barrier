# xscreen-barrier

Barrière pour la souris entre les écrans.

Prérequis
```
sudo apt install libx11-dev libxfixes-dev libxrandr-dev
```

Installation
```
make
make install
```

Ajouter dans la config i3
```
exec --no-startup-id ~/.local/bin/xscreen-barrier
```
