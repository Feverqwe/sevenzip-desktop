#!/usr/bin/env python3
"""Capture existing UI scenarios with the host's native Qt color schemes."""
import argparse
import html
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys
import shutil
import tempfile
sys.dont_write_bytecode = True
from datetime import datetime, timezone
from lib.hyperv_source import runner_context
from lib.build_paths import checked_output, directory as workspace_directory, environment as workspace_environment
from lib.runtime_layout import codec_runtime

ROOT = Path(__file__).resolve().parent.parent
CATALOG = ROOT / "scripts/config/ui-review.json"


def review_metadata(output, run, shot):
    data = json.loads((output / shot["report"]).read_text(encoding="utf-8"))
    captions = []
    pages = []
    def visit(node):
        if node.get("visible") and node.get("class") in {
            "QPushButton", "QToolButton", "QCheckBox", "QRadioButton", "QGroupBox"
        }:
            captions.append(node.get("text", ""))
        if node.get("visible") and node.get("class") == "QMenu":
            captions.extend(action.get("text", "") for action in node.get("actions", []))
        if node.get("visible") and node.get("name", "").endswith("Page"):
            pages.append(node["name"])
        for child in node.get("children", []):
            visit(child)
    visit(data)
    text = " ".join(captions)
    language = data.get("capture", {}).get("language", "")
    if language in {"-", "en"}:
        language = "en"
    elif language != "ru":
        language = "ru" if re.search("[А-Яа-яЁё]", text) else (
            "en" if re.search("[A-Za-z]{2}", text) else "unknown")
    key = shot.get("window") or shot["class"]
    if key in {"QDialog", "QMessageBox", "QWidget", "QMenu"}:
        key = run["id"] + "/" + key
    if pages:
        key += "/" + ",".join(sorted(pages))
    return language, key


