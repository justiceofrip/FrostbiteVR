"""Synthetic BC2 draft-profile geometry checks without native operations."""
from pathlib import Path
import copy
import sys
import unittest
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import build_spas_reload_candidate as draft


def fixture():
    grip=np.eye(4);grip[3,:3]=[.02,.03,.04];terminal=np.eye(4)
    terminal[:3,:3]=[[0,0,-1],[0,1,0],[1,0,0]];terminal[3,:3]=[1,2,3]
    grasp={'row':5,'captured_ms':100,'terminal_before_transfer':False,'item_from_hand_candidate':np.linalg.inv(grip).reshape(-1).tolist(),
           'observed_center_frame_in_wrist':grip.reshape(-1).tolist(),'joint_surface_probes':{}}
    surface={'row':8,'last_observed_motion':{'direction':[1,0,0]},'nearest_receiver_surface':{},'coplanar_surface_patch':{}}
    contacts={'schema':'fvr.bc2.reload_contact_candidates','groups':[{'actor':10,'weapon':20,'space':1,'best_authored_grasp_observation':grasp,'receiver_surface_observations':[surface]}]}
    observations={'schema':'fvr.bc2.reload_shell_observations','asset_name':'SPAS12_sp','groups':[{'actor':10,'weapon':20,'space':1,
        'mapped_asset_bounds_in_shell_bone':{'axis_length_m':.06},'observations':[{'row':8,'shell_center_in_weapon':terminal.reshape(-1).tolist()}],
        'native_transfer_neighbors':[{'wrapper_offset':60,'tick_ms':200,'previous_visible_pose':{'row':8,'relative_to_transfer_ms':-100}}]}]}
    return contacts,observations


class SpasDraftTests(unittest.TestCase):
    def test_grasp_reconstruction_and_complete_insertion_stroke(self):
        contacts,observations=fixture();result=draft.build(contacts,observations);profile=result['portable_profile_candidate']
        grasp=np.array(profile['itemFromHand']).reshape(4,4);entry=np.array(profile['weaponFromEntry']).reshape(4,4);tip=np.array(profile['itemFromInsertion']).reshape(4,4)
        observed=np.array(observations['groups'][0]['observations'][0]['shell_center_in_weapon']).reshape(4,4)
        raw=np.linalg.inv(grasp)@(grasp@observed)
        self.assertTrue(np.allclose(raw,observed))
        state=tip@raw@np.linalg.inv(entry);self.assertTrue(np.allclose(state[3,:3],[0,0,.05]))
        start=observed.copy();start[3,:3]-=.05*entry[2,:3]
        self.assertTrue(np.allclose(tip@start@np.linalg.inv(entry),np.eye(4)))
        self.assertFalse(result['enabled']);self.assertFalse(result['native_written'])
        self.assertFalse(result['constructed_geometry']['actual_open_aperture'])
    def test_source_inputs_are_not_modified(self):
        contacts,observations=fixture();original=copy.deepcopy((contacts,observations));draft.build(contacts,observations)
        self.assertEqual((contacts,observations),original)
    def test_missing_transfer_and_changed_owner_rejected(self):
        contacts,observations=fixture();observations['groups'][0]['weapon']=999
        with self.assertRaises(ValueError):draft.build(contacts,observations)
        contacts,observations=fixture();observations['groups'][0]['native_transfer_neighbors']=[]
        with self.assertRaises(ValueError):draft.build(contacts,observations)
    def test_bad_grasp_matrix_and_wrong_asset_rejected(self):
        contacts,observations=fixture();contacts['groups'][0]['best_authored_grasp_observation']['item_from_hand_candidate'][0]=2
        with self.assertRaises(ValueError):draft.build(contacts,observations)
        contacts,observations=fixture();observations['asset_name']='Other'
        with self.assertRaises(ValueError):draft.build(contacts,observations)

if __name__=='__main__':unittest.main()
