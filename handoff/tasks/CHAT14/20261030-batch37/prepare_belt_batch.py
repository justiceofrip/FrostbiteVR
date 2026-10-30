"""Exact-content belt batch; cached inputs by default, named-entry sampler with --sample.

No archive search, extraction files, source mutation, native execution, or admission.
All outputs stay beside this script. Run with Python -B to avoid parser bytecode.
"""
from pathlib import Path
import argparse, hashlib, json, math, struct, sys, zlib
sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
WORK = HERE.parents[3]
EVIDENCE = WORK.parents[1]
PLAN = WORK / 'handoff/tasks/CHAT14-16/20261030-familybatch36/input-plan.json'
PHASES = ('Resting', 'PouchDetachSource', 'PouchWithdrawn', 'PouchAttachSource',
          'PouchAttachedFeedUnseated', 'FeedSeatSource', 'FeedSeated')
# These are explicit offline presentation-role selections, not numeric-name inference.
# Every selection is checked against its exact skin weights and actual rig topology.
ROLE_SELECTION = {
    'RU_lmg_PKM': ('jntWpn_12', 'jntWpn_6', 'jntWpnwpnJnt_16'),
    'UL_lmg_FNMinimi': ('jntWpn_3', 'jntWpn_6', 'jntWpnwpnJnt_16'),
    'MEC_lmg_QJY88': ('jntWpn_3', 'jntWpn_6', 'jntWpn_12'),
    'BU_lmg_M60': ('jntWpn_9', 'jntWpn_6', 'jntWpn_12'),
    'BU_lmg_MG3': ('jntWpn_3', 'jntWpn_6', 'jntWpnwpnJnt_16'),
}

def sha(data): return hashlib.sha256(data).hexdigest()
def read(path): return json.loads(path.read_text(encoding='utf-8'))
def write(name, value, compact=False):
    path = HERE / name
    path.write_text(json.dumps(value, indent=None if compact else 2, allow_nan=False) + '\n', encoding='utf-8')
    return {'path': path.relative_to(EVIDENCE).as_posix(), 'sha256': sha(path.read_bytes())}
def ref(source, pointer): return {'source': source, 'pointer': pointer}
def lookup(value, path):
    for token in path.split('/')[1:]:
        token = token.replace('~1','/').replace('~0','~')
        value = value[int(token)] if isinstance(value, list) else value[token]
    return value
def content(record):
    return {**{k: record['source'][s] for k,s in (
        ('meshSha256','mesh_sha256'), ('lodSha256','lod_sha256'),
        ('clipSha256','sha256'), ('referenceClipSha256','reference_clip_sha256'))},
        'skeletonSha256': record['skeleton_sha256'], 'rigFingerprint': record['rig_fingerprint']}

def load():
    plan = read(PLAN); docs = {}
    for name in ('actions','belt_bindings','belt_mechanisms','belt_meshes','m60_frames','m60_geometry','m60_roles'):
        pin = plan['sources'][name]; raw = (EVIDENCE / pin['path']).read_bytes()
        if sha(raw) != pin['sha256']: raise ValueError('Pinned input changed: ' + name)
        docs[name] = json.loads(raw)
    groups = {}; configurations = []
    for item in plan['configurations']:
        if item['kind'] != 'feed': continue
        record = lookup(docs['actions'],item['authored_anchor']['pointer'])
        assert record['weapon_configuration']['resource'] == item['configuration_resource']
        assert record['weapon_configuration']['instance_guid'] == item['configuration_guid']
        assert record['weapon_configuration']['sha256'] == item['configuration_sha256']
        pin = content(record); key = sha(json.dumps(pin, sort_keys=True).encode())
        bindings = [(i,b) for i,b in enumerate(docs['belt_bindings']['profiles'])
                    if b['weapon']['resource'] == item['configuration_resource']]
        assert len(bindings) == 1
        bi, binding = bindings[0]
        assert binding['rig_fingerprint'] == record['rig_fingerprint']
        assert binding['skeleton_sha256'] == record['skeleton_sha256']
        assert binding['animation_sha256'] == record['source']['reference_clip_sha256']
        bound_mesh=next(m for m in binding['meshes'] if m['configured_mesh_path']==record['configured_mesh'])
        assert bound_mesh['geometry']['mesh_sha256']==record['source']['mesh_sha256']
        assert bound_mesh['geometry']['lods'][0]['data_sha256']==record['source']['lod_sha256']
        if key in groups:
            assert groups[key]['record']['parts'] == record['parts']
            assert groups[key]['binding']['rig_topology'] == binding['rig_topology']
        else:
            groups[key] = dict(record=record,binding=binding,content=pin,
                               anchor=item['authored_anchor'],binding_ref=ref('belt_bindings',f'/profiles/{bi}'))
        configurations.append(dict(**item, content_group=key))
    assert len(configurations) == 24 and len(groups) == 5
    return plan, docs, groups, configurations

