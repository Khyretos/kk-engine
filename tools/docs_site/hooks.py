"""MkDocs hooks for the KKE docs site (mkdocs.yml, docs/DOCS_SITE.md).

- Links from docs/ to files outside it (../ROADMAP.md, ../games/...) become
  links to the file on GitHub, and images to their raw copy, so the same
  Markdown reads right on GitHub and on the site.
- Every demo's games/<demo>/README.md becomes the page demos/<demo>.md,
  with its links rewritten as if it had been written there: to other docs
  pages, to other demos' pages, or to GitHub.
- The Lua API reference (reference/lua-api.md) is generated at build time
  from the engine's bindings (lua_api.py).
- llms.txt and llms-full.txt, for AI assistants, are generated at the
  site's root (llms.py).
"""

import os
import posixpath
import re
import sys

from mkdocs.structure.files import File

sys.path.insert(0, os.path.dirname(__file__))
import llms  # noqa: E402
import lua_api  # noqa: E402

REPO = "https://github.com/Khyretos/kk-engine"
BLOB = REPO + "/blob/main/"
TREE = REPO + "/tree/main/"
RAW = "https://raw.githubusercontent.com/Khyretos/kk-engine/main/"
IMAGE_EXT = (".png", ".jpg", ".jpeg", ".gif", ".svg", ".webp")

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

# ](target) and ](target "title"), and src="target" in inline HTML.
MD_LINK = re.compile(r"(\]\()([^)\s]+)((?:\s+\"[^\"]*\")?\))")
HTML_SRC = re.compile(r'((?:src|href)=")([^"]+)(")')
FENCE = re.compile(r"^(```|~~~)")


def demo_readmes():
    """{demo name: repo-relative README path} for every games/<demo>/README.md."""
    games = os.path.join(ROOT, "games")
    return {
        name: "games/" + name + "/README.md"
        for name in sorted(os.listdir(games))
        if os.path.isfile(os.path.join(games, name, "README.md"))
    }


# Site page -> the repo folder its Markdown was written in (demo pages).
_SOURCE_DIR = {}


def _outside(page_src, target):
    """Repo-relative path if `target` (from docs/<page_src>) leaves docs/."""
    if re.match(r"^[a-z]+:|^#|^/", target):
        return None
    path, _, anchor = target.partition("#")
    base = _SOURCE_DIR.get(page_src, posixpath.join("docs", posixpath.dirname(page_src)))
    joined = posixpath.normpath(posixpath.join(base, path))
    if joined == "docs" or joined.startswith("docs/"):
        if page_src not in _SOURCE_DIR:
            return None
        if os.path.isdir(os.path.join(ROOT, joined)):
            return joined, anchor, False  # a folder: browse it on GitHub
        # A demo page linking into docs/: a link to that page on the site.
        return posixpath.relpath(joined, posixpath.join("docs", posixpath.dirname(page_src))), anchor, True
    demo = re.fullmatch(r"games/([\w-]+)/README\.md", joined)
    if demo and demo.group(1) in demo_readmes():
        rel = posixpath.relpath("docs/demos/" + demo.group(1) + ".md",
                                posixpath.join("docs", posixpath.dirname(page_src)))
        return rel, anchor, True
    return joined, anchor, False


def _rewrite(hit, image):
    rel, anchor, on_site = hit
    if on_site:
        return rel + ("#" + anchor if anchor else "")
    return _github_url(rel, anchor, image)


def _github_url(rel, anchor, image):
    if image or rel.lower().endswith(IMAGE_EXT):
        return RAW + rel
    base = TREE if os.path.isdir(os.path.join(ROOT, rel)) else BLOB
    return base + rel + ("#" + anchor if anchor else "")


def on_page_markdown(markdown, page, config, files):
    src = page.file.src_uri
    if src in _SOURCE_DIR:
        # "Edit this page" opens the README it was made from.
        page.edit_url = REPO + "/edit/main/" + _SOURCE_DIR[src] + "/README.md"
    out, fenced = [], False
    for line in markdown.split("\n"):
        if FENCE.match(line.lstrip()):
            fenced = not fenced
        if not fenced:
            def md(m):
                hit = _outside(src, m.group(2))
                if not hit:
                    return m.group(0)
                return m.group(1) + _rewrite(hit, False) + m.group(3)

            def html(m):
                hit = _outside(src, m.group(2))
                if not hit:
                    return m.group(0)
                return m.group(1) + _rewrite(hit, m.group(1).startswith("src")) + m.group(3)

            line = MD_LINK.sub(md, line)
            line = HTML_SRC.sub(html, line)
        out.append(line)
    return "\n".join(out)


def on_files(files, config):
    _SOURCE_DIR.clear()
    for name, readme in demo_readmes().items():
        page = "demos/" + name + ".md"
        _SOURCE_DIR[page] = posixpath.dirname(readme)
        with open(os.path.join(ROOT, readme), encoding="utf-8") as f:
            files.append(File.generated(config, page, content=f.read()))
    files.append(File.generated(config, "reference/lua-api.md", content=lua_api.render(ROOT)))
    site = config["site_url"] or llms.SITE
    files.append(File.generated(config, "llms.txt", content=llms.llms_txt(site if site.endswith("/") else site + "/")))
    files.append(File.generated(config, "llms-full.txt", content=llms.llms_full_txt()))
    return files
