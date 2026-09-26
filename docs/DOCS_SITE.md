# The docs site

<https://khyretos.github.io/kk-engine/> is built from this folder with
[MkDocs](https://www.mkdocs.org/) and the Material theme, by
`.github/workflows/docs.yml` on every push to `main`, which pushes the
built site to the `gh-pages` branch. It's free: GitHub
Pages on a public repository.

## Working on it

```bash
pip install -r tools/docs_site/requirements.txt
mkdocs serve           # http://127.0.0.1:8000, rebuilds as you save
mkdocs build --strict  # what CI runs: any warning fails the build
```

- **Pages and menus:** `mkdocs.yml` at the repository root. A new doc in
  `docs/` also needs a line in its `nav:` (and a row in
  [docs/README.md](https://github.com/Khyretos/kk-engine/blob/main/docs/README.md) for people browsing on GitHub).
- **Links out of `docs/`** (`../ROADMAP.md`, `../games/...`) are written as
  ordinary relative links, so they work on GitHub; on the site
  `tools/docs_site/hooks.py` turns them into links to the file on GitHub.
- **The Lua API reference** is generated on every build from the engine's
  `registerFunction` calls by `tools/docs_site/lua_api.py`
  (`python3 tools/docs_site/lua_api.py` prints it). Descriptions come from
  the API table in [SCRIPTING.md](SCRIPTING.md): add a row there when you
  add a table.
- **The front page** is `index.md`; this folder's `README.md` is left out
  of the site.
- **Devlog:** add a section to `devlog/index.md`, newest first.

## First deployment

GitHub Pages has to be switched on once: repository **Settings > Pages >
Build and deployment > Source: Deploy from a branch**, branch `gh-pages`,
folder `/ (root)`. From then on every push to `main` updates the site.
