#!/usr/bin/env python3
"""Re-sync translations/*.md against capabase.md after new strings appear.

Run tools/gen-capabase.py first (it rewrites capabase.md from the code), then
this: every other .md is rebuilt with capabase's key set — entries that already
had a translation keep it, new keys arrive with the capabase text, and keys that
no longer exist in the code are dropped.

    python3 tools/gen-capabase.py && python3 tools/sync-translations.py

Reports how many entries of each file are still identical to capabase, i.e. how
much is left to translate. The Apps section is left alone: it is filled at
runtime from the installed .desktop files, not from the source tree.
"""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "translations"
SEP = " = "


def parse(path):
    """{section: {key: value}} plus the section order, ignoring comments."""
    out, order, section, in_comment = {}, [], None, False
    for line in path.read_text(encoding="utf-8").split("\n"):
        t = line.strip()
        if in_comment:
            in_comment = "-->" not in t
            continue
        if t.startswith("<!--"):
            in_comment = "-->" not in t
            continue
        if t.startswith("##"):
            section = t[2:].strip()
            order.append(section)
            out.setdefault(section, {})
            continue
        if not t or t.startswith("#") or SEP not in line:
            continue
        key, value = line.split(SEP, 1)
        out.setdefault(section, {})[key] = value
    return out, order


def main():
    base, order = parse(ROOT / "capabase.md")
    for path in sorted(ROOT.glob("*.md")):
        if path.name == "capabase.md":
            continue
        old, _ = parse(path)
        # The header block is the file's own (it names the language).
        header = []
        for line in path.read_text(encoding="utf-8").split("\n"):
            if line.strip().startswith("##"):
                break
            header.append(line)

        lines = list(header)
        untranslated = 0
        for section in order:
            lines.append("## " + section)
            if section.lower() == "apps":
                # Runtime-owned: keep whatever the file has.
                for key, value in old.get(section, {}).items():
                    lines.append(key + SEP + value)
                lines.append("")
                continue
            for key, capa in base[section].items():
                value = old.get(section, {}).get(key, capa)
                if value == capa:
                    untranslated += 1
                lines.append(key + SEP + value)
            lines.append("")
        path.write_text("\n".join(lines).rstrip("\n") + "\n", encoding="utf-8")
        print(f"{path.name}: {untranslated} entries still identical to capabase")
    return 0


if __name__ == "__main__":
    sys.exit(main())
