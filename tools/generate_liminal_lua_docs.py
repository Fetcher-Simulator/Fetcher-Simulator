from __future__ import annotations

import html
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DOC_DIR = ROOT / "apps" / "openmw-server" / "scripts"

PAGES = [
    {
        "source": DOC_DIR / "README.md",
        "output": DOC_DIR / "README.html",
        "eyebrow": "Dedicated Server · Lua Runtime",
        "subtitle": "Authoritative scripting beneath the surface",
        "other_href": "CLIENT_LUA_MULTIPLAYER.html",
        "other_label": "Enter the client-side chamber",
        "page_kind": "server",
    },
    {
        "source": DOC_DIR / "CLIENT_LUA_MULTIPLAYER.md",
        "output": DOC_DIR / "CLIENT_LUA_MULTIPLAYER.html",
        "eyebrow": "Client Runtime · Multiplayer Compatibility",
        "subtitle": "Where local scripts meet an authoritative world",
        "other_href": "README.html",
        "other_label": "Return to server Lua",
        "page_kind": "client",
    },
]


def slugify(text: str) -> str:
    text = re.sub(r"`([^`]*)`", r"\1", text)
    text = re.sub(r"\[([^]]+)\]\([^)]*\)", r"\1", text)
    text = re.sub(r"<[^>]+>", "", text)
    text = re.sub(r"[^a-zA-Z0-9\s-]", "", text).strip().lower()
    return re.sub(r"[-\s]+", "-", text) or "section"


def inline_markup(text: str) -> str:
    placeholders: list[str] = []

    def protect_code(match: re.Match[str]) -> str:
        placeholders.append(f"<code>{html.escape(match.group(1), quote=False)}</code>")
        return f"\x00CODE{len(placeholders)-1}\x00"

    text = re.sub(r"`([^`]+)`", protect_code, text)
    text = html.escape(text, quote=False)
    text = re.sub(
        r"\[([^]]+)\]\(([^)]+)\)",
        lambda m: '<a href="{}">{}</a>'.format(
            html.escape(re.sub(r"\.md(?=($|#))", ".html", m.group(2)), quote=True),
            m.group(1),
        ),
        text,
    )
    text = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", text)
    text = re.sub(r"(?<!\*)\*([^*]+)\*(?!\*)", r"<em>\1</em>", text)
    text = re.sub(r"~~([^~]+)~~", r"<del>\1</del>", text)

    for index, value in enumerate(placeholders):
        text = text.replace(f"\x00CODE{index}\x00", value)
    return text


def is_table_separator(line: str) -> bool:
    cells = [c.strip() for c in line.strip().strip("|").split("|")]
    return bool(cells) and all(re.fullmatch(r":?-{3,}:?", cell or "") for cell in cells)


def split_table_row(line: str) -> list[str]:
    return [cell.strip() for cell in line.strip().strip("|").split("|")]


