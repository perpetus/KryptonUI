"""Copy the onboarding subset from the cloned icon repositories (not a build step)."""

import json
from pathlib import Path
import shutil
import subprocess
from urllib.request import urlopen


def main():
    root = Path(__file__).resolve().parents[1]
    icons = root / 'design/icons'
    catalog = json.loads((icons / 'onboarding.json').read_text(encoding='utf-8'))
    copied = 0
    for item in catalog:
        for key, destination in [('source', 'file'), ('dark_source', 'dark_file')]:
            source = item.get(key)
            if not source:
                continue
            target = icons / item[destination]
            target.parent.mkdir(parents=True, exist_ok=True)
            if source.startswith('https://'):
                with urlopen(source, timeout=30) as response:
                    target.write_bytes(response.read())
            else:
                shutil.copyfile(root / '.deps' / source, target)
            copied += 1
    notices = icons / 'onboarding/licenses'
    notices.mkdir(exist_ok=True)
    revisions = []
    for name, license_name in [('simple-icons', 'LICENSE.md'), ('svgl', 'LICENSE')]:
        repository = root / '.deps' / name
        shutil.copyfile(repository / license_name, notices / (name + '.txt'))
        revision = subprocess.check_output(
            ['git', '-C', str(repository), 'rev-parse', 'HEAD'], text=True).strip()
        revisions.append(f'{name}: {revision}')
    (notices / 'revisions.txt').write_text('\n'.join(revisions) + '\n', encoding='utf-8')
    # Retain per-brand source and license metadata for the copied Simple Icons.
    slugs = {Path(item['source']).stem for item in catalog
             if item.get('source', '').startswith('simple-icons/')}
    metadata = json.loads((root / '.deps/simple-icons/data/simple-icons.json').read_text(encoding='utf-8'))
    selected = []
    for item in metadata:
        slug = item.get('slug', item['title'].lower().replace(' ', ''))
        if slug in slugs:
            selected.append(item)
    (notices / 'simple-icons-metadata.json').write_text(
        json.dumps(selected, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f'Copied {copied} onboarding icons and both repository licenses.')


if __name__ == '__main__':
    main()
