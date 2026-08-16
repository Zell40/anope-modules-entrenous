# HelpServ

Module Anope (style X3 HelpServ). **HelpServ** est le service opérateurs ; les utilisateurs parlent à ses bots :

- **HelpServ** — file unique pour l’équipe (`LIST`, `NEXT`, `PICKUP`…) et annonces sur `#_BO` / `#_logs`
- **AideMoi** — assistance utilisateurs sur `#Aide.chat`
- **SignalMoi** — signalements utilisateurs sur `#Signalement.chat`

Les tickets ouverts via AideMoi ou SignalMoi sont toujours notifiés par **HelpServ** sur les salons équipe et logs.

## Installation

1. Cloner ce dépôt dans `modules/third/` d’Anope, par exemple :

   ```
   git clone https://github.com/Zell40/anope-modules-entrenous.git modules/third/entrenous
   ```

   Ou copier uniquement le dossier `helpserv/` dans `modules/third/helpserv/`.

   Si une ancienne copie `aideserv/` est encore dans l’arbre source, **supprime-la**.

2. Reconstruire Anope (`./Config`, `make`, `make install`).

3. Copier `helpserv.example.conf` vers `conf/helpserv.conf` (comme `botserv.conf`), puis dans `anope.conf` :

   ```
   include
   {
   	type = "file"
   	name = "helpserv.conf"
   }
   ```

   Le `module { name = "helpserv" }` et les `service { }` sont déjà dans ce fichier. Ne pas coller le `anope.example.conf` d’une version plus récente d’Anope par-dessus la conf EntreNous.

Les personnes présentes sur `#_BO` ont les commandes d’équipe. Pour un opertype (ex. Helpeur) :

```
commands = "hostserv/* helpserv/helper"
```

## Commandes opérateurs (`/msg HelpServ`)

`LIST` `[HELP|REPORT]` `[UNASSIGNED|ASSIGNED|ME|ALL|CLOSED]`, `NEXT` `[HELP|REPORT]`, `PICKUP`, `SHOW`, `CLOSE`, `ADDNOTE`, `REASSIGN`.

Ajouter un bot sur un salon (indépendant de BotServ) :

```
/msg HelpServ JOIN AideMoi #salon
/msg HelpServ JOIN SignalMoi #salon
/msg HelpServ PART AideMoi #salon
/msg HelpServ BOTLIST
```

AideMoi et SignalMoi apparaissent dans `/bs botlist` mais **ne peuvent pas** être assignés avec BotServ. Seul HelpServ peut les placer :

```
/msg HelpServ JOIN AideMoi #salon
```

Un `/msg BotServ ASSIGN #salon AideMoi` (ou une invitation du bot) est refusé avec un message d’erreur.

Alias FR : `LISTE`, `SUIVANT`, `PRENDRE`, `VOIR`, `FERMER`, `NOTE`, `REASSIGNER`, `AJOUTER`, `RETIRER`, `BOTS`.

## Commandes utilisateurs

- AideMoi : `WAIT` / `ATTENDRE`, `STATUS` / `STATUT`, `CANCEL` / `ANNULER`
- SignalMoi : les mêmes, plus `REPORT` / `SIGNALER`
