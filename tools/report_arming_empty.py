"""Read-only retained paired-Step coverage; never standalone native acceptance."""
import argparse,json
from pathlib import Path
from report_empty_step import check_row

def analyze(document):
    g=document.get("gameplay",{});fixture=g.get("arming_empty_probe",{});journal=g.get("reload_flow",{}).get("empty_step_diagnostic",{})
    result={"status":"inconclusive","native_verified":False,"scope":"retained_paired_arming_step_coverage",
            "required_external_checks":["pinned source/DLL/process","postflight all-three zero loaded/reserve conservation","normal explicit Reload4/5 positive control"],"groups":[],"invalid_rows":0}
    if (fixture.get("phase")!=4 or fixture.get("failure")!=0 or not fixture.get("withheld_reload_samples",0)
        or not fixture.get("arming_policy_samples",0) or not journal.get("drained")):
        result["reason"]="fixture incomplete or no actual Arming/withheld input";return result
    owner=fixture.get("owner");start=fixture.get("start_ns",0);end=fixture.get("cancel_ns",0);reserve=fixture.get("original_reserve")
    if not isinstance(owner,list) or len(owner)!=7 or not 0<start<end or not isinstance(reserve,int) or reserve<=0:
        result["reason"]="missing exact operation owner/time/count boundary";return result
    # This opt-in fixture admits only the existing SpasTube binding (family0/profile0).
    # Agreement among branches is not proof of that scoped native binding.
    groups={};explicit=0
    for row in journal.get("rows",[]):
        if check_row(row):result["invalid_rows"]+=1;continue
        if row["owner"]!=owner or not start<=row["now_ns"]<=end or row["phase"]!=2:continue
        context=row.get("context");before=row.get("before");after=row.get("after")
        if not context or not before or not after:continue
        if context["input_flags"] in (4,5):
            if not row["requested"] and not row["applied"]:explicit+=1
            continue
        if (context["input_flags"] not in (0,1) or not 0<context["delta"]<=.05 or context["reload_multiplier"]!=1 or (context["flags_24_28"][0]!=1 or context["flags_24_28"][2]!=0 or context["flags_24_28"][4]!=0)
            or not row.get("family_known") or row.get("family")!=0 or row.get("profile")!=0 or row.get("lease_invalid_last_known") or row["branch"] not in (0,1,2)
            or not row["owner_revision"] or not row["input_sequence"] or not 0<row["observed_ns"]<=row["now_ns"]<row["deadline_ns"]
            or (row.get("raw_context_known") and row.get("context_input_flags")!=context["input_flags"])
            or not all(row[x] for x in ("requested","applied","restored","owner_retained"))):continue
        if any(state["loaded"]!=0 or state["reserve"]!=reserve or state["current"]!=2 or state["next"]!=2 for state in (before,after)):continue
        if before["firing"]!=after["firing"] or before["flags_a8"]!=after["flags_a8"]:continue
        key=(row["owner_revision"],row["family"],row["profile"]);groups.setdefault(key,{})[row["branch"]]=before["firing"]
    result["explicit_exempt_rows"]=explicit
    result["groups"]=[{"owner_revision":k[0],"family":k[1],"profile":k[2],"branches":sorted(v)} for k,v in groups.items()]
    if any(set(v)=={0,1,2} and len(set(v.values()))==3 for v in groups.values()):result["status"]="paired_arming_step_coverage_observed"
    else:result["reason"]="missing same-owner/revision/family three-branch paired restoration"
    result["journal_loss"]={k:journal.get(k) for k in ("lock_drops","overwritten","salient_dropped")}
    return result

def main():
    parser=argparse.ArgumentParser();parser.add_argument("trace",type=Path);args=parser.parse_args()
    print(json.dumps(analyze(json.loads(args.trace.read_text(encoding="utf-8"))),indent=2))
if __name__=="__main__":main()
