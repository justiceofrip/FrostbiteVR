"""Read-only native-operation build pins, supporting legacy and generated wiring."""
from pathlib import Path
import hashlib,re
import bc2_native_operation_receipts as recipe

KEYS=('BC2_NATIVE_OPERATION_CAPABILITIES_HEADER','BC2_NATIVE_OPERATION_SOURCE_RECEIPT','BC2_NATIVE_OPERATION_SOURCE_SHA256')
CONFIG='BC2_NATIVE_OPERATION_BUILD_CONFIG'
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def collect(source,cache,ninja):
 values={}
 for line in cache.splitlines():
  m=re.fullmatch(r'([^:#]+):[^=]+=(.*)',line)
  if m and m[1] in (*KEYS,CONFIG,'CMAKE_CACHEFILE_DIR'):
   recipe.require(m[1] not in values,'duplicate operation cache key');values[m[1]]=m[2]
 parts=[values.get(k,'') for k in KEYS];present=sum(bool(p) for p in parts)
 recipe.require(present in (0,3),'partial operation class cache provenance')
 normalized=ninja.replace('\\','/').replace('$:',':')
 macros=('FVR_BC2_NATIVE_OPERATION_CAPABILITIES_HEADER','FVR_BC2_NATIVE_OPERATION_BUILD_SHA256')
 generated=None;header=Path(parts[0]) if present else None;digest=parts[2]
 if CONFIG in values:
  generated=Path(values[CONFIG]);build=Path(values.get('CMAKE_CACHEFILE_DIR',''))
  recipe.require(generated.is_absolute() and build.is_absolute(),'generated operation configuration path is relative')
  recipe.require(generated==build/'generated/bc2-operation/Bc2NativeOperationBuild.generated.h','generated operation configuration outside this build')
  recipe.require(not any(re.search(r'(?:-D|/D)'+m+r'(?:=|\s|$)',ninja) for m in macros),'generated configuration mixed with legacy definitions')
  directory=generated.parent.as_posix()
  recipe.require(re.search(r'(?:-I|/I)\s*"?'+re.escape(directory)+r'(?:"|\s|$)',normalized) is not None,'generated operation include directory missing from compiler wiring')
  recipe.require(generated.as_posix() in normalized and 'verify-build-config' in ninja,'generated operation build guard missing')
  recipe.verify_build_config(generated,header,digest)
 else:
  if not present:recipe.require(not any(m in ninja for m in macros),'compiled class macro without cache provenance')
  else:
   recipe.require(all(m in ninja for m in macros),'configured class missing compiled macros')
   recipe.require(header.as_posix() in normalized and digest in ninja,'compiled macro value differs from cache')
 out=dict(configured=bool(present),compiled_configuration_verified=bool(present),registration_count=None,native_admission_granted_by_freezer=False)
 if generated:out['generated_configuration']={'path':str(generated),'sha256':sha(generated)}
 if not present:return out
 receipt=Path(parts[1]);source=Path(source)
 recipe.require(header.is_absolute() and receipt.is_absolute() and header.is_file() and receipt.is_file(),'external provenance files missing or relative')
 recipe.require(re.fullmatch('[0-9a-f]{64}',digest) is not None,'invalid source digest')
 recipe.verify_source(source,receipt,digest,header)
 out.update(source_sha256=digest,reviewed_header={'path':str(header),'sha256':sha(header)},
  source_receipt={'path':str(receipt),'sha256':sha(receipt)},
  verification_recipe={'path':str(source/'tools/bc2_native_operation_receipts.py'),'sha256':sha(source/'tools/bc2_native_operation_receipts.py')},
  build_provenance_recipe={'path':str(Path(__file__).resolve()),'sha256':sha(__file__)},
  limit='Exact configured build pins; native acceptance and loaded binary identity remain separate evidence.')
 return out
