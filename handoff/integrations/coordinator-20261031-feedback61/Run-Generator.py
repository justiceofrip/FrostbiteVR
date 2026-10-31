"""Run the existing generator contracts under the bundled isolated Python."""
from pathlib import Path
import runpy,sys
root=Path(__file__).resolve().parents[3]
sys.dont_write_bytecode=True
sys.path.insert(0,str(root/'tools'))
runpy.run_path(str(root/'tools/test_bc2_mechanism_registry_header.py'),run_name='__main__')
