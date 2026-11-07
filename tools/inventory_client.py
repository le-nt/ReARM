#!/usr/bin/env python3
"""Inventory user-supplied Android native inputs without executing or extracting them.

SPDX-License-Identifier: Apache-2.0
Reports contain metadata and hashes, never library or asset contents. An inventory
records supplied provenance; it does not establish permission to redistribute inputs.
"""
from __future__ import annotations
import argparse
from collections import Counter
import hashlib
import io
import json
from pathlib import Path
import sys
import zipfile

MAX_LIBRARY_BYTES = 512 * 1024 * 1024


def file_record(path: Path) -> dict:
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(block)
    return {'name': path.name, 'size_bytes': path.stat().st_size, 'sha256': digest.hexdigest()}


def inspect_elf(data: bytes, name: str) -> dict:
    from elftools.elf.elffile import ELFFile
    from elftools.elf.enums import ENUM_RELOC_TYPE_ARM
    if len(data) > MAX_LIBRARY_BYTES:
        raise ValueError('library exceeds inventory size limit')
    elf = ELFFile(io.BytesIO(data))
    segments = list(elf.iter_segments())
    dynamic = next((segment for segment in segments if segment['p_type'] == 'PT_DYNAMIC'), None)
    tags = list(dynamic.iter_tags()) if dynamic else []
    section = elf.get_section_by_name('.dynsym')
    symbols = list(section.iter_symbols()) if section else list(dynamic.iter_symbols()) if dynamic else []
    imports, exports = [], []
    for symbol in symbols:
        if not symbol.name:
            continue
        info = {'name': symbol.name, 'type': symbol['st_info']['type'],
                'binding': symbol['st_info']['bind'], 'visibility': symbol['st_other']['visibility'],
                'value': symbol['st_value'], 'size_bytes': symbol['st_size']}
        (imports if symbol['st_shndx'] == 'SHN_UNDEF' else exports).append(info)
    relocation_names = {value: key for key, value in ENUM_RELOC_TYPE_ARM.items() if isinstance(value, int)}
    relocations = []
    if dynamic:
        seen = set()
        for format_name, table in dynamic.get_relocation_tables().items():
            for relocation in table.iter_relocations():
                info = relocation.entry.get('r_info', 0)
                key = (relocation['r_offset'], info, format_name == 'RELR')
                if key in seen:
                    continue
                seen.add(key)
                kind = 23 if format_name == 'RELR' and elf['e_machine'] == 'EM_ARM' else relocation.entry.get('r_info_type', 0)
                relocations.append({'address': relocation['r_offset'], 'type_id': kind,
                    'type': relocation_names.get(kind, str(kind)) if elf['e_machine'] == 'EM_ARM' else str(kind),
                    'symbol_index': relocation.entry.get('r_info_sym', 0), 'format': format_name})
    unparsed = [tag.entry.d_tag for tag in tags if tag.entry.d_tag in
                ('DT_ANDROID_REL', 'DT_ANDROID_RELA', 0x6000000f, 0x60000011)]
    constructors = {tag.entry.d_tag: tag.entry.d_val for tag in tags if tag.entry.d_tag in
                    ('DT_INIT', 'DT_FINI', 'DT_INIT_ARRAY', 'DT_INIT_ARRAYSZ', 'DT_FINI_ARRAY', 'DT_FINI_ARRAYSZ')}
    return {'name': name, 'sha256': hashlib.sha256(data).hexdigest(), 'size_bytes': len(data),
        'elf_class': elf.elfclass, 'little_endian': elf.little_endian, 'machine': elf['e_machine'],
        'type': elf['e_type'], 'entry': elf['e_entry'], 'arm_e_flags': elf['e_flags'],
        'initial_arm32_profile': elf.elfclass == 32 and elf.little_endian and elf['e_machine'] == 'EM_ARM',
        'needed': [tag.needed for tag in tags if tag.entry.d_tag == 'DT_NEEDED'],
        'soname': next((tag.soname for tag in tags if tag.entry.d_tag == 'DT_SONAME'), None),
        'load_segments': [{'address': s['p_vaddr'], 'file_offset': s['p_offset'], 'file_bytes': s['p_filesz'],
                           'memory_bytes': s['p_memsz'], 'flags': s['p_flags']} for s in segments if s['p_type'] == 'PT_LOAD'],
        'imports': imports, 'exports': exports,
        'jni_exports': [s for s in exports if s['name'] in ('JNI_OnLoad', 'JNI_OnUnload') or s['name'].startswith('Java_')],
        'relocations': relocations, 'relocation_counts': dict(Counter(r['type'] for r in relocations)),
        'unparsed_relocation_formats': unparsed,
        'initialization_tags': constructors,
        'has_tls_segment': any(s['p_type'] == 'PT_TLS' for s in segments),
        'has_symbol_versions': any(tag.entry.d_tag in ('DT_VERNEED', 'DT_VERDEF', 'DT_VERSYM') for tag in tags),
        'has_arm_attributes': elf.get_section_by_name('.ARM.attributes') is not None,
        'instruction_extensions': 'not inferred; requires instruction discovery/lifting',
        'runtime_compatibility': 'not established by inventory'}


def inventory(libraries: list[Path], apks: list[Path], assets: list[Path], version: str, provenance: str) -> dict:
    report = {'schema_version': 1, 'version': version, 'provenance_supplied': provenance,
              'libraries': [], 'apks': [], 'assets': [file_record(path) for path in assets]}
    for path in libraries:
        if path.stat().st_size > MAX_LIBRARY_BYTES:
            raise ValueError(f'library exceeds size limit: {path.name}')
        report['libraries'].append(inspect_elf(path.read_bytes(), path.name))
    for path in apks:
        report['apks'].append(file_record(path))
        with zipfile.ZipFile(path) as archive:
            for item in archive.infolist():
                if item.filename.startswith('lib/') and item.filename.endswith('.so'):
                    if item.file_size > MAX_LIBRARY_BYTES:
                        raise ValueError(f'APK library exceeds size limit: {item.filename}')
                    with archive.open(item) as native:
                        data = native.read(MAX_LIBRARY_BYTES + 1)
                    if len(data) != item.file_size or len(data) > MAX_LIBRARY_BYTES:
                        raise ValueError(f'APK library size mismatch: {item.filename}')
                    report['libraries'].append(inspect_elf(data, f'{path.name}!{item.filename}'))
    if not report['libraries']:
        raise ValueError('no native libraries supplied or found in APKs')
    report['notes'] = ['Metadata only; no client code executed.',
                       'Retain proprietary inputs and this client-specific report outside public Git artifacts.']
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path, action='append', default=[])
    parser.add_argument('--apk', type=Path, action='append', default=[])
    parser.add_argument('--asset', type=Path, action='append', default=[])
    parser.add_argument('--version', required=True)
    parser.add_argument('--provenance', required=True, help='authorized source/context supplied by the input owner')
    parser.add_argument('--out', type=Path, default=Path('build/client-inventory.json'))
    args = parser.parse_args()
    report = inventory(args.library, args.apk, args.asset, args.version, args.provenance)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'libraries': len(report['libraries']), 'report': str(args.out)}))
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f'inventory_client: {error}', file=sys.stderr)
        raise SystemExit(2)