def markdown_to_html(markdown: str) -> tuple[str, list[tuple[int, str, str]], str]:
    lines = markdown.replace("\r\n", "\n").replace("\r", "\n").split("\n")
    out: list[str] = []
    headings: list[tuple[int, str, str]] = []
    title = "Documentation"
    used_slugs: dict[str, int] = {}
    i = 0

    def unique_slug(label: str) -> str:
        base = slugify(label)
        count = used_slugs.get(base, 0)
        used_slugs[base] = count + 1
        return base if count == 0 else f"{base}-{count + 1}"

    while i < len(lines):
        line = lines[i]
        stripped = line.strip()

        if not stripped:
            i += 1
            continue

        if stripped.startswith("```"):
            language = stripped[3:].strip()
            code_lines: list[str] = []
            i += 1
            while i < len(lines) and not lines[i].strip().startswith("```"):
                code_lines.append(lines[i])
                i += 1
            i += 1
            class_attr = f' class="language-{html.escape(language, quote=True)}"' if language else ""
            out.append(
                '<div class="code-shell"><div class="code-runes"><span></span><span></span><span></span></div>'
                f'<pre><code{class_attr}>{html.escape(chr(10).join(code_lines))}</code></pre></div>'
            )
            continue

        heading = re.match(r"^(#{1,6})\s+(.+?)\s*$", line)
        if heading:
            level = len(heading.group(1))
            label = heading.group(2)
            anchor = unique_slug(label)
            if level == 1:
                title = re.sub(r"[`*_]", "", label)
            else:
                headings.append((level, re.sub(r"[`*_]", "", label), anchor))
            out.append(
                f'<h{level} id="{anchor}"><a class="heading-anchor" href="#{anchor}" aria-label="Link to this section">'
                f'{inline_markup(label)}</a></h{level}>'
            )
            i += 1
            continue

        if i + 1 < len(lines) and "|" in line and is_table_separator(lines[i + 1]):
            headers = split_table_row(line)
            i += 2
            rows: list[list[str]] = []
            while i < len(lines) and "|" in lines[i] and lines[i].strip():
                rows.append(split_table_row(lines[i]))
                i += 1
            out.append('<div class="table-wrap"><table><thead><tr>')
            out.extend(f"<th>{inline_markup(cell)}</th>" for cell in headers)
            out.append("</tr></thead><tbody>")
            for row in rows:
                padded = row + [""] * (len(headers) - len(row))
                out.append("<tr>" + "".join(f"<td>{inline_markup(cell)}</td>" for cell in padded[: len(headers)]) + "</tr>")
            out.append("</tbody></table></div>")
            continue

        if stripped.startswith(">"):
            quote_lines: list[str] = []
            while i < len(lines) and lines[i].lstrip().startswith(">"):
                quote_lines.append(lines[i].lstrip()[1:].lstrip())
                i += 1
            out.append(f'<blockquote><span class="quote-mark">◈</span><p>{inline_markup(" ".join(quote_lines))}</p></blockquote>')
            continue

        unordered = re.match(r"^\s*[-*+]\s+(.+)$", line)
        ordered = re.match(r"^\s*\d+[.)]\s+(.+)$", line)
        if unordered or ordered:
            tag = "ul" if unordered else "ol"
            items: list[str] = []
            pattern = r"^\s*[-*+]\s+(.+)$" if unordered else r"^\s*\d+[.)]\s+(.+)$"
            while i < len(lines):
                match = re.match(pattern, lines[i])
                if not match:
                    break
                items.append(match.group(1))
                i += 1
            out.append(f"<{tag}>" + "".join(f"<li>{inline_markup(item)}</li>" for item in items) + f"</{tag}>")
            continue

        if re.fullmatch(r"\s*([-*_])(?:\s*\1){2,}\s*", line):
            out.append('<hr aria-hidden="true">')
            i += 1
            continue

        paragraph_lines = [stripped]
        i += 1
        while i < len(lines):
            candidate = lines[i]
            cstrip = candidate.strip()
            if not cstrip:
                break
            if (
                cstrip.startswith("```")
                or re.match(r"^#{1,6}\s+", candidate)
                or cstrip.startswith(">")
                or re.match(r"^\s*[-*+]\s+", candidate)
                or re.match(r"^\s*\d+[.)]\s+", candidate)
                or (i + 1 < len(lines) and "|" in candidate and is_table_separator(lines[i + 1]))
            ):
                break
            paragraph_lines.append(cstrip)
            i += 1
        out.append(f"<p>{inline_markup(' '.join(paragraph_lines))}</p>")

    return "\n".join(out), headings, title


def render_toc(headings: list[tuple[int, str, str]]) -> str:
    items = []
    for level, label, anchor in headings:
        if level > 3:
            continue
        items.append(
            f'<li class="toc-level-{level}"><a href="#{anchor}"><span>{html.escape(label)}</span><i aria-hidden="true"></i></a></li>'
        )
    return "\n".join(items)


