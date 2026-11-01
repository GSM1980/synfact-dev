# core: shared sources

`core.s` (the boot copier and the hook bus) and `settings.s` (the SETTINGS
menu's hook and `core_additem`) come from elekloader's core, where one source
served every device with this hook bus. This repository keeps only the
Syntakt, so these two files are all that is left of it here: the Syntakt's
core, [../core-st](../core-st), builds them (`core.s` is in its `sources`, and
its `settings.s` includes `../core/settings.s`).

They have no addresses of their own: `../core-st/mod.json` gives them as
`defsym`, so a missing one fails the build. Comments in them still name the
devices of the original elekloader cores.

Change them only for the Syntakt's core, and run its regressions
([../../docs/SYNTAKT-BUILD.md](../../docs/SYNTAKT-BUILD.md)).