def roles(group):
    record, binding = group['record'], group['binding']
    family = record['configured_mesh'].split('/')[-2]
    cover, pouch, chainroot = ROLE_SELECTION[family]
    topology = binding['rig_topology']; parts = {p['bone']:p for p in record['parts']}
    def bone(name, parent=True):
        i = next(i for i,p in enumerate(topology) if p['name']==name)
        row = dict(bone=name,index=i)
        if parent: row.update(parent=topology[topology[i]['parent_index']]['name'],parentIndex=topology[i]['parent_index'])
        return row
    chain = [chainroot]
    while True:
        children = [p['bone'] for p in record['parts'] if p['parent']==chain[-1]
                    and any('ammo' in s['name'].lower() for s in p['sections'])]
        if not children: break
        assert len(children)==1
        chain += children
    assert 1 <= len(chain) <= 8
    assert all(parts[n]['independent_rigid_geometry'] for n in [cover,pouch,*chain])
    assert parts[cover]['motion']['kind']=='hinge_candidate'
    return dict(root=bone(binding['weapon_bone'],False),cover=bone(cover),pouch=bone(pouch),
                feed_chain=[bone(n) for n in chain])

def sample(groups, game):
    sys.path.insert(0,str(WORK/'tools'))
    from bc2_magazine_contact_batch import ArchivePool
    from bc2_weapon_animation_pipeline import Skeleton, Clip, inverse_rigid, multiply
    from bc2_mesh_geometry import metadata
    import bc2_authored_magazine_geometry as mag
    import bc2_authored_mechanism_geometry as mech
    common = next(iter(groups.values()))['binding']['skeleton_source']
    pool = ArchivePool(game, common['archive'], common['archive_index_sha256'])
    archives = {}; blobs = {}; output = {}
    def entry(source, name, digest):
        a = source['archive']; index = source['archive_index_sha256']
        if a not in archives:
            archives[a] = pool(game/a)
            assert archives[a].index_sha256 == index, 'Archive index changed: ' + a
        assert archives[a].index_sha256 == index
        if (a,name) not in blobs: blobs[a,name] = archives[a].read_selected([name])[name]
        value = blobs[a,name]
        assert sha(value)==digest, 'Exact resource changed: ' + name
        return value
    for key,group in groups.items():
        r,b = group['record'],group['binding']; source=r['source']; role=roles(group)
        sk = Skeleton(entry(b['skeleton_source'],b['animation_chain']['skeleton_resource'],r['skeleton_sha256']))
        clip = Clip(entry(source,source['resource'],source['sha256']))
        assert not clip.unsupported and sk.fingerprint==r['rig_fingerprint']
        mesh = r['configured_mesh']+'.res'; lod = r['configured_mesh']+'_lod0_data.res'
        mesh_source = source['mesh_source']
        definition, = metadata(entry(mesh_source,mesh,source['mesh_sha256']))
        parts = mag.rigid_parts(entry(mesh_source,lod,source['lod_sha256']),definition,sk,strict_assembly_skin=True)
        names=[role['cover']['bone'],role['pouch']['bone'],*[x['bone'] for x in role['feed_chain']]]
        auxiliary=['jntWpn_11','jntWpn_13'] if '/RU_lmg_PKM/' in r['configured_mesh'] else []
        allnames=sorted(set(names+auxiliary+['LeftHand',*mag.FINGERS]))
        times=[min(i/30,clip.duration) for i in range(math.ceil(clip.duration*30)+1)]
        samples=[clip.evaluate(sk,t,allnames) for t in times]
        rows=[]
        for s in samples:
            rel=s['weapon_relative']; contacts={}
            for name in (names[0],names[1],names[-1]):
                inv=inverse_rigid(rel[name]); points=parts[name]['points']
                fingers={n:mag.point(rel[n][12:15],inv) for n in mag.FINGERS}
                gap,joint,point,vertex=min((math.dist(p,v),n,p,v) for n,p in fingers.items() for v in points)
                contacts[name]=dict(part_from_wrist=multiply(rel['LeftHand'],inv),joint=joint,
                    joint_part=point,nearest_vertex_part=vertex,mesh_gap_m=gap,sustained_grasp_verified=False)
            cover_delta=multiply(rel[names[0]],inverse_rigid(samples[0]['weapon_relative'][names[0]]))
            _, angle=mech.axis_angle(cover_delta)
            rows.append(dict(time_s=s['time_seconds'],transforms={n:rel[n] for n in names+auxiliary},
                weapon_from_wrist=rel['LeftHand'],contacts=contacts,cover_angle_rad=angle,
                pouch_baseline_distance_m=math.dist(rel[names[1]][12:15],samples[0]['weapon_relative'][names[1]][12:15]),
                terminal_baseline_distance_m=math.dist(rel[names[-1]][12:15],samples[0]['weapon_relative'][names[-1]][12:15])))
        output[key]=dict(content=group['content'],roles=role,sample_hz=30,duration_s=clip.duration,
            frames=rows,source=source,limits=['Authored nearest vertex proximity is not a controller grasp or native contact.',
                'Simultaneous transforms are decoder samples, not native stage markers.'])
        # Derived point-cloud diagnostic: four own-clip extrema, two views each.
        picked=[0,next(i for i,s in enumerate(rows) if s['cover_angle_rad']>=max(x['cover_angle_rad'] for x in rows)*.98),
                max(range(len(rows)),key=lambda i:rows[i]['pouch_baseline_distance_m']),
                max(range(len(rows)),key=lambda i:rows[i]['terminal_baseline_distance_m'])]
        width,height=1600,800; pixels=bytearray((19,23,31)*(width*height))
        colors={names[0]:(255,220,50),names[1]:(240,80,210),**{n:c for n,c in zip(names[2:],
                [(255,100,30),(255,155,45),(140,220,60),(40,220,130),(40,150,255),(150,80,240),(240,80,80),(240,240,240)])}}
        for col,at in enumerate(picked):
            t=rows[at]['time_s']; rel={}
            for name in parts:
                try: rel[name]=clip.evaluate(sk,t,[name])['weapon_relative'][name]
                except (ValueError,KeyError): pass
            points=[(mag.point(p,rel[n]),colors.get(n,(95,105,120))) for n in rel for p in parts[n]['points']]
            points=[(p,c) for p,c in points if p[2]>-.72 or c!=(95,105,120)]
            for panel,camera in enumerate(((1,.55,.2),(-1,.55,.2))):
                camera=mag.unit(camera); right=mag.unit(mag.cross((0,1,0),camera)); up=mag.cross(camera,right)
                projected=sorted([(mag.dot(p,right),mag.dot(p,up),mag.dot(p,camera),c) for p,c in points],key=lambda x:x[2])
                lo=[min(p[k] for p in projected) for k in (0,1)];hi=[max(p[k] for p in projected) for k in (0,1)]
                scale=min(370/(hi[0]-lo[0]),370/(hi[1]-lo[1]))
                for x,y,z,color in projected:
                    x=int(col*400+200+(x-(hi[0]+lo[0])/2)*scale);y=int(panel*400+200-(y-(hi[1]+lo[1])/2)*scale)
                    for dx,dy in ((0,0),(1,0),(0,1)):
                        if 0<=x+dx<width and 0<=y+dy<height:
                            index=((y+dy)*width+x+dx)*3;pixels[index:index+3]=bytes(color)
        raw=b''.join(b'\0'+pixels[y*width*3:(y+1)*width*3] for y in range(height))
        def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
        png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',width,height,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')
        filename='role-points-'+key[:8]+'.png';(HERE/filename).write_bytes(png)
        output[key]['diagnostic']=dict(file=filename,sha256=sha(png),times_s=[rows[i]['time_s'] for i in picked],
            colors={'yellow':'selected cover','magenta':'selected container','orange_to_green':'actual parent chain'},
            views=['positive X','negative X'],representation='offline projected mesh vertices; no surfaces or native playback')
        print(json.dumps(dict(sampled=r['asset'],frames=len(rows),content_group=key)),flush=True)
    return output

