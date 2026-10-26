#!/usr/bin/env python3
"""Build read-only voice-caption fallbacks; never modifies the source workbook."""
from __future__ import annotations
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import struct
import xml.etree.ElementTree as ET
import zipfile

SOURCE = 'https://docs.google.com/spreadsheets/d/1x757dhvjUUlROeapDgXx7aLiKTNo977mjG9m9wECuo0/edit'
NS = '{http://schemas.openxmlformats.org/spreadsheetml/2006/main}'

def clean(text: str) -> str:
    return ' '.join(str(text).replace('\x00', '').split())

def audio_path(value: str) -> str | None:
    value = value.strip().replace('\\', '/').lower()
    # Some source cells have a comma after the extension. Accept only trailing
    # separators, rather than guessing filenames or matching by basename.
    value = value.rstrip(' ,;')
    if not value or len(value) > 512 or ':' in value or any(ord(c) < 32 for c in value): return None
    if any(p in ('', '.', '..') for p in value.split('/')): return None
    if not value.endswith(('.mp3', '.wav', '.ogg')): return None
    return value if value.startswith('sound/') else 'sound/' + value

def col_index(ref: str) -> int:
    n = 0
    for ch in re.match(r'[A-Z]+', ref)[0]: n = n * 26 + ord(ch) - 64
    return n

def read_workbook(path: Path):
    with zipfile.ZipFile(path) as archive:
        if sum(i.file_size for i in archive.infolist()) > 50 * 1024 * 1024:
            raise ValueError('Workbook is too large')
        strings = []
        if 'xl/sharedStrings.xml' in archive.namelist():
            strings = [''.join(n.text or '' for n in e.iter(NS + 't'))
                       for e in ET.fromstring(archive.read('xl/sharedStrings.xml')).findall(NS + 'si')]
        rels = {e.attrib['Id']: e.attrib['Target'] for e in ET.fromstring(archive.read('xl/_rels/workbook.xml.rels'))}
        for sheet in ET.fromstring(archive.read('xl/workbook.xml')).findall(NS + 'sheets/' + NS + 'sheet'):
            target = rels[sheet.attrib['{http://schemas.openxmlformats.org/officeDocument/2006/relationships}id']]
            target = target.lstrip('/') if target.startswith('/') else 'xl/' + target
            if '..' in PurePosixPath(target).parts or not target.startswith('xl/'):
                raise ValueError('Unsafe workbook worksheet path')
            rows = []
            for row in ET.fromstring(archive.read(target)).findall(NS + 'sheetData/' + NS + 'row'):
                values = {}
                for cell in row.findall(NS + 'c'):
                    raw = cell.find(NS + 'v')
                    value = raw.text or '' if raw is not None else ''.join(n.text or '' for n in cell.iter(NS + 't'))
                    if cell.get('t') == 's': value = strings[int(value)]
                    values[col_index(cell.attrib['r'])] = value
                rows.append((int(row.attrib['r']), values))
            yield sheet.attrib['name'], rows

def import_rows(sheets):
    captions = defaultdict(Counter)
    locations = defaultdict(list)
    repairs, rejected, names = [], [], []
    for name, rows in sheets:
        names.append(name); columns = []
        for number, cells in rows:
            headers = [c for c, value in cells.items() if value.strip().lower() == 'file'
                       and cells.get(c + 1, '').strip().lower() == 'text']
            if headers: columns = headers; continue
            for c in columns:
                raw, text = cells.get(c, '').strip(), clean(cells.get(c + 1, ''))
                if not raw: continue
                path = audio_path(raw)
                if not path or not text:
                    rejected.append({'sheet': name, 'row': number, 'file': raw, 'reason': 'invalid filename' if not path else 'empty text'})
                    continue
                if raw.rstrip().endswith((',', ';')): repairs.append({'sheet': name, 'row': number, 'file': raw})
                captions[path][text] += 1
                locations[path].append({'sheet': name, 'row': number})
    result = {p: sorted(v, key=lambda t: (-v[t], t)) for p, v in sorted(captions.items())}
    conflicts = {p: {'variants': values, 'locations': locations[p]} for p, values in result.items() if len(values) > 1}
    return result, {'sheets': names, 'paths': len(result), 'conflicts': conflicts, 'repaired': repairs, 'rejected': rejected}

