# AideServ

Module Anope (style X3 HelpServ) avec deux bots et une base de tickets commune :

- **AideMoi** — assistance sur `#Aide.chat`
- **SignalMoi** — signalements sur `#Signalement.chat`
- Notifications équipe sur `#_BO` (salon verrouillé)
- Copie d’audit sur `#_logs` (salon verrouillé)

Les bots rejoignent les salons verrouillés avec le préfixe `@`, comme les autres services Anope. Un ticket n’est ouvert qu’une fois la demande assez claire (triage en message privé).

## Installation

1. Cloner ce dépôt dans `modules/third/` d’Anope, par exemple :

   ```
   git clone https://github.com/<votre-compte>/anope-modules-entrenous.git modules/third/entrenous
   ```

   Ou copier uniquement le dossier `aideserv/` dans `modules/third/aideserv/`.

2. Reconstruire Anope (`./Config`, `make`, `make install`).

3. Inclure la config dans `anope.conf` :

   ```
   include
   {
       type = "file"
       name = "aideserv.example.conf"
   }
   ```

4. Charger le module : `module { name = "aideserv" }` (déjà dans l’exemple).

Les personnes présentes sur `#_BO` ont les commandes d’équipe. Pour un opertype :

```
commands = "aideserv/helper"
```

(`aideserv/manager`, `aideserv/admin` et `aideserv/*` fonctionnent aussi.)

## Commandes

Utilisateur : `HELP`, `WAIT` / `ATTENDRE`, `STATUS` / `STATUT`, `CANCEL` / `ANNULER`, et sur SignalMoi `REPORT` / `SIGNALER`.

Équipe : `LIST`, `NEXT`, `PICKUP`, `SHOW`, `CLOSE`, `ADDNOTE`, `REASSIGN` (alias FR : `LISTE`, `SUIVANT`, `PRENDRE`, `VOIR`, `FERMER`, `NOTE`, `REASSIGNER`).

Les réponses suivent la langue du compte (`NS SET LANGUAGE fr_FR.UTF-8`) ou `options:defaultlanguage`.