def gallery(output, runs):
    catalog = {row["id"]: row for row in json.loads(CATALOG.read_text(encoding="utf-8"))}
    hosts = {"macos": "Darwin", "windows": "Windows", "linux": "Linux"}
    runs = [run for run in runs if run["id"] in catalog and
            run.get("target") == catalog[run["id"]]["target"] and
            run.get("functions") == catalog[run["id"]]["functions"] and
            (not catalog[run["id"]].get("platforms") or
             hosts.get(run["host"]) in catalog[run["id"]]["platforms"])]
    cards = []
    for run in runs:
        for shot in run["captures"]:
            title = f'{run["title"]} · {run["scheme"]} · {shot["tag"]}'
            esc = html.escape
            image = esc(shot["image"], quote=True)
            report = esc(shot["report"], quote=True)
            language, key = review_metadata(output, run, shot)
            cards.append(
                f'<article data-group="{esc(run["id"], quote=True)}" '
                f'data-language="{language}" data-dialog="{esc(key, quote=True)}" '
                f'data-width="{shot["size"][0]}" data-height="{shot["size"][1]}" '
                f'data-scheme="{run["scheme"]}" data-search="{esc((title + " " + shot["image"]).lower(), quote=True)}">'
                f'<a class="shot" href="{image}" data-name="{image}" data-title="{esc(title, quote=True)}" '
                f'data-report="{report}"><img loading="lazy" src="{image}" alt="{esc(title)}"></a>'
                f'<p>{esc(title)}</p><p class="filename">{image}</p><small>{language} · {esc(shot["qpa"])} · '
                f'{shot["size"][0]}×{shot["size"][1]} · '
                f'<a href="{report}">widget JSON</a></small></article>'
            )
    problems = [f'{r["id"]} / {r["scheme"]}: {r["error"]}' for r in runs if r.get("error")]
    status = "Все выбранные сценарии сняты." if not problems else "Есть незавершённые сценарии."
    document = """<!doctype html><html lang="ru"><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Sevenzip — ревью интерфейса</title><style>
:root {color-scheme:light dark;font-family:system-ui,sans-serif}
body {margin:2rem;line-height:1.5} header {max-width:75rem} h1 {margin-bottom:.3rem}
input[type=search] {font:inherit;padding:.6rem;width:min(36rem,90%);margin:1rem 0}
main {display:grid;grid-template-columns:repeat(auto-fill,minmax(320px,1fr));gap:1.5rem}
article {border:1px solid GrayText;border-radius:8px;padding:1rem;min-width:0}
img {width:100%;height:280px;object-fit:contain;object-position:top;background:Canvas}
p {overflow-wrap:anywhere;margin:.6rem 0}small {color:GrayText}[hidden]{display:none}
.filename {font-family:ui-monospace,monospace;font-size:.85rem;user-select:all}
button,select {font:inherit;padding:.45rem;cursor:pointer}
dialog {box-sizing:border-box;width:100vw;height:100dvh;max-width:none;max-height:none;
margin:0;border:0;padding:0;background:Canvas;color:CanvasText}
dialog[open] {display:flex;flex-direction:column} dialog::backdrop {background:#000b}
.toolbar {padding:.65rem 1rem;border-bottom:1px solid GrayText;display:flex;gap:.6rem;align-items:center;flex-wrap:wrap}
.identity {flex:1;min-width:200px}.identity p {margin:0}.identity .filename {font-size:1rem}
#stage {flex:1;min-height:0;overflow:auto;display:grid;place-items:center;padding:1rem;background:color-mix(in srgb,Canvas,GrayText 12%)}
#stage img {width:100%;height:100%;min-height:0;min-width:0;object-fit:contain;object-position:center}
#stage.actual {display:block}#stage.actual img {width:auto;height:auto;max-width:none;max-height:none;display:block;margin:auto}
body.viewing {overflow:hidden}#position {white-space:nowrap}
</style><header><h1>Sevenzip · ревью интерфейса</h1>
<p>Нажмите снимок или «Открыть первый» и листайте кнопками ← → на клавиатуре.
Имя и путь каждого снимка показаны для ваших пометок. Ссылка в адресной строке сохраняет выбранный снимок.
Нативные Qt-окна на локальных тестовых данных.
Системные настройки ОС не менялись. Снимки показывают клиентскую область окон;
нативные панели выбора ОС и заголовки окон сюда не входят.</p>
<p>STATUS</p><details><summary>Сведения о сборе</summary><pre>DETAILS</pre></details>
<select id="group" aria-label="Группа окон"><option value="">Все окна</option>GROUPS</select>
<select id="scheme" aria-label="Тема"><option value="">Обе темы</option><option>light</option><option>dark</option></select>
<select id="language" aria-label="Язык"><option value="">Все языки</option><option value="en">English</option><option value="ru">Русский</option><option value="unknown">Без языковой подписи</option></select>
<p><label><input type="checkbox" id="unique"> Уникальные окна</label>
<select id="size" aria-label="Размер для уникальных окон"><option value="normal">Обычный размер</option><option value="compact">Компактный размер</option></select>
<small>Главное окно, меню, страницы настроек и диалоги — без повторов состояний. Язык выбирается отдельным фильтром.</small></p>
<input type="search" placeholder="Поиск: окно, en, ru…" aria-label="Поиск снимков">
<p><button id="first">Открыть первый</button> <span id="count"></span></p></header><main>CARDS</main>
<dialog id="viewer" aria-label="Просмотр снимков"><div class="toolbar">
<button id="prev" aria-label="Предыдущий снимок">←</button><span id="position"></span>
<button id="next" aria-label="Следующий снимок">→</button>
<div class="identity"><p id="filename" class="filename"></p><p id="caption"></p></div>
<button id="copy">Копировать имя</button><button id="zoom" aria-pressed="false">Масштаб 100%</button>
<a id="original" target="_blank" rel="noopener">PNG</a><a id="report" target="_blank" rel="noopener">JSON</a>
<button id="close" aria-label="Закрыть просмотр">Закрыть · Esc</button></div>
<div id="stage"><img id="full" alt=""></div></dialog><script>
const input=document.querySelector('input[type=search]'), group=document.querySelector('#group'), scheme=document.querySelector('#scheme'), cards=[...document.querySelectorAll('article')];
const language=document.querySelector('#language'),unique=document.querySelector('#unique'),size=document.querySelector('#size');
const viewer=document.querySelector('#viewer'),stage=document.querySelector('#stage'),full=document.querySelector('#full');
const links=cards.map(c=>c.querySelector('.shot'));let current=null;
function visible(){return links.filter(a=>!a.closest('article').hidden);}
function filter(){const q=input.value.toLowerCase();for(const c of cards){c.hidden=!c.dataset.search.includes(q)||(group.value&&c.dataset.group!==group.value)||(scheme.value&&c.dataset.scheme!==scheme.value)||(language.value&&c.dataset.language!==language.value);}
if(unique.checked){const chosen=new Map();function score(c){const tag=c.querySelector('.shot').dataset.name;let quality=0;if(/initial|default|source-frame|main-shell-980|create-normal/.test(tag))quality+=10;if(/hidden|physical-control|clipped|large-font|archivePassword|archiveBrowse/.test(tag))quality-=20;if(c.dataset.group==='file-colors')quality-=10;return quality;}
for(const c of cards.filter(c=>!c.hidden)){const key=c.dataset.dialog,old=chosen.get(key);const area=+c.dataset.width*+c.dataset.height,oldArea=old?(+old.dataset.width*+old.dataset.height):0;if(!old||score(c)>score(old)||(score(c)===score(old)&&(size.value==='normal'?area>oldArea:area<oldArea)))chosen.set(key,c);}
const selected=new Set(chosen.values());for(const c of cards)if(!c.hidden)c.hidden=!selected.has(c);}
const n=visible().length;document.querySelector('#count').textContent=`${n} / ${cards.length} снимков`;document.querySelector('#first').disabled=!n;}
function show(a){if(!a)return;current=a;const list=visible(),i=list.indexOf(a);full.src=a.href;full.alt=a.dataset.title;
document.querySelector('#filename').textContent=a.dataset.name;document.querySelector('#caption').textContent=a.dataset.title;
document.querySelector('#position').textContent=`${i+1} / ${list.length}`;
document.querySelector('#prev').disabled=i<=0;document.querySelector('#next').disabled=i>=list.length-1;
document.querySelector('#original').href=a.href;document.querySelector('#report').href=a.dataset.report;
document.querySelector('#copy').textContent='Копировать имя';stage.scrollTop=stage.scrollLeft=0;
history.replaceState(null,'','#shot='+encodeURIComponent(a.dataset.name));
if(!viewer.open){viewer.showModal();document.body.classList.add('viewing');}}
function move(step){const list=visible();show(list[list.indexOf(current)+step]);}
function fromHash(){if(!location.hash.startsWith('#shot='))return;let name;try{name=decodeURIComponent(location.hash.slice(6));}catch{return;}
const a=links.find(a=>a.dataset.name===name);if(a){if(a.closest('article').hidden){input.value=group.value=scheme.value=language.value='';unique.checked=false;filter();}show(a);}}
links.forEach(a=>a.addEventListener('click',e=>{if(e.ctrlKey||e.metaKey||e.shiftKey||e.altKey)return;e.preventDefault();show(a);}));
document.querySelector('#first').onclick=()=>show(visible()[0]);document.querySelector('#prev').onclick=()=>move(-1);document.querySelector('#next').onclick=()=>move(1);
document.querySelector('#close').onclick=()=>viewer.close();viewer.addEventListener('close',()=>{if(viewer.open)return;document.body.classList.remove('viewing');history.replaceState(null,'',location.pathname+location.search);current?.focus();});
document.querySelector('#zoom').onclick=e=>{const actual=stage.classList.toggle('actual');e.currentTarget.setAttribute('aria-pressed',actual);e.currentTarget.textContent=actual?'Вписать в окно':'Масштаб 100%';};
document.querySelector('#copy').onclick=async e=>{const name=current.dataset.name;try{await navigator.clipboard.writeText(name);e.target.textContent='Скопировано';}catch{const selection=getSelection(),range=document.createRange();range.selectNodeContents(document.querySelector('#filename'));selection.removeAllRanges();selection.addRange(range);e.target.textContent='Нажмите Ctrl+C / ⌘C';}};
viewer.addEventListener('keydown',e=>{if(e.ctrlKey||e.metaKey||e.altKey)return;if(e.key==='ArrowLeft'){e.preventDefault();move(-1);}else if(e.key==='ArrowRight'){e.preventDefault();move(1);}else if(e.key==='Home'){e.preventDefault();show(visible()[0]);}else if(e.key==='End'){e.preventDefault();show(visible().at(-1));}});
input.addEventListener('input',filter);group.addEventListener('change',filter);scheme.addEventListener('change',()=>{if(unique.checked&&!scheme.value)scheme.value='light';filter();});language.addEventListener('change',filter);size.addEventListener('change',filter);unique.addEventListener('change',()=>{if(unique.checked)scheme.value ||= 'light';filter();});window.addEventListener('hashchange',fromHash);filter();fromHash();</script></html>"""
    document = document.replace("STATUS", html.escape(status))
    document = document.replace("DETAILS", html.escape("\n".join(problems) or "См. manifest.json и журналы сценариев."))
    document = document.replace("CARDS", "\n".join(cards))
    groups = dict.fromkeys((run["id"], run["title"]) for run in runs)
    document = document.replace("GROUPS", "".join(
        f'<option value="{html.escape(key, quote=True)}">{html.escape(title)}</option>'
        for key, title in groups))
    (output / "index.html").write_text(document, encoding="utf-8")


