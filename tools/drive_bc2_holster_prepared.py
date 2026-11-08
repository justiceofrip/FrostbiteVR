"""ROOT-RUN-ONLY successor100: prepare the exact neutral release before Empty.

Retains frozen100 ->94 ->92 ->80 admission and command publication. Preparation
holds only unchanged command bytes; genuine fresh Empty still authorizes release.
The inherited source/install/session binding and all original deadlines remain.
"""
import importlib.util
from pathlib import Path
import sys

sys.dont_write_bytecode = True
FROZEN_PATH = Path(__file__).resolve().parents[1] / 'reports/coordinator-20261102-feedback100/holster-transition/drive_bc2_holster_transition100.py'
_spec = importlib.util.spec_from_file_location('holster_prepared_frozen100', FROZEN_PATH)
frozen = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(frozen)
transport, require = frozen.transport, frozen.require
_snapshot_spec = importlib.util.spec_from_file_location(
    'holster_prepared_snapshot_transport', Path(__file__).with_name('bc2_holster_snapshot_transport.py'))
_snapshot_transport = importlib.util.module_from_spec(_snapshot_spec)
_snapshot_spec.loader.exec_module(_snapshot_transport)


class HolsterDriver(frozen.HolsterDriver):
    def __init__(self, args):
        super().__init__(args)
        self.artifacts.save('holster-preparation-invocation.json', dict(
            driverSha256=transport.digest(Path(__file__).read_bytes()),
            frozenPath=str(FROZEN_PATH), frozenSha256=transport.digest(FROZEN_PATH.read_bytes()),
            nativeAcceptance=False, headsetAcceptance=False))

    def wait_state(self, name):
        if name != 'empty':
            return super().wait_state(name)
        ack = self.stow_ack
        require(self.phase == 'stow_wait' and ack is not None and self.held_right
                and not self.pending_command and not self.unsafe_owner and not self.draw_armed
                and self.command_identity() == ack['identity'] and self.right_pose == ack['pose'],
                'release preparation requires the exact acknowledged stow press')
        # The last press already fixes these bytes; none depend on the as-yet
        # unreceived Empty snapshot. Inherited validation still checks exact
        # source, current input, cohort and owner at the eventual send.
        self.prepare_next_command(ack['lines'][:-1] + ['grip r 0'])
        try:
            return super().wait_state(name)
        except BaseException:
            self.discard_prepared_command()
            raise


HolsterDriver = _snapshot_transport.canonical_holster_snapshot_driver(HolsterDriver, transport)


def main(argv=None, *, observed_binding=None):
    saved = frozen.HolsterDriver
    frozen.HolsterDriver = HolsterDriver
    try:
        return frozen.main(argv, observed_binding=observed_binding)
    finally:
        frozen.HolsterDriver = saved


if __name__ == '__main__':
    raise SystemExit(main())