def subrecords(data: bytes):
    pos = 0
    while pos < len(data):
        kind, size = struct.unpack_from('<4sI', data, pos); pos += 8
        if pos + size > len(data): raise ValueError('Truncated game subrecord')
        yield kind, data[pos:pos+size]; pos += size

def script_captions(game_data: Path):
    result = {}
    for name in ('Morrowind.esm', 'Tribunal.esm', 'Bloodmoon.esm'):
        with (game_data / name).open('rb') as stream:
            while header := stream.read(16):
                kind, size, _, _ = struct.unpack('<4sIII', header)
                if kind != b'SCPT': stream.seek(size, 1); continue
                for field, data in subrecords(stream.read(size)):
                    if field != b'SCTX': continue
                    source = data.rstrip(b'\0').decode('cp1252', errors='replace')
                    # Strip comments, preserving semicolons within quoted text.
                    source = '\n'.join(re.split(r';(?=(?:[^\"]*\"[^\"]*\")*[^\"]*$)', line, maxsplit=1)[0] for line in source.splitlines())
                    for m in re.finditer(r'(?im)\bsay\s*,?\s*"([^"\r\n]+)"\s*,?\s*"([^"\r\n]+)"', source):
                        path, text = audio_path(m[1]), clean(m[2])
                        if path and text: result[path] = text
    return dict(sorted(result.items()))

def lua_string(value):
    # Control bytes must not become JSON-only \uNNNN escapes in Lua source.
    value = ''.join(c for c in value if ord(c) >= 32 and ord(c) != 127)
    return json.dumps(value, ensure_ascii=False)

def render(sheet, scripts, automatic, digest):
    lines = ['-- Generated by scripts/import_hotkeys_voice_transcripts.py.',
             '-- Source: ' + SOURCE, '-- Workbook SHA256: ' + digest,
             '-- Installed INFO subtitles take precedence; alternate spreadsheet text remains searchable.', 'return {']
    for group, entries in [('sheet', sheet), ('scripts', scripts), ('automatic', automatic)]:
        lines.append('  ' + group + '={')
        for path, value in sorted(entries.items()):
            encoded = '{' + ','.join(lua_string(v) for v in value) + '}' if isinstance(value, list) else lua_string(value)
            lines.append('    [' + lua_string(path) + ']=' + encoded + ',')
        lines.append('  },')
    return '\n'.join(lines + ['}', ''])

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workbook', type=Path, required=True)
    parser.add_argument('--game-data', type=Path, required=True)
    parser.add_argument('--automatic', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    sheet, report = import_rows(read_workbook(args.workbook))
    scripts = script_captions(args.game_data)
    automatic = {}
    withheld = 0
    if args.automatic and args.automatic.exists():
        latest = {}
        for line in args.automatic.read_text(encoding='utf-8').splitlines():
            row = json.loads(line); path = audio_path(row['path'])
            if path: latest[path] = row
        for path,row in latest.items():
            text = clean(row.get('text', ''))
            # Isolated syllables in attack/hit clips are especially prone to
            # speech-model hallucination. Keep them in the review log, not UI text.
            if row['status'] == 'automatic' and len(re.findall(r'[A-Za-z]+',text)) >= 2:
                automatic[path] = text
            else: withheld += 1
    digest = hashlib.sha256(args.workbook.read_bytes()).hexdigest()
    report.update(source=SOURCE, workbookSHA256=digest, scriptPaths=len(scripts), automaticPaths=len(automatic),automaticWithheld=withheld)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(render(sheet, scripts, automatic, digest), encoding='utf-8', newline='\n')
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({k: v for k, v in report.items() if k not in ('conflicts', 'repaired', 'rejected')}, ensure_ascii=True))

if __name__ == '__main__': main()
