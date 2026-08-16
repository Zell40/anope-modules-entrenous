# Modèles e-mail Anope (format 2.1 récent)

À copier dans `conf/email/` de l’installation Anope, puis à référencer
depuis le bloc `mail { }` de `anope.conf`.

Les clés plates (`memo_subject`, `registration_message`, …) ne sont plus
lues : il faut des sous-blocs `memo`, `registration`, `password_reset`,
`email_change` avec `subject` et `message { file = "…" }`.