def assemble(plan, docs, groups, configurations, sampled):
    sys.path.insert(0,str(WORK/'tools'))
    from bc2_weapon_animation_pipeline import inverse_rigid, multiply
    import bc2_authored_magazine_geometry as mag
    import bc2_authored_mechanism_geometry as mech
    measurements={}; reviews=[]; ready=[]; companion_ready=[]
    for key,group in groups.items():
        s=sampled['groups'][key]; rows=s['frames']; role=roles(group)
        assert s['content']==group['content'] and s['roles']==role
        family=group['record']['configured_mesh'].split('/')[-2]
        cover,pouch,chain=role['cover']['bone'],role['pouch']['bone'],[x['bone'] for x in role['feed_chain']]
        peak=max(range(len(rows)),key=lambda i:rows[i]['pouch_baseline_distance_m'])
        first=next(i for i in range(peak) if rows[i]['pouch_baseline_distance_m']>.03)
        detach=max(i for i in range(first) if rows[i]['pouch_baseline_distance_m']<.003)
        attached=next(i for i in range(peak,len(rows)) if rows[i]['pouch_baseline_distance_m']<.003)
        maximum=max(x['cover_angle_rad'] for x in rows)
        opened=next(i for i,r in enumerate(rows) if r['cover_angle_rad']>maximum*.97)
        closing=next(i for i in range(attached,len(rows)) if rows[i]['cover_angle_rad']<maximum*.8)
        seated=next(i for i in range(attached+1,closing-4)
                    if max(r['terminal_baseline_distance_m'] for r in rows[i:i+5])<.005)
        contact=min(range(attached,seated),key=lambda i:rows[i]['contacts'][chain[-1]]['mesh_gap_m'])
        indices=[0,detach,peak,peak,attached,contact,seated]
        poses=[];composition_error=0.
        for index,label in zip(indices,PHASES):
            row=rows[index];tf=row['transforms']
            local=[multiply(tf[c],inverse_rigid(tf[p])) for p,c in zip(chain,chain[1:])]
            compose=tf[chain[0]]
            for frame,bone in zip(local,chain[1:]):
                compose=multiply(frame,compose)
                composition_error=max(composition_error,max(abs(a-b) for a,b in zip(compose,tf[bone])))
            poses.append(dict(authoredSeconds=row['time_s'],weaponFromPouch=tf[pouch],weaponFromFeedRoot=tf[chain[0]],
                parentFromChainChild=local,sourceFromWrist=row['contacts'][chain[-1] if label=='FeedSeatSource' else pouch]['part_from_wrist']))
        assert composition_error<1e-9
        # A settled open frame is selected within this clip's own open hold.
        hold=sorted(rows[i]['cover_angle_rad'] for i in range(opened,detach+1));angle=hold[len(hold)//2]
        open_index=min(range(opened,detach+1),key=lambda i:abs(rows[i]['cover_angle_rad']-angle))
        contact_index=min(range(opened+1),key=lambda i:rows[i]['contacts'][cover]['mesh_gap_m'])
        base=rows[0]['transforms'][cover];open_tf=rows[open_index]['transforms'][cover]
        axis,travel=mech.axis_angle(multiply(open_tf,inverse_rigid(base)))
        axis=mag.unit([sum(axis[j]*base[j*4+i] for j in range(3)) for i in range(3)])
        right=mag.unit(mag.cross((0,1,0),axis));up=mag.cross(axis,right)
        hinge=[*right,0.,*up,0.,*axis,0.,*base[12:15],1.]
        grip=rows[contact_index]['contacts'][cover]['part_from_wrist']
        geometry={k:v for k,v in group['content'].items() if k in ('meshSha256','lodSha256','skeletonSha256','clipSha256')}
        geometry.update(weaponFromHinge=hinge,hingeFromOpeningContact=multiply(multiply(grip,base),inverse_rigid(hinge)),
            hingeFromClosingContact=multiply(multiply(grip,open_tf),inverse_rigid(hinge)),
            weaponFromCoverReference=base,coverTravelRadians=travel,poses=poses)
        phase_basis='Own 30Hz clip: last <3mm pouch baseline sample before outward travel; maximum pouch displacement; first <3mm return; closest terminal contact before five consecutive <5mm terminal baseline samples. Open cover uses median hold angle. These thresholds select mod presentation endpoints, not authored native stages.'
        if family=='BU_lmg_M60':
            proof=docs['m60_frames']; resources=set(proof['resources'].values())
            assert all(geometry[n] in resources for n in ('meshSha256','lodSha256','skeletonSha256','clipSha256'))
            geometry.update(weaponFromHinge=proof['cover']['weapon_from_motion'],
                hingeFromOpeningContact=proof['cover']['motion_from_opening_contact'],
                hingeFromClosingContact=proof['cover']['motion_from_closing_contact'],coverTravelRadians=proof['cover']['travel_rad'],
                weaponFromCoverReference=next(p['baseline_weapon_from_part'] for p in docs['m60_geometry']['parts'] if p['bone']==cover),
                poses=[dict(authoredSeconds=p['time_s'],weaponFromPouch=p['weapon_from_pouch'],weaponFromFeedRoot=p['weapon_from_feed_root'],
                    parentFromChainChild=p['parent_from_chain_child'],sourceFromWrist=p['source_from_wrist']) for p in proof['poses']])
            indices=[round(p['authoredSeconds']*30) for p in geometry['poses']]
            contact_index=round(proof['cover']['grip_source_s']*30);open_index=45
            phase_basis='Preserved the separately reviewed M60 seven-pose selection only after exact mesh/lod/skeleton/clip hash equality; every exact configuration retains its own anchor. No M60 measurement is assigned to a different content group.'
        seat_distance=math.dist(rows[indices[4]]['transforms'][chain[-1]][12:15],rows[indices[6]]['transforms'][chain[-1]][12:15])
        detach_distance=math.dist(geometry['poses'][1]['weaponFromPouch'][12:15],geometry['poses'][2]['weaponFromPouch'][12:15])
        # New batch interaction design, geometry-scaled per content group; not an authored calibration.
        tolerance=round(min(.018,max(.004,seat_distance*.12)),6)
        contact_radius=round(min(.09,max(.025,max(rows[i]['contacts'][n]['mesh_gap_m']
            for i,n in ((indices[1],pouch),(indices[2],pouch),(indices[5],chain[-1]),(contact_index,cover)))+.015)),6)
        policy=dict(completeFraction=.96,releaseFraction=.12,pathToleranceMeters=tolerance,
            orientationToleranceRadians=round(min(.24,geometry['coverTravelRadians']*.08),6),
            maxStepMeters=round(min(.07,detach_distance*.2),6),maxStepRadians=round(min(.45,geometry['coverTravelRadians']*.18),6),
            endpointDwellNs=80000000,maximumGestureNs=10000000000,maxInputGapNs=80000000)
        mechanism=dict(revision=1,maxAgeNs=80000000,ammunitionKind=1,cover=True,latch=False,container=True,charge=False,
            containerNeedsOpen=True,feedNeedsOpen=True,detachNeedsUnseated=False,sameContainerReplenishment=True,coupledContainerDetach=True,
            contacts={name:dict(role=i,radiusMeters=contact_radius,maxAngleRadians=.55) for i,name in enumerate(('Cover','Container','Feed'),1)})
        missing=[]; auxiliary=[]; companions=[]
        if family=='RU_lmg_PKM':
            for name in ('jntWpn_11','jntWpn_13'):
                topo=group['binding']['rig_topology'];bi=next(i for i,p in enumerate(topo) if p['name']==name)
                reference=rows[0]['transforms'][name]; relative=multiply(reference,inverse_rigid(base))
                errors=[mag.grasp_separation(multiply(relative,r['transforms'][cover]),r['transforms'][name]) for r in rows]
                maximum_error=[max(e[i] for e in errors) for i in (0,1)]
                candidate=dict(bone=name,index=bi,parent=topo[topo[bi]['parent_index']]['name'],parentIndex=topo[bi]['parent_index'],weaponFromReference=reference)
                rigid=maximum_error[0]<1e-5 and maximum_error[1]<1e-4
                auxiliary.append(dict(candidate=candidate,rigid_companion=rigid,max_full_clip_translation_error_m=maximum_error[0],
                    max_full_clip_rotation_error_rad=maximum_error[1],sample_count=len(rows),phase_errors=[dict(time_s=rows[i]['time_s'],
                    translation_m=errors[i][0],rotation_rad=errors[i][1]) for i in indices],
                    contract='reference * inverse(primary cover reference) * current primary cover; row-vector transforms',
                    disposition='Independent root sibling requires explicit cover companion publication.' if name=='jntWpn_11' else
                    'Actual descendant retained by native subtree inheritance; independent local motion remains unassigned, not a rigid companion or inferred latch.'))
                if name=='jntWpn_11':
                    assert rigid, 'PKM sibling cover is not rigidly co-moving; do not enroll'
                    companions.append(candidate)
            missing.append(dict(field='geometry.coverCompanions',reason='Prepared exact sibling11 role/reference and full-clip co-motion proof. Await exporter/runtime optional cover-companion contract; descendant13 retains its own local motion and is not listed as rigid.'))
        measurements[key]=dict(content=group['content'],presentation_family=family,roles=role,geometry=geometry,
            phase_review=dict(labels=list(PHASES),sample_indices=indices,basis=phase_basis,cover_contact_index=contact_index,
                cover_open_index=open_index,pose_times_s=[p['authoredSeconds'] for p in geometry['poses']],
                contacts=[dict(label=label,bone=chain[-1] if label=='FeedSeatSource' else pouch,
                    measurement=rows[i]['contacts'][chain[-1] if label=='FeedSeatSource' else pouch]) for label,i in zip(PHASES,indices)],
                cover_contact=rows[contact_index]['contacts'][cover],composition_max_error=composition_error),
            role_review=dict(reviewer='Codex CHAT14 offline mesh and authored motion review',
                evidence=s['diagnostic'],basis='Two projected views of exact weighted vertices at rest, opened cover and detached container show the yellow hinged panel over the feed opening, magenta container beneath it, and repeated cartridge geometry in the separately colored parent chain. Verified actual topology/independent skinning and exact source hashes. M60 additionally retains prior visual review.',
                full_single_cover_contract_supported=not missing,physical_reviewed=False,native_reviewed=False),
            interaction_policy=policy,mechanism=mechanism,missing_prerequisites=missing,
            cover_companion_candidates=companions,cover_auxiliary_evidence=auxiliary,
            policy_review=dict(reviewed_for_disabled_candidate=True,phase_endpoints=phase_basis,
                tolerances='New mod policy: path tolerance=max(4mm,min(18mm,12% of this group seat displacement)); contact radius=max(25mm,min(90mm,largest selected mesh gap+15mm)); angular/step tolerances scale from this group cover/detach travel. Fractions,80ms dwell/gap,10s gesture and0.55rad contact orientation are explicit interaction design, never authored calibration.',
                continuity='Attach begins from retained detach destination; FeedSeatSource supplies a measured terminal-local wrist reference transported to retained unseated source. Retain actual pickup offset and parent-local articulation; never use a rigid belt substitute.',
                cover_contact='Measured same-frame fingertip/mesh proximity; local wrist frame transported to each cover endpoint by mod policy. Sustained grasp and controller calibration are unproved.',
                unsupported_operations=['ReleaseLatch','EngageLatch','CycleAction','independent WithdrawFeed','separate replacement-container mode']))
    for i,item in enumerate(configurations):
        group=groups[item['content_group']]; m=measurements[item['content_group']];r=group['record']
        missing=list(m['missing_prerequisites'])
        common_missing=['exact_native_descriptor_review','exact_profile_digest_mechanism_admission',
            'physical_controller_contact_and_endpoint_acceptance','actual_game_native_operation_and_publication','headset_acceptance']
        review=dict(configuration={'resource':item['configuration_resource'],'instance_guid':item['configuration_guid'],'sha256':item['configuration_sha256']},
            asset=item['asset'],configured_mesh=r['configured_mesh'],rig_fingerprint=r['rig_fingerprint'],
            content_group=item['content_group'],descriptor_key=item['descriptor_key'],descriptor_digest=item['descriptor_digest'],
            authored_anchor=item['authored_anchor'],roles_reviewed=True,policy_reviewed=True,physical_reviewed=False,native_reviewed=False,
            exporter_candidate_ready=not missing,missing_geometry_fields=missing,missing_enablement_prerequisites=common_missing,
            role_review='Reviewed exact resource join plus the matching content-group mesh/topology/contact/phase evidence; shared numbers require all five resource hashes and the rig fingerprint to agree.')
        reviews.append(review)
        anchor=item['authored_anchor']['pointer'];gpath='/groups/'+item['content_group']
        annotation=dict(kind='feed',descriptor_key=item['descriptor_key'],descriptor_digest=item['descriptor_digest'],
            native_class='AutomaticMagazineZeroBoltTransfer',configuration=ref('actions',anchor+'/weapon_configuration'),
            asset=ref('actions',anchor+'/asset'),meshPath=ref('actions',anchor+'/configured_mesh'),rigFingerprint=ref('actions',anchor+'/rig_fingerprint'),
            roles=ref('belt37',gpath+'/roles'),geometry=ref('belt37',gpath+'/geometry'),geometry_evidence=item['authored_anchor'],
            role_review=ref('belt37',f'/configurations/{i}'),reviewer='Codex CHAT14 offline geometry and mod-policy review',
            roles_reviewed=True,policy_reviewed=True,interaction_policy=m['interaction_policy'],mechanism=m['mechanism'],enabled=False)
        if missing:
            geometry={name:ref('belt37',gpath+'/geometry/'+name) for name in m['geometry']}
            geometry['coverCompanions']=ref('belt37',gpath+'/cover_companion_candidates')
            annotation['geometry']=geometry;companion_ready.append(annotation)
        else:ready.append(annotation)
    pins={name:plan['sources'][name] for name in ('actions','belt_bindings','belt_mechanisms','belt_meshes','m60_frames','m60_geometry','m60_roles')}
    pins['synchronized_samples']={'path':(HERE/'synchronized-samples.json').relative_to(EVIDENCE).as_posix(),'sha256':sha((HERE/'synchronized-samples.json').read_bytes())}
    pin=write('roles-geometry-policy.json',dict(schema='fvr.bc2.belt_family_measured_review.v1',sources=pins,
        evidence_level='offline_exact_hash_pinned_authored_geometry_and_explicit_mod_policy',
        sampler_sha256=sha(Path(__file__).read_bytes()),groups=measurements,configurations=reviews,
        summary=dict(exact_configurations=len(reviews),numeric_content_groups=len(measurements),usable_exporter_rows=len(ready),
            pending_companion_contract_rows=len(companion_ready),data_ready_rows=len(ready)+len(companion_ready),native_enabled=0,headset_accepted=0)))
    write('reviewed-annotations.json',dict(schema='fvr.bc2.mechanism_registry_annotations.v1',
        sources={'actions':plan['sources']['actions'],'belt37':pin},profiles=ready))
    write('companion-contract-annotations.json',dict(schema='fvr.bc2.mechanism_registry_annotations.v1',
        sources={'actions':plan['sources']['actions'],'belt37':pin},profiles=companion_ready))
    print(json.dumps(dict(usable_exporter_rows=len(ready),exact_missing_rows=[r['asset']+':'+r['configuration']['resource'] for r in reviews if not r['exporter_candidate_ready']])))

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--sample',action='store_true')
    parser.add_argument('--game',type=Path,default=Path('G:/New folder (2)/Battlefield Bad Company 2'))
    args=parser.parse_args(); plan,docs,groups,configs=load()
    if args.sample:
        data=sample(groups,args.game)
        write('synchronized-samples.json',dict(schema='fvr.bc2.belt_synchronized_samples.v1',
            evidence_level='offline_exact_hash_pinned_named_resource_decode',sources=plan['sources'],
            reader_sha256=sha(Path(__file__).read_bytes()),groups=data),compact=True)
    assemble(plan,docs,groups,configs,read(HERE/'synchronized-samples.json'))
    print(json.dumps(dict(configurations=len(configs),content_groups=len(groups),output=str(HERE))))

if __name__=='__main__':main()
