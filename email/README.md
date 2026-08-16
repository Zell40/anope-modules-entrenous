# Modèles e-mail Anope (format 2.1 récent)

À copier dans `conf/email/` de l’installation Anope, puis à référencer
depuis le bloc `mail { }` de `anope.conf`.

Les clés plates (`memo_subject`, `registration_message`, …) ne sont plus
lues : il faut des sous-blocs `memo`, `registration`, `password_reset`,
`email_change` avec `subject` et un ou plusieurs `message { file = "…" }`.

Pour un mail HTML (couleurs / logo du site) **en plus** du texte brut,
ajoutez un second bloc `message` :

```
		message
		{
			file = "email/memo.txt"
		}
		message
		{
			content_type = "text/html; charset=UTF-8"
			file = "email/memo.html"
		}
```

Même chose pour `registration`, `password_reset` et `email_change`.
Sans le fichier HTML, Anope refuse de démarrer : ne déclarez le second
bloc que lorsque les `.html` sont bien dans `conf/email/`.
