"""Host tier smoke test: the test plan parses. Verifies no requirement."""

import pathlib

import yaml

PLAN = pathlib.Path(__file__).resolve().parents[2] / "testing" / "test-plan.yaml"


def test_plan_is_valid_yaml_with_unique_ids():
    plan = yaml.safe_load(PLAN.read_text())
    ids = [t["id"] for t in plan["tests"]]
    assert len(ids) == len(set(ids)), "duplicate test id"
