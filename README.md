# anope-modules-entrenous

Modules Anope tiers pour le réseau **EntreNous**. Un dossier par module.

## Installation

Cloner ce dépôt dans l’arbre source d’Anope :

```
cd /chemin/vers/anope
git clone https://github.com/<votre-compte>/anope-modules-entrenous.git modules/third/entrenous
./Config
make
make install
```

Chaque module a son `*.example.conf` : copiez-le (ou incluez-le) depuis `anope.conf`.

## Modules

| Dossier | Module Anope | Rôle |
| --- | --- | --- |
| [`aideserv/`](aideserv/) | `aideserv` | AideServ — files d’aide (`AideMoi`) et de signalement (`SignalMoi`) |
