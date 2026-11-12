"""ROOT-RUN-ONLY shared holster entry with prepared neutral release.

Tracked sibling modules provide all policy and transport. Preparation holds only
unchanged command bytes; genuine fresh Empty still authorizes release. The exact
source/install/session binding and all original deadlines remain.

For readiness composition, pass (HolsterDriver, core) to
bc2_draw_readiness_driver.make_driver; core exposes the full binding interface.
"""
import importlib.util
from pathlib import Path
import sys

sys.dont_write_bytecode = True
CORE_PATH = Path(__file__).with_name('bc2_holster_core.py')
_spec = importlib.util.spec_from_file_location('bc2_holster_core', CORE_PATH)
core = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(core)
transport, require = core.transport, core.require
_snapshot_spec = importlib.util.spec_from_file_location(
    'holster_prepared_snapshot_transport', Path(__file__).with_name('bc2_holster_snapshot_transport.py'))
_snapshot_transport = importlib.util.module_from_spec(_snapshot_spec)
_snapshot_spec.loader.exec_module(_snapshot_transport)


class HolsterDriver(core.HolsterDriver):
    def __init__(self, args):
        super().__init__(args)
        self.artifacts.save('holster-preparation-invocation.json', dict(
            driverSha256=transport.digest(Path(__file__).read_bytes()),
            corePath=str(CORE_PATH), coreSha256=transport.digest(CORE_PATH.read_bytes()),
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
    return core.main(argv, observed_binding=observed_binding, driver_class=HolsterDriver)


if __name__ == '__main__':
    raise SystemExit(main())
