# engine.kreative-kompas.com: the showcase (Hugo, website/) and the docs
# (MkDocs, docs/ at /docs/) in one small nginx image. Build from the
# repository root (website/docker-compose.yml does):
#   docker build -f website/Dockerfile -t kke-website .

# --- The showcase site (Hugo extended, pinned) ---
FROM debian:bookworm-slim AS site
ARG HUGO_VERSION=0.134.3
RUN apt-get update -qq && apt-get install -y -qq --no-install-recommends ca-certificates curl >/dev/null \
 && curl -fsSL "https://github.com/gohugoio/hugo/releases/download/v${HUGO_VERSION}/hugo_extended_${HUGO_VERSION}_linux-amd64.tar.gz" \
    | tar -xz -C /usr/local/bin hugo \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY website/ ./
ARG SITE_URL=https://engine.kreative-kompas.com/
RUN hugo --minify --gc --baseURL "${SITE_URL}" -d /out

# --- The docs (the same MkDocs site as GitHub Pages, served at /docs/) ---
FROM python:3.12-slim AS docs
ENV NO_MKDOCS_2_WARNING=true PIP_DISABLE_PIP_VERSION_CHECK=1 PIP_ROOT_USER_ACTION=ignore
WORKDIR /src
COPY tools/docs_site/requirements.txt tools/docs_site/requirements.txt
RUN pip install --no-cache-dir -q -r tools/docs_site/requirements.txt
COPY mkdocs.yml ./
COPY tools/docs_site/ tools/docs_site/
COPY docs/ docs/
# The Lua API reference is generated from the engine's bindings.
COPY engine/src/ engine/src/
COPY games/template/ games/template/
ARG SITE_URL=https://engine.kreative-kompas.com/
RUN KKE_DOCS_SITE_URL="${SITE_URL}docs/" mkdocs build --strict -d /out/docs

# --- Serve (unprivileged nginx on port 8080) ---
FROM nginxinc/nginx-unprivileged:1.27-alpine
COPY website/nginx.conf /etc/nginx/conf.d/default.conf
COPY --from=site /out /usr/share/nginx/html
COPY --from=docs /out/docs /usr/share/nginx/html/docs
EXPOSE 8080
HEALTHCHECK --interval=30s --timeout=3s CMD wget -qO /dev/null http://127.0.0.1:8080/ || exit 1
