# m_youtube

Module BotServ (Jean « reverse » Chevronnet) : quand un lien YouTube est posté
dans un salon avec bot assigné, le bot répond avec le titre, la durée et le
nombre de vues (API YouTube Data v3).

## Compilation

Le module a besoin de **libcurl**. JSON est fourni par **yyjson vendored**
d’Anope (`vendor/yyjson`) — il ne faut **pas** installer une lib yyjson système.

Sur Debian/Ubuntu :

```
apt install libcurl4-openssl-dev
```

Le `#include` et le CMake du module doivent pointer vers Anope, pas vers une
lib externe :

```
#include "yyjson/yyjson.h"
target_link_libraries(${SO} PRIVATE CURL::libcurl vendored_yyjson)
```

## Installation

1. Cloner ce dépôt dans `modules/third/` d’Anope, ou copier `m_youtube/` dans
   `modules/third/m_youtube/`.
2. `./Config && make && make install`
3. Inclure `m_youtube.example.conf` depuis `anope.conf` et renseigner
   `youtube_api_key`.

Le fichier `.so` s’appelle `m_youtube` :

```
module { name = "m_youtube" }
```
