#!/usr/bin/env python3
"""Serve the design preview and redirect links from its retired worktree."""

import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit, urlunsplit


ROOT = Path(__file__).resolve().parent.parent
LEGACY_DESIGN = "/.worktrees/design-concept/work/design/"
CURRENT_DESIGN = "/work/design/"


class DesignPreviewHandler(SimpleHTTPRequestHandler):
    def send_head(self):
        url = urlsplit(self.path)
        if url.path.startswith(LEGACY_DESIGN):
            target = CURRENT_DESIGN + url.path[len(LEGACY_DESIGN):]
            self.send_response(307)
            self.send_header("Location", urlunsplit(("", "", target, url.query, "")))
            self.send_header("Content-Length", "0")
            self.end_headers()
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
    handler = partial(DesignPreviewHandler, directory=str(ROOT))
    with ThreadingHTTPServer(("127.0.0.1", args.port), handler) as server:
        print(f"Design: http://127.0.0.1:{args.port}/work/design/", flush=True)
        server.serve_forever()


if __name__ == "__main__":
    main()
