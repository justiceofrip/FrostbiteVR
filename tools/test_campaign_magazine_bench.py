"""Saved-data-only regression; no process access, native actions or headset."""
import copy
import unittest
import bc2_campaign_bench as bench
from preflight_reload_probe import validate
from check_resource_inventory_status import check_campaign_magazine


def preflight(asset='Nambu100', capacity=30, loaded=30, reserve=60):
    state=dict(current=2,next=2,loaded=loaded,reserve=reserve,capacity=capacity,finite_ammo=True)
    row=dict(client_owner={'actor':1,'selected_weapon':2},server_player=3,server_soldier=4,
             server_item=5,server_firing=6,asset_name=asset,asset_path='exact/'+asset,
             weapon_data=7,firing_data=8,ammo_address=9,identity_coherent=True,
             branches=[copy.deepcopy(state) for _ in range(3)],monotonic_ns=100)
    return dict(read_only=True,native_calls=False,process_writes=False,rejected=[],
                samples=[row,{**copy.deepcopy(row),'monotonic_ns':200}])


def completed():
    probe=dict(campaign_magazine=True,magazine_only=True,magazine_postfire=True,support_only=False,
               manual_pump=False,input_only=True,persistent_consumers=True,phase=11,failure=0,completed=True,
               partial_magazine_ready=True,partial_loaded=29,support_reacquired=True,rifle_support_required=True,
               initial_support_claim=5,rifle_support_claim=9,rifle_shot_count=1,rifle_shot_sequence=100,
               first_magazine=dict(actual_consumer_completed=True,phase=12,failure=0,completed_cycles=2))
    return dict(hooks_disabled=True,gameplay=dict(resource_inventory_probe=probe,reload_flow=dict(
        drained=True,in_flight=0,ammo_resource_hands=dict(ordinary_input_build=True,calls=4,completed=4,rejected=0,pending=0))))


class CampaignMagazine(unittest.TestCase):
    def test_full_or_partial_exact_selected_weapon_no_companion_required(self):
        for asset,capacity in [('Nambu100',30),('SCAR_sp',30),('XM8_sp_s',30),('US_rgl_SMG',20)]:
            for loaded in (capacity,capacity-1):
                result=validate(preflight(asset,capacity,loaded),expected_asset=asset,campaign_magazine=True)
                self.assertTrue(result['campaign_magazine']);self.assertFalse(result['native_reload_verified'])

    def test_three_copy_identity_and_finite_capacity_still_required(self):
        cases=[]
        wrong=preflight();wrong['samples'][1]['asset_name']='other';cases.append(wrong)
        wrong=preflight();wrong['samples'][1]['weapon_data']=123;cases.append(wrong)
        wrong=preflight();wrong['samples'][0]['branches'][2]['loaded']=0;cases.append(wrong)
        cases.extend((preflight(loaded=0),preflight(reserve=29)))
        for bad in cases:
            with self.assertRaises(ValueError):validate(bad,expected_asset='Nambu100',campaign_magazine=True)

    def test_original_preflight_modes_keep_existing_semantics(self):
        with self.assertRaises(ValueError):validate(preflight(),expected_asset='Nambu100')
        self.assertTrue(validate(preflight(),prepare_shots=True,expected_asset='Nambu100')['prepare_shots'])
        with self.assertRaises(ValueError):validate(preflight(),prepare_shots=True,expected_asset='Nambu100',campaign_magazine=True)

    def test_status_is_completion_not_native_acceptance(self):
        result=check_campaign_magazine(completed());self.assertTrue(result['runtime_completed'])
        self.assertFalse(result['native_reload_accepted']);self.assertTrue(result['independent_receipt_audit_required'])

    def test_missing_operation_stale_support_or_wrong_composition_reject(self):
        for fault in range(5):
            trace=completed();probe=trace['gameplay']['resource_inventory_probe']
            if fault==0:probe['campaign_magazine']=False
            elif fault==1:probe['rifle_support_claim']=probe['initial_support_claim']
            elif fault==2:trace['gameplay']['reload_flow']['ammo_resource_hands']['completed']=3
            elif fault==3:trace['hooks_disabled']=False
            else:probe['rifle_shot_count']=0
            self.assertFalse(check_campaign_magazine(trace)['runtime_completed'])

    def test_native_profile_serialization_matches_harness_hex_and_null(self):
        rows=bench.decode_provision_variants('{0,0x0ull,0ull,false,{"M95_sp","a"}\n'
             '{1,0xabcull,1ull,true,{"AEK971_sp","b"}\n'
             '{2,0xabcull,0ull,true,{"XM8_sp_s","c"}\n')
        self.assertEqual([(r['descriptor_profile_id'],r['native_profile_id']) for r in rows],
                         [('0',None),('abc','1'),('abc','0')])


if __name__=='__main__':unittest.main()
