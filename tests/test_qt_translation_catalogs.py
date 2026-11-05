"""Check ID/placeholder integrity and a lossless native lupdate round trip."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import re
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import translation_catalog as catalog


def contents(path):
    entries = ET.parse(path).getroot().findall('./context/message')
    return {m.attrib['id']: (m.findtext('source', ''), m.findtext('translation', ''),
                            m.find('translation').get('type')) for m in entries}


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--lupdate', required=True)
args = parser.parse_args()
catalog.check()
expected_languages = {'en', 'ja', 'zh_CN', 'zh_TW', 'ko', 'fr', 'de', 'es',
                      'it', 'pt_BR', 'ru', 'nl', 'pl', 'tr', 'vi'}
assert {path.stem.removeprefix('mikudancestudio_') for path in
        catalog.DIRECTORY.glob('mikudancestudio_*.ts')} == expected_languages
supplement = ET.parse(catalog.DIRECTORY / 'qtbase_vi.ts').getroot()
assert supplement.attrib['language'] == 'vi'
for message in supplement.findall('./context/message'):
    source, target = message.findtext('source', ''), message.findtext('translation', '')
    assert target and message.find('translation').get('type') is None, source
    assert sorted(re.findall(r'%(?:L?\d+|n)', source)) == sorted(
        re.findall(r'%(?:L?\d+|n)', target)), source
with tempfile.TemporaryDirectory(prefix='mds-linguist-') as directory:
    copies = []
    for source in sorted(catalog.DIRECTORY.glob('mikudancestudio_*.ts')):
        copy = Path(directory) / source.name
        shutil.copy2(source, copy)
        copies.append((source, copy))
    result = subprocess.run([args.lupdate, str(catalog.HEADER), '-locations', 'none',
                             '-no-obsolete', '-ts', *[str(copy) for _, copy in copies]],
                            capture_output=True, text=True, encoding='utf-8', errors='replace')
    assert result.returncode == 0, result.stdout + result.stderr
    for source, copy in copies:
        assert contents(source) == contents(copy), f'lupdate changed finished translations: {source}'
print(f'All {len(copies)} catalogs survive native Qt lupdate without lost or unfinished translations')
