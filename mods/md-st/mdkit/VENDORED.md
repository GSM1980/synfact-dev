# mdkit, as vendored

- Source: https://github.com/soejrd/mdkit
- Commit: `96157fe4fbda17bf4cae3168f52437d8232e6570` (2026-10-22, "Refactor README for clarity")
- Licence: CC0 1.0 Universal (`LICENSE`, `NOTICE`)
- Copied: `src/` (every voice, without the `voice.mk` build fragments) and `include/`,
  unchanged; mdkit's README as `UPSTREAM-README.md`. Its tests, tools, examples and web
  player are not copied.

Nothing in this folder is edited. `../tools/md_port.py` reads it and writes the Syntakt's
versions to `../audio/voices/`; `tests/test_md_st.py` compiles these files, as they are,
as the reference every ported voice must equal word for word.
