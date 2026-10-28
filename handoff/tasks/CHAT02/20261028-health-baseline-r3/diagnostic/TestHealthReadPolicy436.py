"""Pure fake-memory regression. No process access, native calls, or game needed."""
from pathlib import Path
import unittest
from HealthReadPolicy436 import load_proof, verify_spans, LEGACY_STATIC_PREFIXES

PROOF = Path(r'G:\Unity\VRMMO\bc2vr-recovery\headset391-investigation\actual-death395-preparation\native-health-consumer395.json')


class Image:
    base = 0x400000
    def __init__(self, spans):
        self.rows = {(v['address']-self.base,v['size']):bytes.fromhex(v['bytes_hex']) for v in spans.values()}
    def read(self, address, size):
        return self.rows[(address,size)]


class HealthReadPolicyTests(unittest.TestCase):
    def setUp(self):
        self.raw = PROOF.read_bytes()
        self.proof = load_proof(self.raw)
        self.spans = {**self.proof['code_spans'], **self.proof['data_spans']}
        self.image = Image(self.spans)
        self.live = {(v['address'],v['size']):bytes.fromhex(v['bytes_hex']) for v in self.spans.values()}
        self.reads = []

    def read(self, address, size):
        self.reads.append((address,size))
        return self.live[(address,size)]

    def test_original_proof_tamper_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Offline health proof changed'):
            load_proof(self.raw+b' ')

    def test_only_damage_function_loses_live_equality(self):
        damage = self.spans['server_damage']
        key = damage['address'],damage['size']
        self.live[key] = b'\xe9\x01\x02\x03\x04'+self.live[key][5:]
        report = verify_spans(self.proof,self.image,self.read)
        expected = [name for name in self.spans if not name.startswith(LEGACY_STATIC_PREFIXES) and name != 'server_damage']
        self.assertEqual(report['verified_native_spans'],expected)
        self.assertEqual(len(expected),18)
        self.assertEqual(len(report['disk_verified_native_spans']),27)
        self.assertIn('server_damage',report['disk_verified_native_spans'])
        self.assertNotIn(key,self.reads)
        self.assertFalse(report['native_damage_consumer_live_verified'])
        self.assertFalse(report['damage_hook_followed'])
        self.assertFalse(report['actual_death'])
        self.assertTrue(report['independent_presentation_required'])

    def test_every_other_required_live_span_still_rejected(self):
        for name,item in self.spans.items():
            if name.startswith(LEGACY_STATIC_PREFIXES) or name == 'server_damage':
                continue
            with self.subTest(name=name):
                key = item['address'],item['size']
                original = self.live[key]
                self.live[key] = bytes([original[0]^1])+original[1:]
                with self.assertRaisesRegex(ValueError, 'Live health-read proof mismatch: '+name):
                    verify_spans(self.proof,self.image,self.read)
                self.live[key] = original

    def test_every_original_disk_span_still_rejected(self):
        for name,item in self.spans.items():
            with self.subTest(name=name):
                key = item['address']-self.image.base,item['size']
                original = self.image.rows[key]
                self.image.rows[key] = bytes([original[0]^1])+original[1:]
                with self.assertRaisesRegex(ValueError, 'Offline native proof mismatch: '+name):
                    verify_spans(self.proof,self.image,self.read)
                self.image.rows[key] = original


if __name__ == '__main__':
    unittest.main()
