"""MkDocs hooks for the KKE docs site (mkdocs.yml, docs/DOCS_SITE.md).

- Links from docs/ to files outside it (../ROADMAP.md, ../games/...) become
  links to the file on GitHub, and images to their raw copy, so the same
  Markdown reads right on GitHub and on the site.
- The Lua API reference (reference/lua-api.md) is generated at build time
  from the engine's bindings (lua_api.py).
"""

import os
import posixpath
import re
import sys

from mkdocs.structure.files import File

sys.path.insert(0, os.path.dirname(__file__))
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


def _outside(page_src, target):
    """Repo-relative path if `target` (from docs/<page_src>) leaves docs/."""
    if re.match(r"^[a-z]+:|^#|^/", target):
        return None
    path, _, anchor = target.partition("#")
    joined = posixpath.normpath(posixpath.join("docs", posixpath.dirname(page_src), path))
    if joined == "docs" or joined.startswith("docs/"):
        return None
    return joined, anchor


def _github_url(rel, anchor, image):
    if image or rel.lower().endswith(IMAGE_EXT):
        return RAW + rel
    base = TREE if os.path.isdir(os.path.join(ROOT, rel)) else BLOB
    return base + rel + ("#" + anchor if anchor else "")


def on_page_markdown(markdown, page, config, files):
    src = page.file.src_uri
    out, fenced = [], False
    for line in markdown.split("\n"):
        if FENCE.match(line.lstrip()):
            fenced = not fenced
        if not fenced:
            def md(m):
                hit = _outside(src, m.group(2))
                if not hit:
                    return m.group(0)
                return m.group(1) + _github_url(hit[0], hit[1], False) + m.group(3)

            def html(m):
                hit = _outside(src, m.group(2))
                if not hit:
                    return m.group(0)
                return m.group(1) + _github_url(hit[0], hit[1], m.group(1).startswith("src")) + m.group(3)

            line = MD_LINK.sub(md, line)
            line = HTML_SRC.sub(html, line)
        out.append(line)
    return "\n".join(out)


def on_files(files, config):
    files.append(File.generated(config, "reference/lua-api.md", content=lua_api.render(ROOT)))
    return files