def collect(directory, output, scheme, qpa):
    captures = []
    errors = []
    for path in sorted(directory.glob("*.json")):
        image = path.with_suffix(".png")
        if not image.is_file():
            continue
        data = json.loads(path.read_text(encoding="utf-8"))
        metadata = data["capture"]
        if metadata.get("colorScheme") != scheme:
            errors.append(f'{path.name}: requested {scheme}, got {metadata.get("colorScheme")}')
        if metadata["qpa"] != qpa:
            errors.append(f'{path.name}: requested native {qpa}, got {metadata["qpa"]}')
        captures.append({"tag": path.stem, "image": image.relative_to(output).as_posix(),
                         "report": path.relative_to(output).as_posix(),
                         "size": data["bounds"][2:], "qpa": metadata["qpa"],
                         "effectiveScheme": metadata.get("colorScheme"),
                         "palette": metadata.get("palette"), "window": data.get("name"),
                         "class": data["class"]})
    if not captures:
        errors.append("No screenshots produced")
    return captures, errors


def main():
    catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--only", nargs="+", choices=[row["id"] for row in catalog])
    parser.add_argument("--schemes", nargs="+", choices=["light", "dark"], default=["light", "dark"])
    parser.add_argument("--output", type=Path)
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--gallery-only", action="store_true", help="Rebuild gallery from existing manifest without capturing windows")
    args = parser.parse_args()
    if not args.list:
        os.environ.update(workspace_environment(ROOT))
    if args.gallery_only:
        output = checked_output(ROOT, args.output or workspace_directory(ROOT, "artifacts") / "dev/ui-theme-review")
        gallery(output, json.loads((output / "manifest.json").read_text(encoding="utf-8"))["runs"])
        print(f'Review: {output / "index.html"}')
        return 0
    selected = [row for row in catalog if not args.only or row["id"] in args.only]
    if args.list:
        for row in selected:
            print(f'{row["id"]}: {row["title"]} ({row["target"]})')
        return 0
    system = platform.system()
    qpa = {"Darwin": "cocoa", "Windows": "windows", "Linux": "xcb"}[system]
    selected = [row for row in selected if system in row.get("platforms", [system])]
    if not selected:
        parser.error(f"No selected capture scenarios support {system}")
    output = checked_output(ROOT, args.output or workspace_directory(ROOT, "artifacts") / "dev/ui-theme-review")
    output.mkdir(parents=True, exist_ok=True)
    targets = list(dict.fromkeys(row["target"] for row in selected))
    subprocess.run([sys.executable, "scripts/prepare-runtime.py"], cwd=ROOT, check=True)
    subprocess.run(["bash", "scripts/build.sh", "dev", *targets], cwd=ROOT, check=True)
    previous = output / "manifest.json"
    runs = json.loads(previous.read_text(encoding="utf-8")).get("runs", []) if previous.exists() else []
    snapshot = runner_context(ROOT)
    if snapshot:
        revision, source_state = snapshot["revision"], snapshot["sourceState"]
    else:
        revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        source_diff = subprocess.check_output(["git", "diff", "HEAD", "--", "src", "CMakeLists.txt", "cmake"], cwd=ROOT)
        source_state = {"modified": bool(source_diff), "diffSha256": hashlib.sha256(source_diff).hexdigest()}
    host = {"Darwin": "macos", "Windows": "windows", "Linux": "linux"}[system]
    failures = 0
    for row in selected:
        for scheme in args.schemes:
            directory = output / host / scheme / row["id"]
            directory.mkdir(parents=True, exist_ok=True)
            # Capture separately: a blocked desktop or failed producer must
            # leave the last successful review and its provenance usable.
            old = [run for run in runs if (run["id"], run["scheme"], run["host"]) ==
                   (row["id"], scheme, host)]
            pending = Path(tempfile.mkdtemp(prefix=f'.{row["id"]}-{scheme}-', dir=output))
            settings = pending / "settings.ini"
            env = os.environ | {"QT_QPA_PLATFORM": qpa, "SEVENZIP_REVIEW_COLOR_SCHEME": scheme,
                                "SEVENZIP_CAPTURE_SCREENSHOTS": "1",
                                "SEVENZIP_UI_REPORT_DIR": str(pending),
                                "SEVENZIP_SETTINGS_PATH": str(settings)}
            runtime = os.environ.get("SEVENZIP_CODEC_MODULE_DIR", str(codec_runtime(ROOT)))
            env["PATH"] = runtime + os.pathsep + env.get("PATH", "")
            executable = workspace_directory(ROOT, "artifacts") / "dev" / row["target"]
            if system == "Windows":
                executable = executable.with_suffix(".exe")
            print(f'Capture {host}/{scheme}/{row["id"]}', flush=True)
            error = []
            with (pending / "run.log").open("w", encoding="utf-8") as log:
                try:
                    result = subprocess.run([str(executable), *row["functions"], "-silent"],
                                            cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                            timeout=180)
                    if result.returncode:
                        error.append(f'Producer exited {result.returncode}; see run.log')
                except subprocess.TimeoutExpired:
                    error.append("Producer timed out; see run.log")
            captures, issues = collect(pending, output, scheme, qpa)
            error.extend(issues)
            settings.unlink(missing_ok=True)
            if error:
                failures += 1
                print("; ".join(error), flush=True)
                shutil.copy2(pending / "run.log", directory / "last-failed-run.log")
                shutil.rmtree(pending)
                if old:
                    continue
                captures = []
            else:
                for run in old:
                    for shot in run["captures"]:
                        for key in ("image", "report"):
                            artifact = (output / shot[key]).resolve()
                            if artifact.parent == directory:
                                artifact.unlink(missing_ok=True)
                for artifact in pending.iterdir():
                    artifact.replace(directory / artifact.name)
                for shot in captures:
                    for key in ("image", "report"):
                        shot[key] = (directory / Path(shot[key]).name).relative_to(output).as_posix()
                pending.rmdir()
            run = {**row, "scheme": scheme, "host": host, "revision": revision, "sourceState": source_state,
                   "capturedAt": datetime.now(timezone.utc).isoformat(), "captures": captures}
            if error:
                run["error"] = "; ".join(error)
            runs = [r for r in runs if r not in old] + [run]
            previous.write_text(json.dumps({"runs": runs}, ensure_ascii=False, indent=2), encoding="utf-8")
            gallery(output, runs)
    print(f'Review: {output / "index.html"}; failed scenarios: {failures}', flush=True)
    return int(bool(failures))


if __name__ == "__main__":
    sys.exit(main())
