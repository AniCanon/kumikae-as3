"""Named case identities shared by source actions and result navigation."""
import re
from urllib.parse import urlencode


def validate(case):
    identities = set()
    for entry in case.get('cases', []):
        if not isinstance(entry, dict) or any(not isinstance(entry.get(key), str) or not entry[key]
                                             for key in ('source', 'suite', 'name', 'method')):
            raise ValueError('Case mappings need source, suite, name and method')
        if any('::' in entry[key] or '\n' in entry[key] or '\r' in entry[key] for key in ('suite','name')):
            raise ValueError('Case names must be single-line and cannot contain ::')
        if not re.fullmatch(r'[A-Za-z_$][\w$]*', entry['method']):
            raise ValueError('Invalid test method: ' + entry['method'])
        identity = (entry['suite'], entry['name'])
        if identity in identities:
            raise ValueError('Duplicate case identity')
        identities.add(identity)


def location(root, case, suite, name):
    for entry in case.get('cases', []):
        if entry['suite'] == suite and entry['name'] == name:
            source = (root / entry['source']).as_uri().replace('file:', 'kumikae-as3:', 1)
            return source + '?' + urlencode({'method': entry['method']})
    return (root / case.get('source', '')).as_uri()