CSS = r"""
:root {
  color-scheme: dark;
  --ink: #d8eee8;
  --muted: #89aaa7;
  --deep: #03090b;
  --abyss: #071113;
  --stone: rgba(15, 29, 30, .78);
  --stone-2: rgba(8, 18, 20, .9);
  --line: rgba(147, 218, 207, .18);
  --glow: #8de7d6;
  --glow-2: #7ca9ff;
  --gold: #cabd82;
  --shadow: rgba(0, 0, 0, .58);
  --content: 860px;
  --rail: 260px;
}
* { box-sizing: border-box; }
html { scroll-behavior: smooth; background: var(--deep); }
body {
  margin: 0;
  min-height: 100vh;
  color: var(--ink);
  background:
    radial-gradient(ellipse at 50% -15%, rgba(101, 179, 174, .22), transparent 42%),
    radial-gradient(ellipse at 50% 105%, rgba(59, 136, 151, .24), transparent 38%),
    linear-gradient(180deg, #061012 0%, #030708 50%, #061113 100%);
  font-family: Georgia, "Times New Roman", serif;
  line-height: 1.72;
  overflow-x: hidden;
}
body::before {
  content: "";
  position: fixed;
  inset: 0;
  z-index: -5;
  background:
    radial-gradient(ellipse at 9% 10%, #000 0 17%, transparent 18%),
    radial-gradient(ellipse at 91% 9%, #000 0 18%, transparent 19%),
    radial-gradient(ellipse at 50% -7%, transparent 0 32%, rgba(0,0,0,.96) 49%),
    linear-gradient(90deg, rgba(0,0,0,.85), transparent 19% 81%, rgba(0,0,0,.85));
  pointer-events: none;
}
.ambient, .caustics, .mist, .pool-line { position: fixed; pointer-events: none; }
.ambient {
  inset: -20%; z-index: -4; opacity: .32; filter: blur(36px);
  background: conic-gradient(from 20deg at 50% 50%, transparent, rgba(82, 220, 202, .16), transparent 28%, rgba(77, 119, 204, .13), transparent 55%, rgba(82, 220, 202, .12), transparent);
  animation: drift 24s ease-in-out infinite alternate;
}
.caustics {
  left: -10vw; right: -10vw; bottom: -8vh; height: 48vh; z-index: -3;
  opacity: .24; filter: blur(2px) saturate(1.4);
  background:
    repeating-radial-gradient(ellipse at 30% 70%, transparent 0 15px, rgba(146, 255, 233, .19) 18px 20px, transparent 24px 48px),
    repeating-radial-gradient(ellipse at 70% 40%, transparent 0 20px, rgba(112, 174, 255, .14) 23px 25px, transparent 30px 58px);
  transform: perspective(500px) rotateX(63deg) scale(1.35);
  transform-origin: bottom;
  animation: caustic-shift 13s linear infinite alternate;
}
.mist {
  inset: auto -10vw 0; height: 33vh; z-index: -2;
  background: linear-gradient(180deg, transparent, rgba(93, 174, 170, .06) 45%, rgba(59, 117, 128, .17));
  filter: blur(15px);
}
.pool-line {
  left: 0; right: 0; bottom: 19vh; z-index: -1; height: 1px;
  background: linear-gradient(90deg, transparent, rgba(151, 245, 226, .55), transparent);
  box-shadow: 0 0 30px 7px rgba(102, 221, 205, .11);
}
@keyframes drift { to { transform: translate3d(3%, -2%, 0) rotate(8deg) scale(1.04); } }
@keyframes caustic-shift { to { background-position: 80px -40px, -70px 45px; transform: perspective(500px) rotateX(63deg) scale(1.42) translateX(2%); } }

.skip-link { position: fixed; left: 1rem; top: -5rem; z-index: 50; padding: .7rem 1rem; background: #d6f8ef; color: #041011; border-radius: .3rem; }
.skip-link:focus { top: 1rem; }
.site-header {
  min-height: 62vh; display: grid; place-items: center; text-align: center; padding: 7rem 1.5rem 5rem;
  position: relative;
}
.site-header::before {
  content: ""; position: absolute; inset: 0; opacity: .6;
  background:
    radial-gradient(circle at 50% 42%, rgba(151, 236, 219, .18) 0 1px, transparent 2px),
    radial-gradient(ellipse at center, transparent 0 32%, rgba(0,0,0,.32) 70%);
  background-size: 39px 39px, 100% 100%;
  mask-image: linear-gradient(180deg, black, transparent 92%);
}
.hero { position: relative; max-width: 950px; }
.eyebrow { margin: 0 0 1.1rem; color: var(--gold); text-transform: uppercase; letter-spacing: .27em; font: 600 .72rem/1.4 system-ui, sans-serif; }
h1 {
  margin: 0; font-weight: 400; font-size: clamp(3rem, 8vw, 6.8rem); line-height: .95; letter-spacing: -.04em;
  color: #e5f7f2; text-shadow: 0 0 36px rgba(133, 233, 213, .19), 0 3px 0 #020607;
}
.subtitle { margin: 1.4rem auto 0; color: #a7c5c1; font-style: italic; font-size: clamp(1.05rem, 2vw, 1.4rem); }
.portal-nav { display: flex; flex-wrap: wrap; justify-content: center; gap: .85rem; margin-top: 2.3rem; }
.portal-link, .source-link {
  display: inline-flex; align-items: center; gap: .65rem; min-height: 44px; padding: .72rem 1rem;
  color: #dff7f1; text-decoration: none; border: 1px solid rgba(142, 226, 210, .25); border-radius: 999px;
  background: rgba(6, 18, 20, .56); backdrop-filter: blur(9px); box-shadow: inset 0 0 24px rgba(128, 232, 211, .04), 0 12px 32px rgba(0,0,0,.25);
  font: 600 .77rem/1 system-ui, sans-serif; letter-spacing: .06em;
  transition: border-color .25s, background .25s, transform .25s, box-shadow .25s;
}
.portal-link::before { content: "◇"; color: var(--glow); font-size: 1.1rem; }
.source-link::before { content: "⌁"; color: var(--gold); font-size: 1.1rem; }
.portal-link:hover, .source-link:hover { transform: translateY(-2px); border-color: rgba(142, 226, 210, .58); background: rgba(11, 31, 33, .8); box-shadow: 0 0 28px rgba(105, 226, 207, .08); }
.rune-divider { margin: 3.2rem auto 0; color: rgba(202, 189, 130, .55); letter-spacing: 1.2em; font-size: .8rem; }

.layout { width: min(calc(100% - 2rem), 1220px); margin: -3.5rem auto 10rem; display: grid; grid-template-columns: var(--rail) minmax(0, var(--content)); gap: 2.4rem; align-items: start; justify-content: center; position: relative; }
.toc {
  position: sticky; top: 1.2rem; max-height: calc(100vh - 2.4rem); overflow: auto; padding: 1.1rem 1rem 1.3rem;
  border: 1px solid var(--line); border-radius: 1.2rem 1.2rem 1.2rem .35rem; background: linear-gradient(145deg, rgba(14, 30, 31, .7), rgba(4, 11, 13, .86));
  backdrop-filter: blur(14px); box-shadow: inset 0 1px rgba(255,255,255,.03), 0 30px 60px rgba(0,0,0,.2);
  scrollbar-width: thin; scrollbar-color: rgba(126, 211, 197, .3) transparent;
}
.toc h2 { margin: 0 0 .75rem; padding: 0; border: 0; color: var(--gold); font: 700 .7rem/1.2 system-ui, sans-serif; text-transform: uppercase; letter-spacing: .2em; }
.toc ul { list-style: none; margin: 0; padding: 0; }
.toc li { margin: 0; }
.toc a { display: flex; align-items: center; justify-content: space-between; gap: .4rem; padding: .43rem .2rem; color: #89aaa7; text-decoration: none; font: 500 .75rem/1.35 system-ui, sans-serif; border-bottom: 1px solid rgba(145, 220, 207, .05); }
.toc a:hover { color: #dcf8f1; }
.toc a i { width: 4px; height: 4px; border-radius: 50%; background: rgba(138, 230, 213, .3); flex: 0 0 auto; }
.toc-level-3 a { padding-left: .75rem; font-size: .7rem; opacity: .84; }

.document {
  min-width: 0; padding: clamp(1.25rem, 4vw, 3.8rem); border: 1px solid var(--line); border-radius: 2.2rem 2.2rem 2.2rem .6rem;
  background:
    linear-gradient(180deg, rgba(255,255,255,.025), transparent 10%),
    radial-gradient(circle at 82% 2%, rgba(99, 214, 196, .07), transparent 19%),
    linear-gradient(145deg, rgba(12, 27, 29, .88), rgba(4, 11, 13, .95));
  backdrop-filter: blur(17px); box-shadow: inset 0 1px rgba(255,255,255,.04), 0 45px 100px rgba(0,0,0,.34), 0 0 0 8px rgba(2,8,9,.13);
}
.document > h1:first-child { display: none; }
.document h2, .document h3, .document h4 { scroll-margin-top: 1.5rem; }
.document h2 { margin: 3.5rem 0 1.1rem; padding-top: 1.6rem; border-top: 1px solid rgba(147, 218, 207, .11); color: #dff6ef; font-size: clamp(1.65rem, 3vw, 2.35rem); line-height: 1.18; font-weight: 400; letter-spacing: -.025em; }
.document h2::before { content: "◈"; display: block; margin-bottom: .55rem; color: rgba(202, 189, 130, .65); font-size: .72rem; letter-spacing: .2em; }
.document h3 { margin: 2.4rem 0 .8rem; color: #b8ddd5; font-size: 1.32rem; font-weight: 500; }
.document h4 { margin: 1.8rem 0 .55rem; color: #b7cbc8; font-size: 1.06rem; font-family: system-ui, sans-serif; letter-spacing: .02em; }
.heading-anchor { color: inherit; text-decoration: none; }
.heading-anchor:hover { text-shadow: 0 0 18px rgba(128, 235, 213, .2); }
p { margin: .8rem 0 1.15rem; }
a { color: #99eadb; text-decoration-color: rgba(153, 234, 219, .35); text-underline-offset: .18em; }
a:hover { color: #d9fff8; text-decoration-color: currentColor; }
strong { color: #effbf8; font-weight: 700; }
em { color: #bdd7d2; }
code { padding: .12rem .36rem; border: 1px solid rgba(125, 207, 195, .14); border-radius: .28rem; background: rgba(0, 7, 8, .52); color: #aee8dc; font: .88em/1.45 Consolas, "Cascadia Mono", monospace; }
ul, ol { padding-left: 1.35rem; margin: .7rem 0 1.25rem; }
li { padding-left: .28rem; margin: .35rem 0; }
li::marker { color: rgba(137, 229, 211, .62); }
blockquote { position: relative; margin: 1.7rem 0; padding: 1rem 1.2rem 1rem 3.1rem; border: 1px solid rgba(202, 189, 130, .16); border-left: 2px solid rgba(202, 189, 130, .5); border-radius: .35rem 1rem 1rem .35rem; background: rgba(34, 31, 21, .15); color: #c6d9d4; }
blockquote p { margin: 0; }
.quote-mark { position: absolute; left: 1.15rem; top: 1rem; color: rgba(202, 189, 130, .7); }
hr { border: 0; height: 1px; margin: 3rem 0; background: linear-gradient(90deg, transparent, rgba(143, 220, 206, .25), transparent); }
.code-shell { margin: 1.35rem 0 1.7rem; overflow: hidden; border: 1px solid rgba(122, 206, 193, .15); border-radius: .8rem; background: #020809; box-shadow: inset 0 0 50px rgba(45, 118, 111, .05), 0 20px 40px rgba(0,0,0,.2); }
.code-runes { height: 30px; display: flex; align-items: center; gap: 7px; padding: 0 12px; border-bottom: 1px solid rgba(130, 211, 198, .08); background: rgba(255,255,255,.018); }
.code-runes span { width: 6px; height: 6px; border: 1px solid rgba(159, 226, 213, .25); transform: rotate(45deg); }
pre { margin: 0; padding: 1rem 1.15rem 1.2rem; overflow: auto; line-height: 1.58; tab-size: 4; }
pre code { padding: 0; border: 0; background: transparent; color: #b9d9d3; font-size: .83rem; }
.table-wrap { margin: 1.4rem 0 2rem; overflow-x: auto; border: 1px solid rgba(140, 218, 204, .15); border-radius: .8rem; box-shadow: 0 18px 35px rgba(0,0,0,.16); }
table { width: 100%; border-collapse: collapse; min-width: 620px; background: rgba(2, 9, 10, .4); font-size: .88rem; }
th, td { padding: .8rem .9rem; text-align: left; vertical-align: top; border-bottom: 1px solid rgba(140, 218, 204, .09); border-right: 1px solid rgba(140, 218, 204, .06); }
th { color: #d9f4ed; background: rgba(54, 105, 99, .14); font: 700 .73rem/1.35 system-ui, sans-serif; text-transform: uppercase; letter-spacing: .06em; }
tr:last-child td { border-bottom: 0; }
tr:hover td { background: rgba(104, 201, 184, .035); }

.site-footer { width: min(calc(100% - 2rem), 900px); margin: -4rem auto 0; padding: 3rem 1rem 7rem; text-align: center; color: #668783; font: .75rem/1.6 system-ui, sans-serif; letter-spacing: .04em; }
.site-footer .sigil { display: block; color: rgba(202, 189, 130, .5); font-size: 1.2rem; margin-bottom: .8rem; }

@media (max-width: 980px) {
  .layout { grid-template-columns: minmax(0, 860px); margin-top: -2.5rem; }
  .toc { position: relative; top: auto; max-height: none; }
  .toc ul { columns: 2; column-gap: 1.5rem; }
  .toc li { break-inside: avoid; }
}
@media (max-width: 620px) {
  .site-header { min-height: 56vh; padding-top: 5rem; }
  .layout { width: min(calc(100% - 1rem), 860px); gap: 1rem; }
  .document { padding: 1.25rem; border-radius: 1.25rem 1.25rem 1.25rem .35rem; }
  .toc ul { columns: 1; }
  .portal-nav { flex-direction: column; align-items: center; }
  .portal-link, .source-link { width: min(100%, 330px); justify-content: center; }
  h1 { font-size: clamp(2.6rem, 15vw, 4.3rem); }
  .rune-divider { letter-spacing: .65em; }
}
@media (prefers-reduced-motion: reduce) {
  *, *::before, *::after { scroll-behavior: auto !important; animation-duration: .001ms !important; animation-iteration-count: 1 !important; }
}
@media print {
  :root { color-scheme: light; }
  body { color: #172321; background: white; }
  body::before, .ambient, .caustics, .mist, .pool-line, .site-header::before, .toc, .portal-nav, .rune-divider { display: none !important; }
  .site-header { min-height: 0; padding: 1cm 0 .5cm; color: #111; }
  h1 { color: #111; text-shadow: none; font-size: 32pt; }
  .subtitle, .eyebrow { color: #444; }
  .layout { display: block; width: auto; margin: 0; }
  .document { color: #172321; background: white; border: 0; box-shadow: none; padding: 0; }
  .document h2, .document h3, .document h4, strong, a, code { color: #172321; }
  .code-shell, blockquote, .table-wrap { break-inside: avoid; }
}
"""


