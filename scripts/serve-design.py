#!/usr/bin/env python3
"""Serve the design preview and redirect links from its retired worktree."""

import argparse
import json
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit, urlunsplit


ROOT = Path(__file__).resolve().parent.parent
LEGACY_DESIGN = "/.worktrees/design-concept/work/design/"
CURRENT_DESIGN = "/work/design/"
DESIGN_ROOT = ROOT / "work/design"


class DesignPreviewHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, gallery_root, **kwargs):
        self.gallery_root = gallery_root
        super().__init__(*args, **kwargs)

    def send_head(self):
        url = urlsplit(self.path)
        if url.path.startswith(LEGACY_DESIGN):
            target = CURRENT_DESIGN + url.path[len(LEGACY_DESIGN):]
            self.send_response(307)
            self.send_header("Location", urlunsplit(("", "", target, url.query, "")))
            self.send_header("Content-Length", "0")
            self.end_headers()
            return None
        target = Path(self.translate_path(self.path)).resolve()
        if not (target.is_relative_to(DESIGN_ROOT) or target.is_relative_to(self.gallery_root)):
            self.send_error(404)
            return None
        return super().send_head()

    def end_headers(self):
        path = urlsplit(self.path).path
        if path.startswith((LEGACY_DESIGN, CURRENT_DESIGN)):
            self.send_header("Cache-Control", "no-store")
        super().end_headers()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8768)
    args = parser.parse_args()
    catalog = json.loads((DESIGN_ROOT / "window-catalog.json").read_text(encoding="utf-8"))
    gallery_root = (DESIGN_ROOT / catalog["galleryBase"]).resolve()
    if ROOT / "build" not in gallery_root.parents:
        parser.error("The design gallery must be under the checkout's build directory")
    handler = partial(DesignPreviewHandler, directory=str(ROOT), gallery_root=gallery_root)
    with ThreadingHTTPServer(("127.0.0.1", args.port), handler) as server:
        print(f"Design: http://127.0.0.1:{server.server_port}/work/design/", flush=True)
        server.serve_forever()


if __name__ == "__main__":
    main()
