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

Les modèles d’e-mail FR (format `mail { memo { … } }` d’Anope 2.1 récent) sont dans [`email/`](email/). Copiez-les vers `conf/email/` de l’installation.

## Modules

| Dossier | Module Anope | Rôle |
| --- | --- | --- |
| [`helpserv/`](helpserv/) | `helpserv` | **HelpServ** (ops) + bots **AideMoi** / **SignalMoi** |
| [`m_youtube/`](m_youtube/) | `m_youtube` | BotServ — titre / durée / vues des liens YouTube |
