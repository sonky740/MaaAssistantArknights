#!/usr/bin/env python3
import importlib.util
import unittest
from pathlib import Path
spec = importlib.util.spec_from_file_location("trace", Path(__file__).resolve().parents[1] / "tools/formation_trace_summary.py")
trace = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trace)
H = "[2026-11-19 15:45:41.566][INF][Px1][Tx2] "
class TraceTest(unittest.TestCase):
    def test_order_redaction_and_filter(self):
        lines = [H + "asst::RoguelikeFormationTaskPlugin::_run | enter\n",
                 H + 'Assistant::append_callback | SubTaskError {"class":"asst::RoguelikeFormationTaskPlugin","details":{},"uuid":"SECRET"}\n',
                 H + 'Assistant::append_callback | SubTaskCompleted {"details":{"task":"QuickFormation","action":"ClickSelf","exec_times":14,"result":{"secret":"SECRET"}}}\n',
                 H + '{"details":{"to_be_recognized":["Sarkaz@Roguelike@FormationConfirm"],"cur_retry":0,"retry_times":20},"uuid":"SECRET"}\n',
                 H.replace("Tx2", "Tx3") + "asst::RoguelikeFormationTaskPlugin::_run | enter\n"]
        output = list(trace.events(lines, "Tx2"))
        self.assertEqual([x["event"] for x in output], ["plugin_attempt_enter", "plugin_error", "task_completed", "confirm_recognition_attempt"])
        self.assertNotIn("SECRET", str(output))
        self.assertEqual(output[-1]["detail"]["retry_times"], 20)
    def test_bad_json_and_unrelated_events(self):
        self.assertEqual(list(trace.events([H + "{broken", H + '{"details":{"to_be_recognized":["Other"]}}'])), [])
if __name__ == "__main__": unittest.main(verbosity=2)
