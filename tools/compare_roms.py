#!/usr/bin/env python3
"""Compare recipe ROM ranges across clean BPEE/BPES dumps, without exporting game bytes.

This is an investigation tool, not a Spanish recipe generator. A byte match
cannot identify a translated symbol or prove equivalent script behaviour.
"""
from __future__ import annotations

import argparse
import json
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'builder'))
from emerald3ds_builder.rom import load_rom  # noqa: E402
from emerald3ds_builder.recipe import Recipe  # noqa: E402


def compare(reference: bytes, candidate: bytes, recipe: Recipe) -> dict:
    cache: dict[tuple[int, int], int] = {}
    domains: dict[str, Counter] = {}
    entries = []
    for entry in recipe.entries + recipe.inputs:
        domain = entry['path'].split('/')[0]
        stats = domains.setdefault(domain, Counter())
        missing = []
        matched = 0
        for op in entry['ops']:
            if op[0] != 'C':
                stats['other_operations'] += 1
                continue
            _, offset, length = op
            stats['copy_bytes'] += length
            # Short windows are too ambiguous to be useful as location hints.
            if length < 32:
                stats['short_copy_bytes'] += length
                continue
            key = offset, length
            if key not in cache:
                data = reference[offset:offset + length]
                same = candidate[offset:offset + length] == data
                cache[key] = offset if same else candidate.find(data)
            found = cache[key]
            if found < 0:
                stats['missing_copy_bytes'] += length
                missing.append({'offset': offset, 'length': length})
            else:
                stats['matched_copy_bytes'] += length
                if found != offset:
                    stats['relocated_copy_bytes'] += length
                matched += length
        if missing:
            entries.append({'path': entry['path'], 'missing_ranges': missing,
                            'matched_copy_bytes': matched})
    return {'minimum_match_bytes': 32,
            'interpretation': 'Exact byte matches only; no inferred translated symbols or gameplay compatibility.',
            'domains': {name: dict(counts) for name, counts in sorted(domains.items())},
            'entries_with_missing_ranges': entries}


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--english', type=Path, required=True)
    ap.add_argument('--spanish', type=Path, required=True)
    ap.add_argument('--recipe', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    english, spanish = load_rom(args.english), load_rom(args.spanish)
    recipe = Recipe.load(args.recipe)
    if (english.code, spanish.code) != ('BPEE', 'BPES'):
        ap.error('Expected English BPEE and Spanish BPES ROMs in that order.')
    if recipe.rom_sha1 != english.sha1:
        ap.error('The recipe must match the English reference ROM.')
    report = compare(english.data, spanish.data, recipe)
    report.update(reference_sha1=english.sha1, candidate_sha1=spanish.sha1,
                  recipe_release=recipe.release)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    for domain, counts in report['domains'].items():
        print('%s: matched %d, missing %d, short/ambiguous %d copy bytes' %
              (domain, counts.get('matched_copy_bytes', 0), counts.get('missing_copy_bytes', 0),
               counts.get('short_copy_bytes', 0)))
    print('No ROM bytes were written. Report:', args.output)


if __name__ == '__main__':
    main()
