# Pinned existing native diagnostic dependencies; importing this file performs no action.
function Assert-KnifeIntegrationDependencies {
 $pins=@{
  'G:\Unity\VRMMO\FrostbiteVR\tools\StorageBudget.ps1'='72f99931825f10970d6974ee6caccf806805fa8a21d220708bc9283c34d71672'
  'G:\Unity\VRMMO\FrostbiteVR\tools\read_bc2.py'='aa03949aaa09f68534498cbc57f174f91f9843fd6a7ce1b3b86ca4127bffd45b'
  'G:\Unity\VRMMO\FrostbiteVR\tools\capture_reload_state.py'='94db343a4d568c5c6c784aa70b74a508ab936187f63eb95c778359a15294ca88'
  'G:\Unity\VRMMO\FrostbiteVR\tools\capture_bc2_emulation_fixture.py'='da1faef3f08e47674d905bc10ed87195ed1bafeeba9129165f75e2c9b3abdf09'
  'G:\Unity\VRMMO\FrostbiteVR\tools\capture_reload_server.py'='9da3b354b76a43c11f6884ed03d560fdab73fa97bcf5af7351a02266d0cf587c'
  'G:\Unity\VRMMO\bc2vr-recovery\overnight426\knife\SelectedKnifeIdentity426.py'='15c4fea10ee43432955869d242c2bc9ba99a3ccefbaac2b99f691f2a0936bb3c'
  'G:\Unity\VRMMO\bc2vr-recovery\performance436\StartMenuGameR4.ps1'='d9193e4659710b6ef81bd1def3392f9fbdba56fe07e2f0473e4f2100a7003bc7'
  'G:\Unity\VRMMO\bc2vr-recovery\performance436\WindowPlacementR4.ps1'='d166488c41d7df6b86c7782ea6178496683e67651dc3726571edb7270485111a'
  'G:\Unity\VRMMO\BC2VR-Launcher\versions\0.1.12\app\Launcher.Core.ps1'='546a95304d84d0dcfc51d61aba17f1a2fcb0d4a34ecadf00aa6b1235cdd3211e'
  'G:\Unity\VRMMO\BC2VR-Launcher\versions\0.1.12\runtime\Runtime.Core.ps1'='22a766f0f8ea07b289948b0bcaed07e810258a7ff6bd1816672f920ca6aabd33'
  'G:\Unity\VRMMO\bc2vr-recovery\overnight426\release\stage-0.1.8-01\runtime\python\python.exe'='31fe10b4ed82a2f960af0ef9931f46a4b84ff286f8b6ffd9dee098cf4f10f17f'
 }
 foreach($path in $pins.Keys){
  $file=Get-Item -LiteralPath $path -ErrorAction Stop
  if(($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
     (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -cne $pins[$path]){throw "Native diagnostic dependency changed: $path"}
 }
}
