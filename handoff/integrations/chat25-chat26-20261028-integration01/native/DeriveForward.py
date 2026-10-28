"""Rebind the existing offline rotation calculation to this integration's evidence."""
import argparse
import hashlib
import importlib.util
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--direction", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
root = Path("G:/Unity/VRMMO/FrostbiteVR/reports/integration-chat25-chat26-20261028-01/native")
if not args.direction.resolve().is_relative_to(root) or not args.output.resolve().is_relative_to(root):
    raise ValueError("This integration's current direction and a new output are required")
source = Path("G:/Unity/VRMMO/bc2vr-recovery/overnight426/knife/forward427/MeasureForwardKnife427.py")
if hashlib.sha256(source.read_bytes()).hexdigest() != "0389e37e8f43e4fa8de9a775c3f0dbfd3cde247c1f3217eec1c401a6937ef1ba":
    raise ValueError("Reviewed offline rotation calculation changed")
spec = importlib.util.spec_from_file_location("alignment427", source)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
module.EVIDENCE = args.direction.resolve()
module.OUTPUT = args.output.resolve()
module.main()
