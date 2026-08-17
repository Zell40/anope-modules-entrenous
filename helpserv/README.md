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

   Le `module { name = "helpserv" }` et les `service { }` sont déjà dans ce fichier. Dans `anope.conf`, n’ajoute **que** l’include ci-dessus.

   Anope n’applique **que** les `include` écrits dans `anope.conf`. Un include dans `modules.conf` (ou un `module { name = "helpserv" }` vide à côté) est ignoré. Le module relit alors `conf/helpserv.conf` tout seul.

Les personnes présentes sur `#_BO` ont les commandes d’équipe. Pour un opertype (ex. Helpeur) :

```
commands = "hostserv/* helpserv/helper"
```

## Commandes opérateurs (`/msg HelpServ`)

`LIST` `[HELP|REPORT]` `[UNASSIGNED|ASSIGNED|ME|ALL|CLOSED]`, `NEXT` `[HELP|REPORT]`, `PICKUP`, `SHOW`, `CLOSE`, `REOPEN`, `ADDNOTE`, `REASSIGN`.

`LIST` affiche par défaut les tickets **en attente**, **en traitement** (« en attente de traitement par X ») et **fermés** (« Fermé »). Les fermés apparaissent après les tickets encore ouverts. `LIST UNASSIGNED` / `LIST ASSIGNED` / `LIST CLOSED` filtrent. `SHOW` et `REOPEN` (`REOUVRIR`) permettent de consulter ou de relancer un ticket fermé.

Ajouter un bot sur un salon (indépendant de BotServ) :

```
/msg HelpServ JOIN AideMoi #salon
/msg HelpServ JOIN AideMoi +#salon
/msg HelpServ JOIN SignalMoi #salon admin
/msg HelpServ PART AideMoi #salon
/msg HelpServ BOTLIST
```

Réponses automatiques en message privé (persistées, en plus de celles de `helpserv.conf`) :

```
/msg HelpServ AUTOADD AideMoi bannis, je suis banni : Vous pouvez trouver les règles ici : https://www.reseau-entrenous.fr/aide/
/msg HelpServ AUTOADD SignalMoi harcelement : Merci, décrivez les faits sans les discuter en public.
/msg HelpServ AUTOLIST
/msg HelpServ AUTOLIST AideMoi
/msg HelpServ AUTODEL AideMoi 1
/msg HelpServ AUTODEL SignalMoi harcelement
```

Le statut sur le salon se choisit avec un préfixe (InspIRCd) : `~` fondateur, `&` admin, `@` op, `%` halfop, `+` voice, ou `none`. Par défaut c’est `op` (`@`). Sur les salons d’origine (`#Aide.chat`, `#Signalement.chat`, `#_BO`, `#_logs`), ça se règle dans `helpserv.conf` (`help { prefix }`, `report { prefix }`, `staff_prefix`) — et le même préfixe doit être devant le salon dans `service { channels }`.

AideMoi et SignalMoi apparaissent dans `/bs botlist` mais **ne peuvent pas** être assignés avec BotServ. Seul HelpServ peut les placer :

```
/msg HelpServ JOIN AideMoi #salon
```

Un `/msg BotServ ASSIGN #salon AideMoi` (ou une invitation du bot) est refusé avec un message d’erreur.

Alias FR : `LISTE`, `SUIVANT`, `PRENDRE`, `VOIR`, `FERMER`, `REOUVRIR`, `NOTE`, `REASSIGNER`, `AJOUTER`, `RETIRER`, `BOTS`, `AJOUTAUTO`, `SUPPRIAUTO`, `LISTAUTO`.

## Commandes utilisateurs

- AideMoi : `WAIT` / `ATTENDRE`, `STATUS` / `STATUT`, `CANCEL` / `ANNULER`
- SignalMoi : les mêmes (`REPORT` / `SIGNALER` restent optionnels)

Un ticket n’est **pas** ouvert sur un salut ou une demande vide (« j’ai besoin d’aide », « aidez-moi », « help »). AideMoi redemande le problème (pseudo, salon, connexion, erreur). Le ticket s’ouvre seulement quand il reste une vraie description. Les messages de triage restent dans l’historique du ticket. `QUIT` pendant ce dialogue abandonne le triage (aucun ticket).

SignalMoi fonctionne de la même façon : on parle en message privé (qui, où, ce qui s’est passé). Le ticket s’ouvre tout seul, sans taper `REPORT`. Les non-inscrits peuvent demander de l’aide et signaler ; le spam est limité par IP/hôte (flood, reconnexion, quotas horaires).

Les questions d’utilisation reçoivent d’abord **une** page d’aide EntreNous (ou le lien général https://www.reseau-entrenous.fr/aide/), pas une liste de liens. Un incident (erreur, nick pris, « je n’arrive pas… ») ouvre toujours un ticket. D’autres réponses auto peuvent être ajoutées dans `helpserv.conf` (`help { auto { match; reply } }`) ou en IRC avec `AUTOADD`.

Quand un aidant prend un ticket (`NEXT` / `PICKUP`), l’ouvreur est prévenu en message privé (AideMoi ou SignalMoi). S’il est déjà sur `#Aide.chat` ou `#Signalement.chat`, il reçoit le voice (`+v`). Sinon il est invité sur le salon ; le voice est posé dès qu’il rejoint.

Un `PART` / kick du salon d’aide, ou un `QUIT`, n’annule pas le ticket : l’équipe est prévenue et le ticket reste ouvert. Au retour, si le ticket est encore attribué, le voice est remis. `CLOSE` retire le voice.

Les tickets inactifs sont fermés automatiquement après `ticket_expire` (7 jours par défaut). Les tickets fermés sont **conservés** (historique, `LIST CLOSED`, `SHOW`, `REOPEN`). Mettre `ticket_expire = 0` pour ne plus auto-fermer. `ticket_retain` (0 par défaut = jamais) efface ensuite les archives trop anciennes si on le souhaite.

Un rappel automatique est envoyé sur `staff_channel` et `log_channel` toutes les `ticket_reminder` (15 minutes par défaut) tant qu’il reste des tickets en attente ou en traitement. Mettre `ticket_reminder = 0` pour désactiver.
