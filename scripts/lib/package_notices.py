"""Index the documents present in a deployed notices payload."""
import json
from pathlib import Path

INDEX_NAME = "index.json"


def notice_directory(install, system):
    return Path(install) / ("Sevenzip.app/Contents/Resources/Notices" if system == "Darwin"
                            else "share/licenses/sevenzip-desktop")


def write_notice_index(root):
    root = Path(root)
    if not root.is_dir() or root.is_symlink():
        raise ValueError("Notice directory is missing or is a symlink")
    documents = []
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            raise ValueError(f"Symlink in notices payload: {path.relative_to(root)}")
        if not path.is_file() or path == root / INDEX_NAME:
            continue
        relative = path.relative_to(root).as_posix()
        documents.append({"path": relative, "title": relative.replace("/", " / ")})
    if not documents:
        raise ValueError("Notice payload contains no documents")
    # Put the distribution's overview before individual original documents.
    documents.sort(key=lambda row: (row["path"] != "THIRD_PARTY_NOTICES.md", row["path"]))
    index = root / INDEX_NAME
    index.write_text(json.dumps({"schema": 1, "documents": documents},
                                ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return index
