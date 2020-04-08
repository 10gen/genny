import json
import unittest
from typing import NamedTuple, Dict, List, Tuple, Optional
from unittest.mock import MagicMock

from shrub.config import Configuration

from gennylib.auto_tasks import (
    CurrentBuildInfo,
    CLIOperation,
    FileLister,
    Repo,
    ConfigWriter,
    YamlReader,
)


class MockFile(NamedTuple):
    base_name: str
    modified: bool
    yaml_conts: Optional[dict]

    @property
    def repo_path(self):
        return "src/workloads/" + self.base_name


class MockReader(YamlReader):
    def __init__(self, files: List[MockFile]):
        self.files = files

    def load(self, path: str):
        found = self._find_file(path)
        if not found:
            raise Exception(f"Couldn't find {path} in {[self.files]}")
        return found.yaml_conts

    def exists(self, path: str) -> bool:
        found = self._find_file(path)
        return found is not None

    def _find_file(self, path: str):
        found = [f for f in self.files if f.repo_path == path or f.base_name == path]
        if found:
            return found[0]
        return None


def helper(files: List[MockFile], argv: List[str]) -> Configuration:
    # Create "dumb" mocks.
    lister: FileLister = MagicMock(name="lister", spec=FileLister, instance=True)
    reader: YamlReader = MockReader(files)

    # Make them smarter.
    lister.all_workload_files.return_value = [v.repo_path for v in files]
    lister.modified_workload_files.return_value = [v.repo_path for v in files if v.modified]

    # Put a dummy argv[0] as test sugar
    argv.insert(0, "dummy.py")

    # And send them off into the world.
    build = CurrentBuildInfo(reader)
    op = CLIOperation.parse(argv, reader)
    repo = Repo(lister, reader)
    tasks = repo.tasks(op, build)
    writer = ConfigWriter(op)

    config = writer.write(tasks, write=False)
    return config


class AutoTasksTests(unittest.TestCase):
    def test_all_tasks(self):
        config = helper([MockFile("scale/Foo.yml", False, {})], ["all_tasks"])
        parsed = json.loads(config.to_json())
        self.assertEqual(["foo"], [task["name"] for task in parsed["tasks"]])
        self.assertDictEqual(
            {
                "func": "f_run_dsi_workload",
                "vars": {"test_control": "foo", "auto_workload_path": "scale/Foo.yml"},
            },
            parsed["tasks"][0]["commands"][0],
        )

    def test_variant_tasks(self):
        config = helper(
            [
                MockFile(
                    base_name="expansions.yml",
                    modified=False,
                    yaml_conts={
                        "build_variant": "some-build-variant",
                        "mongodb_setup": "some-setup",
                    },
                ),
                MockFile(
                    base_name="src/Foo.yml",
                    modified=False,
                    yaml_conts={"AutoRun": {"Requires": {"mongodb_setup": ["some-setup"]}}},
                ),
            ],
            ["variant_tasks"],
        )

        parsed = json.loads(config.to_json())
        self.assertEqual(
            parsed, {"buildvariants": [{"name": "some-build-variant", "tasks": [{"name": "foo"}]}]}
        )

    def test_patch_tasks(self):
        config = helper(
            [
                MockFile(
                    base_name="expansions.yml",
                    modified=False,
                    yaml_conts={
                        "build_variant": "some-build-variant",
                        "mongodb_setup": "some-setup",
                    },
                ),
                MockFile(
                    base_name="src/Foo.yml",
                    modified=True,
                    yaml_conts={"AutoRun": {"Requires": {"mongodb_setup": ["some-other-setup"]}}},
                ),
                MockFile(
                    base_name="src/Bar.yml",
                    modified=False,
                    yaml_conts={"AutoRun": {"Requires": {"mongodb_setup": ["some-other-setup"]}}},
                ),
            ],
            ["patch_tasks"],
        )
        parsed = json.loads(config.to_json())
        self.assertEqual(
            parsed,
            {"buildvariants": [{"name": "some-build-variant", "tasks": [{"name": "foo"}]}]},
            "Patch tasks always run for the selected variant "
            "even if their Requires blocks don't align",
        )


class LegacyHappyCaseTests(unittest.TestCase):
    def test_all_tasks(self):
        config = helper(
            [
                MockFile(base_name="src/Foo.yml", modified=True, yaml_conts={}),
                MockFile(base_name="src/Bar.yml", modified=False, yaml_conts={}),
            ],
            ["--generate-all-tasks", "--output", "build/all_tasks.json"],
        )
        parsed = json.loads(config.to_json())
        expected = {
            "tasks": [
                {
                    "commands": [
                        {
                            "func": "prepare environment",
                            "vars": {"auto_workload_path": "src/Foo.yml", "test": "foo"},
                        },
                        {"func": "deploy cluster"},
                        {"func": "run test"},
                        {"func": "analyze"},
                    ],
                    "name": "foo",
                    "priority": 5,
                },
                {
                    "commands": [
                        {
                            "func": "prepare environment",
                            "vars": {"auto_workload_path": "src/Bar.yml", "test": "bar"},
                        },
                        {"func": "deploy cluster"},
                        {"func": "run test"},
                        {"func": "analyze"},
                    ],
                    "name": "bar",
                    "priority": 5,
                },
            ],
            "timeout": 64800,
        }
        self.assertDictEqual(expected, parsed)

    def test_variant_tasks(self):
        config = helper(
            [
                MockFile(
                    base_name="bootstrap.yml",
                    modified=False,
                    yaml_conts={"mongodb_setup": "my-setup"},
                ),
                MockFile(
                    base_name="src/Foo.yml",
                    modified=False,
                    yaml_conts={"AutoRun": {"Requires": {"mongodb_setup": ["my-setup"]}}},
                ),
                MockFile(
                    base_name="src/Bar.yml",
                    modified=True,
                    yaml_conts={"AutoRun": {"Requires": {"mongodb_setup": ["some-other-setup"]}}},
                ),
            ],
            ["--variants", "some-variant", "--autorun", "--output", "build/all_tasks.json"],
        )
        parsed = json.loads(config.to_json())
        expected = {"buildvariants": [{"name": "some-variant", "tasks": [{"name": "foo"}]}]}
        self.assertDictEqual(expected, parsed)


if __name__ == "__main__":
    unittest.main()
