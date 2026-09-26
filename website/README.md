# engine.kreative-kompas.com

The engine's showcase website (Hugo), served together with the docs
(MkDocs, `docs/`, at `/docs/`) from one nginx container. The GitHub Pages
site (khyretos.github.io/kk-engine) stays as a docs-only mirror.

## Run it

```bash
cd website
docker compose up -d --build      # http://localhost:8080
```

Put your reverse proxy in front of port 8080 for the domain and HTTPS.
After pulling new commits, run the same command again to publish them.

## Work on it

```bash
cd website && hugo server          # http://localhost:1313, live reload (Hugo extended 0.134+)
```

The docs aren't in `hugo server`; `mkdocs serve` at the repository root
serves them (docs/DOCS_SITE.md).

## Update it

| To change | Edit |
|---|---|
| A feature on the showcase and its home-page card | `data/features.yaml` (text, points, images, sounds, code) |
| Screenshots and clips | `static/media/` (webp, about 1280 px wide), then list them in `data/features.yaml` |
| The numbers under the hero | `data/stats.yaml` |
| A devlog post | a new `content/devlog/YYYY-MM-title.md` |
| Menu, links, description | `hugo.toml` |
| Look and feel | `assets/css/site.css`, `assets/js/site.js`, `layouts/` |

Screenshots come from the demos running headless (Xvfb and lavapipe);
paid asset-pack files are never committed, only screenshots of scenes.
The style follows kees.kreative-kompas.com: Inter, the electric purple and
the Kreative Kompas orange, glow blobs, dark by default with a light theme.
