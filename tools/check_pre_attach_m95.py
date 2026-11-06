"""Join existing fresh read-only client/on-foot and server M95 observations.

This file never opens BC2. The owning setup child brackets captures with the
retained PID/start/path and executes the unchanged bench Observe/Equip gates.
"""
import argparse
import json
from pathlib import Path


def require(condition, message):
    if not condition:
        raise ValueError(message)


def validate(foot, server, native, expected_pid):
    require(foot.get("passed") is True and foot.get("vehicle") is False
            and foot.get("read_only") is True and foot.get("process_writes") is False
            and foot.get("native_calls") is False and foot.get("os_input") is False
            and not foot.get("rejected"), "On-foot client preflight failed")
    require(server.get("passed") is True and server.get("pid") == expected_pid
            and server.get("read_only") is True and server.get("process_writes") is False
            and server.get("native_calls") is False and not server.get("rejected"),
            "Server read-only boundary failed")
    require(native.get("complete") is True and native.get("hooks_removed") is True
            and native.get("code_restored") is True and native.get("pid") == expected_pid
            and native.get("client_server_converged") is True
            and native.get("target_name") == "M95_sp" and native.get("target_variant") == 0
            and native.get("target_path") == "Objects/Weapons/Handheld/BU_sni_M95/SP_sni_M95",
            "Exact selector and drained Equip required")
    require(len(foot["samples"]) == len(server["samples"]) == 2, "Paired observations required")
    a, b = foot["samples"]
    require(a == b and a["asset"] == "M95_sp" and a["actor"] == a["controlled"]
            and a["loaded"] == a["capacity"] == 5 and a["reserve"] >= 5,
            "Full on-foot M95 with reserve >=5 required")
    identity = None
    for sample in server["samples"]:
        owner = sample["client_owner"]
        require(sample["identity_coherent"] is True
                and sample["asset_name"] == "M95_sp"
                and sample["asset_path"] == native["target_path"]
                and sample["weapon_data"] == native["target_data"]
                and owner["actor"] == a["actor"] and owner["player"] == a["player"]
                and owner["inventory"] == a["inventory"] and owner["selected_slot"] == 1
                and owner["selected_weapon"] == a["weapon"], "Current client/server/config owner join differs")
        state = sample["state"]
        require(state["current"] == state["next"] == 2
                and state["loaded"] == 5 and state["reserve"] == a["reserve"],
                "Server/client ammunition or readiness differs")
        current = tuple(sample[k] for k in ("server_player", "server_soldier", "server_item",
                                          "server_firing", "weapon_data", "firing_data", "ammo_address"))
        require(all(isinstance(v, int) and not isinstance(v, bool) and v > 65536 for v in current),
                "Invalid server ownership pointers")
        require(identity is None or current == identity, "Server lifetime changed")
        identity = current
    return {"passed": True, "loaded": 5, "reserve": a["reserve"],
            "read_only_client_pair_and_server_joined": True,
            "atomic_snapshot": False, "native_reload_accepted": False,
            "normal_live_three_copy_rig_owner_input_revalidation_required": True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--pid", type=int, required=True)
    args = parser.parse_args()
    load = lambda name: json.loads((args.directory / name).read_text(encoding="utf-8-sig"))
    result = validate(load("on-foot.json"), load("server.json"), load("equip/native.json"), args.pid)
    output = args.directory / "equipment-join.json"
    with output.open("x", encoding="utf-8") as stream:
        json.dump(result, stream, indent=2)
        stream.write("\n")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
