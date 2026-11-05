"""Check ID/placeholder integrity and a lossless native lupdate round trip."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
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
print('All eight catalogs survive native Qt lupdate without lost or unfinished translations')
