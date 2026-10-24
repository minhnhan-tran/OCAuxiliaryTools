#!/usr/bin/env python3
"""Regenerates src/efibuilder/efidata.json from lzhoang2801/OpCore-Simplify
(BSD-3-Clause) Scripts/datasets/*.py. Usage: gen_efi_data.py <OpCore-Simplify checkout> <out.json>"""
import importlib, json, sys
sys.path.insert(0, sys.argv[1])
def conv(v):
    if isinstance(v, dict): return {str(k): conv(x) for k, x in v.items()}
    if isinstance(v, (list, tuple, set, frozenset)): return [conv(x) for x in v]
    if hasattr(v, '__dict__') and not callable(v): return {k: conv(x) for k, x in vars(v).items() if not k.startswith('_')}
    return v if isinstance(v, (int, float, str, bool, type(None))) else str(v)
out = {}
for name in ('os_data', 'cpu_data', 'gpu_data', 'pci_data', 'codec_layouts', 'mac_model_data', 'chipset_data', 'kext_data'):
    m = importlib.import_module('Scripts.datasets.' + name)
    out[name] = {k: conv(v) for k, v in vars(m).items()
                 if not k.startswith('_') and not callable(v) and not hasattr(v, '__spec__')}
# Keep only a compact download source per kext: "gh" = owner/repo (GitHub
# releases) or "url" = direct zip. Everything else in the metadata is dropped.
for k in out.get('kext_data', {}).get('kexts', []):
    g = k.pop('github_repo', None) or {}
    d = k.pop('download_info', None) or {}
    if g.get('owner') and g.get('repo'): k['gh'] = g['owner'] + '/' + g['repo']
    if d.get('url'): k['url'] = d['url']
json.dump(out, open(sys.argv[2], 'w'), separators=(',', ':'), sort_keys=True)
