"""Explicit documentary-pin successor; all generic admission predicates remain."""
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import sys

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
REPO = Path(r'G:\Unity\VRMMO\FrostbiteVR')
EVIDENCE = REPO / 'reports/coordinator-20261028-runtime02/magazine'
PLAN = Path(r'G:\Unity\VRMMO\bc2vr-recovery\performance436\campaign-magazine-batch-r55-prep\plans\sp01-nambu.json')
DOCUMENTS = {
    'WEAPON-SYSTEM-PIPELINE.md': (
        '98ace2f1f93cf12148e694cb88f7693ccca23c31224d5b170ea89e4b28454470', 42336,
        '4f633e68ae77d850d82fa0b58f5ea423bf3a3edc2cf11f69f61e82448cafb5a9', 42424),
    'WEAPON-COVERAGE-CURRENT.md': (
        '9dcb580a96d94455e4ec6df319d8021c71fcd12c37105d12288cc3eb1c04bccc', 72400,
        '158d211c2e5f4e2b5bd97fe09c89ddb894082551a1fb3fd71ba6cd66e1b6e657', 73643),
    'EMPTY-PREDICTION-293.md': (
        '83b39573b3b6f66c34c56b1467af269e8844606331330282f0fdca5b2fb5469f', 4651,
        '016cf5a88ee4010cdbe69988a3785f8760aecaa2300729e95a729e8cf036b10c', 4659),
}
ORIGINALS = {
    'original_admission': (HERE / 'Admission.py', 827,
        '013db57a27871290566c303f4fccfab1b6e0de29762d179ee52d2c4672461ad5'),
    'original_runner': (HERE / 'RunReady.ps1', 26555,
        'e65a5717017fc59ce5d75e00d11e16390c5a8512da5cb96afeaa10b36f087167'),
    'build': (EVIDENCE / 'build01/run01/build-receipt.json', 233171,
        '273b0504dfa002ead9a5ee7b92edc2ff7867875cf84e95857cb255cbec17625c'),
    'runtime_binding': (EVIDENCE / 'runtime-binding.json', 1656,
        'ccfe3148c2fa26a91bb0c7c7017a04ac23c0f0273a435c68ec8acbda96fad1b4'),
    'admission_inputs': (EVIDENCE / 'admission-inputs.json', 3284,
        'd4476b4001b8cf82c0fe8fabd0be41bd268138e6d20bb2860801fdb13d6c0e18'),
    'plan': (PLAN, 751783,
        '0e1df14474dc91bee67da6d9c78996ca0e432863061efacce8c19b17567ba3ea'),
}
RUNNER_ANCHOR = b"$runnerPrep=@{admission_helper=@{path=(Join-Path $PSScriptRoot 'Admission.py')}}"
RUNNER_SUCCESSOR = RUNNER_ANCHOR.replace(b"'Admission.py'", b"'AdmissionReviewed.py'")


def need(condition, message):
    if not condition:
        raise ValueError(message)


def read(path, maximum=64 * 1024 * 1024):
    path = Path(path).resolve(strict=True)
    need(path.is_file() and path.stat().st_size <= maximum, 'Pinned input exceeds bound: ' + str(path))
    return path.read_bytes()


def pin(path, data):
    return {'path': str(Path(path).resolve()), 'bytes': len(data),
            'sha256': hashlib.sha256(data).hexdigest()}


def check_pin(value):
    need(isinstance(value, dict) and set(value) == {'path', 'bytes', 'sha256'}, 'Exact file pin required')
    data = read(value['path'])
    need(len(data) == value['bytes'] and hashlib.sha256(data).hexdigest() == value['sha256'],
         'Reviewed file changed: ' + value['path'])
    return data


