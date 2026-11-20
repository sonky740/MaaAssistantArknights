#!/usr/bin/env python3
"""Redacted offline event extraction; input logs stay outside the repository.

Usage: python3 tools/formation_trace_summary.py /path/to/log --thread Tx26118
Output is an ordered selection of observed events, not a reconstructed controller
or proof that clicks succeeded. Limit input to a single incident/time window.
"""
import argparse
import json
import re

HEADER = re.compile(r"^\[([^]]+)\]\[[^]]+\]\[([^]]+)\]\[([^]]+)\] ")
SUFFIXES = ("QuickFormation", "FormationConfirm")

def events(lines, thread=None):
    for line in lines:
        h = HEADER.match(line)
        if not h or (thread and h.group(3) != thread):
            continue
        ts, process, tid = h.groups()
        body = line[h.end():]
        kind, detail = None, None
        if "RoguelikeFormationTaskPlugin::_run | enter" in body:
            kind = "plugin_attempt_enter"
        elif "RoguelikeFormationTaskPlugin::_run | leave" in body:
            kind = "plugin_attempt_leave"
        elif "asst::WordOcr [{ text: ," in body:
            kind = "empty_ocr_observed"
        else:
            offset = body.find("{")
            if offset < 0:
                continue
            try:
                obj = json.loads(body[offset:])
            except (ValueError, TypeError):
                continue
            if not isinstance(obj, dict):
                continue
            details = obj.get("details")
            if not isinstance(details, dict):
                continue
            prefix = body[:offset]
            if "SubTaskError" in prefix and "RoguelikeFormationTaskPlugin" in str(obj.get("class", "")):
                kind = "plugin_error"
            elif "SubTaskCompleted" in prefix and details.get("task") in SUFFIXES:
                kind = "task_completed"
                detail = {k: details[k] for k in ("task", "action", "exec_times", "max_times") if k in details}
            else:
                names = details.get("to_be_recognized")
                if isinstance(names, list) and any(isinstance(n, str) and n.endswith("@FormationConfirm") for n in names):
                    kind = "confirm_recognition_attempt"
                    detail = {k: details[k] for k in ("cur_retry", "retry_times") if k in details}
        if kind:
            out = {"time": ts, "process": process, "thread": tid, "event": kind}
            if detail is not None:
                out["detail"] = detail
            yield out

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", help="external log or incident-window excerpt")
    parser.add_argument("--thread", help="exact thread ID, e.g. Tx26118")
    args = parser.parse_args()
    with open(args.log, encoding="utf-8", errors="replace") as source:
        for event in events(source, args.thread):
            print(json.dumps(event, ensure_ascii=False))

if __name__ == "__main__":
    main()
