"""Read-only original-x86 Commit callback evidence for authored bolt configs.

Uses the bolt machine fixture's synthetic object and empty listeners. Callback
shapes and conservation are measured; native owner/cohort/shot admission is not.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import unicorn
from probe_bc2_bolt_machine import Image, bind, setup, snapshot, value, EXPECTED_EXE


def callback_case(im, binding, row, dt, freeze):
    em, context, ctx, primary, config = setup(im, binding, row)
    commit = 0x2dd6a0
    raw = im.read(commit, 163)
    h = 14695981039346656037
    for b in raw:
        h = ((h ^ b) * 1099511628211) & 0xffffffffffffffff
    if h != 0x3cae7f07de53d0f7:
        raise ValueError('Original Commit full-function proof differs')
    update = binding['cycle']['Update']['rva']
    pending = []
    commits = []
    updates = []
    frame = 0

    def intercept(vm, pc, size, user):
        sp = vm.reg_read(em.x86.UC_X86_REG_ESP)
        if pending and (pc, sp) == pending[-1][:2]:
            _, _, before, saved_context = pending.pop()
            after = snapshot(em)
            if bytes(vm.mem_read(context, 0x30)) != saved_context:
                raise ValueError('Commit changed original context')
            if before['state'] == before['next'] or after['previous'] != before['state'] or after['state'] != before['next'] or after['next'] != before['next']:
                raise ValueError('Commit state transition differs')
            if any(before[k] != after[k] for k in ('timer', 'loaded', 'reserve')):
                raise ValueError('Commit changed timer or ammunition')
            commits.append(dict(frame=frame, before=before, after=after, exact_context=True))
        if pc != im.base + commit:
            return
        ret, argument = struct.unpack('<II', vm.mem_read(sp, 8))
        if pending or vm.reg_read(em.x86.UC_X86_REG_ECX) != em.object or argument != context or ret != im.base + update + 0x156:
            raise ValueError('Commit is not the direct original Update child')
        pending.append((ret, sp + 8, snapshot(em), bytes(vm.mem_read(context, 0x30))))

    em.vm.hook_add(unicorn.UC_HOOK_CODE, intercept)
    held = 0
    initial = snapshot(em)
    for frame in range(300):
        before = snapshot(em)
        do_hold = freeze and before['state'] == 8 and before['next'] == 1 and frame < 60
        struct.pack_into('<f', ctx, 0x18, dt)
        em.vm.mem_write(context, bytes(ctx))
        if do_hold:
            em.vm.mem_write(context + 0x18, bytes(4))
        em.invoke(update, em.object, context, 0)
        if pending:
            raise ValueError('Original Commit failed to return to original parent')
        if do_hold:
            if bytes(em.vm.mem_read(context + 0x18, 4)) != bytes(4):
                raise ValueError('Original held delta changed')
            em.vm.mem_write(context + 0x18, bytes(ctx[0x18:0x1c]))
            held += 1
        after = snapshot(em)
        if do_hold and before != after:
            raise ValueError('Held Update changed its observed boundary')
        actual = bytes(em.vm.mem_read(context, 0x30))
        if actual[:0x14] != ctx[:0x14] or actual[0x18:] != ctx[0x18:]:
            raise ValueError('Update context changed outside native scratch word')
        if bytes(em.vm.mem_read(primary, len(config))) != config:
            raise ValueError('Original configuration changed')
        if (after['loaded'], after['reserve']) != (initial['loaded'], initial['reserve']):
            raise ValueError('Cycle changed ammunition')
        if before['state'] != after['state'] or do_hold and held == 1:
            updates.append(dict(frame=frame, held=do_hold, before=before, after=after))
        if after['state'] == after['next'] == 2:
            break
    else:
        raise ValueError('Original cycle never reached idle')
    transitions = [(r['before']['state'], r['after']['state']) for r in commits]
    if transitions != [(7, 8), (8, 1), (1, 2)] or freeze and held != 59:
        raise ValueError('Measured callback tail differs')
    return dict(asset=row['native_name'], configuration=row['resource'], configuration_sha256=row['resource_sha256'],
                delta=dt, freeze=freeze, held_updates=held, commits=commits, updates=updates,
                direct_original_update_children=True, unchanged_ammunition=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe', type=Path, required=True)
    p.add_argument('--configurations', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    if args.output.exists():
        p.error('Preserve existing evidence; choose a new output')
    doc = json.loads(args.configurations.read_text(encoding='utf-8-sig'))
    rows = [r for r in doc['configurations']['resolved_weapons'] if r['weapon_class'] == 'wcSniper' and value(r, 'FireLogic.FireLogicType') == 'fltSingleFireWithBoltAction']
    im = Image(args.exe)
    binding = bind(im)
    cases = [callback_case(im, binding, row, dt, freeze) for row in rows for dt in (1/60, .0596221, .1) for freeze in (False, True)]
    if len(rows) != 17 or len(cases) != 102:
        raise ValueError('Expected exact reviewed configuration set')
    result = dict(schema='fvr.bc2.native_cycle_callback_emulation.v1', passed=True,
                  executable_sha256=EXPECTED_EXE, configuration_input_sha256=hashlib.sha256(args.configurations.read_bytes()).hexdigest(),
                  native_machine_code=True, synthetic_object=True, empty_listeners=True,
                  game_process_opened=False, native_admitted=False, actual_shot_verified=False,
                  native_owner_and_scheduler_emulated=False, gameplay_verified=False,
                  cases=cases)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(passed=True, cases=len(cases), commits=sum(len(c['commits']) for c in cases), native_admitted=False, output=str(args.output))))


if __name__ == '__main__':
    main()
