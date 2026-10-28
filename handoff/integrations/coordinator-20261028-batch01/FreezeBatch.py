"""Reuse the reviewed six-pair freezer with this coordinator's exact lease.

The imported verifier retains its legacy schema names and all source, normal-flag,
test, storage and PE/PDB identity checks. The caller supplies the new run/version.
"""
import hashlib
import importlib.util
from pathlib import Path
import sys

sys.dont_write_bytecode = True
recipe = Path(__file__).resolve().parent.parent / "chat25-chat26-20261028-integration01" / "FreezeRuntime.py"
expected = "03f9a10d5e0b09f5157da40bf447ecfa0aaab3f21e2ccef59dd3fa15bd357076"
if hashlib.sha256(recipe.read_bytes()).hexdigest() != expected:
    raise RuntimeError("Reviewed runtime verifier changed")
spec = importlib.util.spec_from_file_location("coordinator_runtime_freezer", recipe)
freezer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(freezer)
freezer.LEASE_TOKEN = "campaign-coordinator-20261028-r1-batch01-70571f7dcb734b658814f1d2a17765c1"
if __name__ == "__main__":
    freezer.main()