def load_review(args):
    review_path = HERE / 'metadata-review.json'
    raw = read(review_path, 64 * 1024)
    review = json.loads(raw.decode('utf-8-sig'))
    need(review.get('schema') == 'fvr.runtime02.magazine.document-review.v1'
         and review.get('documentary_only') is True
         and review.get('native_admission_granted') is False
         and review.get('original_plan_preserved') is True,
         'Exact documentary-only review required')
    pins = review['pins']
    need(set(pins) == set(ORIGINALS) | {'reviewed_admission', 'reviewed_runner'}, 'Unexpected review pin set')
    for role, (path, size, sha) in ORIGINALS.items():
        need(pins[role] == {'path': str(path), 'bytes': size, 'sha256': sha}, 'Original pin differs: ' + role)
        check_pin(pins[role])
    for role, path in (('reviewed_admission', Path(__file__).resolve()),
                       ('reviewed_runner', HERE / 'RunMetadata.ps1')):
        need(pins[role]['path'] == str(path), 'Successor path differs: ' + role)
        check_pin(pins[role])
    original_runner = check_pin(pins['original_runner'])
    need(original_runner.count(RUNNER_ANCHOR) == 1 and
         check_pin(pins['reviewed_runner']) == original_runner.replace(RUNNER_ANCHOR, RUNNER_SUCCESSOR, 1),
         'RunMetadata must change only the admission-helper path')
    need(Path(args.plan).resolve() == PLAN.resolve() and args.plan_sha256 == pins['plan']['sha256'],
         'This review covers only the retained SP01 Nambu plan')
    need(Path(args.build).resolve() == Path(pins['build']['path']).resolve(), 'Review names another build')
    binding = json.loads(check_pin(pins['runtime_binding']).decode('utf-8-sig'))
    need(binding['build'] == pins['build'] and binding['admission'] == pins['admission_inputs'],
         'Original runtime binding differs')
    documents = review['documents']
    need(len(documents) == 3 and {row['name'] for row in documents} == set(DOCUMENTS),
         'Exactly the three reviewed documents are required')
    original_plan = json.loads(check_pin(pins['plan']).decode('utf-8-sig'))
    for row in documents:
        old_hash, old_size, current_hash, current_size = DOCUMENTS[row['name']]
        path = str(REPO / 'docs' / row['name'])
        old = {'path': path, 'bytes': old_size, 'sha256': old_hash}
        current = {'path': path, 'bytes': current_size, 'sha256': current_hash}
        need(row['original'] == old and row['current'] == current, 'Document pin set differs: ' + row['name'])
        need(sum(value == old for value in original_plan['inputs']) == 1, 'Original plan document differs')
        current_data = check_pin(current)
        if row['name'] != 'WEAPON-COVERAGE-CURRENT.md':
            restored = current_data.replace(b'(archive/', b'(')
            need(len(restored) == old_size and hashlib.sha256(restored).hexdigest() == old_hash,
                 'Document changes exceed reviewed archive-link routing')
    return review, pin(review_path, raw)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('plan', 'plan-sha256', 'definition', 'asset', 'build', 'geometry', 'geometry-sha256'):
        parser.add_argument('--' + name, required=True)
    args = parser.parse_args()
    review, review_pin = load_review(args)
    # Import the pinned wrapper without invoking its CLI. It retains its own
    # upstream hash check and all unchanged generic metadata/geometry predicates.
    original = runpy.run_path(review['pins']['original_admission']['path'],
                              run_name='_runtime02_original_metadata_admission')
    bench = original['bench']
    original_check_pin = bench.check_pin
    substituted = set()

    def reviewed_check_pin(value):
        for row in review['documents']:
            # Match the entire historical pin, including its exact path/size.
            # Similar paths, different hashes, and every non-document pin retain
            # the original function and its original failure behavior.
            if value == row['original']:
                result = original_check_pin(row['current'])
                substituted.add(row['name'])
                return result
        return original_check_pin(value)

    try:
        bench.check_pin = reviewed_check_pin
        result = original['admit'](args)
        need(substituted == set(DOCUMENTS), 'Admission did not consume the exact three reviewed document pins')
    finally:
        bench.check_pin = original_check_pin
    result['supplemental_document_review'] = review_pin
    result['documentary_pin_substitutions'] = [
        {'original': row['original'], 'current': row['current']} for row in review['documents']]
    result['original_plan_preserved'] = True
    print(json.dumps(result))


if __name__ == '__main__':
    main()
