import copy,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parents[1]/'tools'))
from preflight_reload_probe import validate

def report(loaded=8,reserve=23,capacity=8):
    state=dict(current=2,next=2,loaded=loaded,reserve=reserve,capacity=capacity,finite_ammo=True)
    s=dict(client_owner={'selected_weapon':123},server_player=1,server_soldier=2,server_item=3,
           server_firing=4,asset_name='SPAS12_sp',identity_coherent=True,branches=[copy.deepcopy(state) for _ in range(3)])
    a,b=copy.deepcopy(s),copy.deepcopy(s);a['monotonic_ns']=10**9;b['monotonic_ns']=10**9+30_000_000
    return dict(read_only=True,native_calls=False,process_writes=False,rejected=[],samples=[a,b])

class PreflightTests(unittest.TestCase):
    def test_explicit_magazine_asset_preserves_cohort_checks(self):
        r=report(29,90,30)
        for s in r['samples']:s['asset_name']='XM8_sp_s';s['asset_path']='exact/xm8';s['weapon_data']=55
        with self.assertRaises(ValueError):validate(r)
        self.assertEqual(validate(r,expected_asset='XM8_sp_s')['loaded'],29)
        r['samples'][1]['asset_path']='other/variant'
        with self.assertRaises(ValueError):validate(r,expected_asset='XM8_sp_s')
        for asset in ('','XM8\x00_sp_s',None):
            with self.assertRaises(ValueError):validate(r,expected_asset=asset)
    def test_supported_modes(self):
        self.assertEqual(validate(report(),True,True)['capacity'],8)
        validate(report(6),False,True);validate(report(7),False,False)
        validate(report(10,23,12),False,True) # effective capacity, not authored 4
    def test_wrong_asset_owner_and_branch(self):
        for key in ('asset_name','server_item','client_owner'):
            r=report();r['samples'][1][key]='wrong'
            with self.assertRaises(ValueError):validate(r,True,True)
        r=report();r['samples'][0]['branches'][1]['loaded']=7
        with self.assertRaises(ValueError):validate(r,True,True)
    def test_wrong_counts(self):
        for loaded,reserve,prepare,request in [(8,0,True,True),(7,23,True,True),(7,23,False,True),(6,1,False,True),(8,23,False,False)]:
            with self.assertRaises(ValueError):validate(report(loaded,reserve),prepare,request)
    def test_stale_busy_infinite_and_counterfeit(self):
        for mode in range(5):
            r=report()
            if mode==0:r['samples'][1]['monotonic_ns']+=10**9
            if mode==1:r['samples'][0]['branches'][0]['current']=11
            if mode==2:r['samples'][0]['branches'][0]['finite_ammo']=False
            if mode==3:r['samples'][0]['branches'][0]['loaded']=True
            if mode==4:r['process_writes']=True
            with self.assertRaises(ValueError):validate(r,True,True)

if __name__=='__main__':unittest.main()