def build_page(page: dict[str, object]) -> None:
    source = Path(page["source"])
    output = Path(page["output"])
    markdown = source.read_text(encoding="utf-8")
    body, headings, title = markdown_to_html(markdown)
    toc = render_toc(headings)
    source_href = source.name
    page_kind = str(page["page_kind"])

    document = f"""<!doctype html>
<html lang="en" data-page="{html.escape(page_kind, quote=True)}">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta name="theme-color" content="#061012">
  <meta name="description" content="{html.escape(str(page['subtitle']), quote=True)}">
  <title>{html.escape(title)} · OpenMW Lua</title>
  <style>{CSS}</style>
</head>
<body>
  <a class="skip-link" href="#document">Skip to documentation</a>
  <div class="ambient" aria-hidden="true"></div>
  <div class="caustics" aria-hidden="true"></div>
  <div class="mist" aria-hidden="true"></div>
  <div class="pool-line" aria-hidden="true"></div>

  <header class="site-header">
    <div class="hero">
      <p class="eyebrow">{html.escape(str(page['eyebrow']))}</p>
      <h1>{html.escape(title)}</h1>
      <p class="subtitle">{html.escape(str(page['subtitle']))}</p>
      <nav class="portal-nav" aria-label="Related documentation">
        <a class="portal-link" href="{html.escape(str(page['other_href']), quote=True)}">{html.escape(str(page['other_label']))}</a>
        <a class="source-link" href="{html.escape(source_href, quote=True)}">Read the Markdown source</a>
      </nav>
      <div class="rune-divider" aria-hidden="true">◇ ◈ ◇</div>
    </div>
  </header>

  <main class="layout">
    <nav class="toc" aria-label="On this page">
      <h2>Carved passages</h2>
      <ul>{toc}</ul>
    </nav>
    <article class="document" id="document">{body}</article>
  </main>

  <footer class="site-footer">
    <span class="sigil" aria-hidden="true">◈</span>
    A self-contained offline document. No external assets are required.
  </footer>
</body>
</html>
"""
    output.write_text(document, encoding="utf-8", newline="\n")
    print(f"wrote {output.relative_to(ROOT)} ({output.stat().st_size} bytes)")


if __name__ == "__main__":
    for page in PAGES:
        build_page(page)
