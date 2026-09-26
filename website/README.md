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

### The proxy shows 502 but the container is healthy

The container's log only shows `127.0.0.1 ... "Wget"` lines: that is its own
healthcheck, and it means the proxy's requests never arrive. Check, in order:

1. `docker compose ps` shows `kke-website` as `healthy` with `0.0.0.0:8080->8080/tcp`.
2. On the host, `curl -I http://localhost:8080/` returns `200`. If not, the
   port is not published (another container or service may hold 8080; change
   the left side of `"8080:8080"` in `docker-compose.yml`).
3. The proxy's upstream port is **8080**, not 80. The image is
   nginx-unprivileged, which cannot bind port 80.
4. If the proxy itself runs in Docker (Nginx Proxy Manager, Traefik, Caddy),
   `localhost` inside it is the proxy's own container. Join both to one
   network (`networks:` block in `docker-compose.yml`, or
   `docker network connect <proxy-network> kke-website`) and use
   `http://kke-website:8080` as the upstream. Alternatively use the host's
   Docker IP (`http://172.17.0.1:8080`, or `host.docker.internal` on Docker
   Desktop).
5. `docker logs kke-website` should then show the proxy's requests with
   the real client IP instead of only the Wget lines.

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
