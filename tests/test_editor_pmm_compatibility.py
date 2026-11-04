"""Public PMM v1/v2 relocation, duplicate-name and polygon accessory regression."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import cpu_import_fixture


def fixed(value):
    encoded = value.encode('cp932')
    assert len(encoded) < 256
    return encoded + bytes(256 - len(encoded))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--test', type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='mds-relocated-pmm-') as temporary:
        base = Path(temporary)
        cpu_import_fixture.write(base)
        project = base / 'relocated' / 'UserFile'
        for folder in ('Model', 'Accessory', 'Scenes'):
            (project / folder).mkdir(parents=True)
        original = (base / 'articulated.pmd').read_bytes()
        paths = []
        for duplicate in (False, True):
            name = '\u30e2\u30c7\u30eb' + ('-duplicates' if duplicate else '') + '.pmd'
            model = original.replace(b'E' + bytes(19), b'L' + bytes(19)) if duplicate else original
            assert not duplicate or model != original
            (project / 'Model' / name).write_bytes(model)
            for version in (1, 2):
                scene = cpu_import_fixture.scene(version, 'articulated.pmd', True, 2, bindings=True)
                scene = scene.replace(fixed('articulated.pmd'), fixed('G:\\old\\UserFile\\Model\\' + name))
                scene = scene.replace(fixed('bound.x'), fixed('G:\\old\\UserFile\\Accessory\\polygon.x'))
                if duplicate and version == 2:
                    assert b'\x01R\x01L\x01E\x01I' in scene
                    scene = scene.replace(b'\x01R\x01L\x01E\x01I', b'\x01R\x01L\x01L\x01I')
                path = project / 'Scenes' / f'v{version}-duplicate-{int(duplicate)}.pmm'
                path.write_bytes(scene)
                paths.append(path)
        (project / 'Accessory' / 'polygon.x').write_text('''xof 0303txt 0032
Mesh {4; -1;0;0;,1;0;0;,1;2;0;,-1;2;0;; 1;4;0,1,2,3;;
MeshTextureCoords {4;0;0;,1;0;,1;1;,0;1;;}
MeshVertexColors {1;0;1;0;0;1;;}
MeshMaterialList {1;1;0;; Material {1;.5;.25;1;;0;0;0;0;;0;0;0;;}}
}''', encoding='ascii')
        subprocess.run([str(args.test.resolve()), str(base / 'saved'), *map(str, paths)],
                       check=True, timeout=120)


if __name__ == '__main__':
    main()
