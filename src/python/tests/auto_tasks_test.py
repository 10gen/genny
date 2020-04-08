import json
import unittest
from typing import NamedTuple, List, Optional
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


class MockReader(YamlReader):
    def __init__(self, files: List[MockFile]):
        self.files = files

    def load(self, path: str):
        return self._find_file(path).yaml_conts

    def exists(self, path: str) -> bool:
        found = self._find_file(path)
        return found is not None

    def _find_file(self, path: str):
        found = [f for f in self.files if f.base_name == path]
        if found:
            return found[0]
        return None


class BaseTestClass(unittest.TestCase):
    def assert_result(
        self, given_files: List[MockFile], and_args: List[str], then_writes: dict, to_file: str
    ):
        # Create "dumb" mocks.
        lister: FileLister = MagicMock(name="lister", spec=FileLister, instance=True)
        reader: YamlReader = MockReader(given_files)

        # Make them smarter.
        lister.all_workload_files.return_value = [
            v.base_name
            for v in given_files
            # Hack: Prevent calling e.g. expansions.yml a 'workload' file
            # (solution would be to pass in workload files as a different
            # param to this function, but meh)
            if "/" in v.base_name
        ]
        lister.modified_workload_files.return_value = [
            v.base_name for v in given_files if v.modified and "/" in v.base_name
        ]

        # Put a dummy argv[0] as test sugar
        and_args.insert(0, "dummy.py")

        # And send them off into the world.
        build = CurrentBuildInfo(reader)
        op = CLIOperation.parse(and_args, reader)
        repo = Repo(lister, reader)
        tasks = repo.tasks(op, build)
        writer = ConfigWriter(op)

        config = writer.write(tasks, write=False)
        parsed = json.loads(config.to_json())
        try:
            self.assertDictEqual(then_writes, parsed)
        except AssertionError:
            print(parsed)
            raise
        self.assertEqual(op.output_file, to_file)


class AutoTasksTests(BaseTestClass):
    def test_all_tasks(self):
        self.assert_result(
            given_files=[EMPTY_UNMODIFIED, MULTI_MODIFIED, EXPANSIONS],
            and_args=["all_tasks"],
            then_writes={
                "tasks": [
                    {
                        "name": "empty_unmodified",
                        "commands": [
                            {
                                "func": "f_run_dsi_workload",
                                "vars": {
                                    "test_control": "empty_unmodified",
                                    "auto_workload_path": "scale/EmptyUnmodified.yml",
                                },
                            }
                        ],
                        "priority": 5,
                    },
                    {
                        "name": "multi_a",
                        "commands": [
                            {
                                "func": "f_run_dsi_workload",
                                "vars": {
                                    "test_control": "multi_a",
                                    "auto_workload_path": "src/Multi.yml",
                                    "mongodb_setup": "a",
                                },
                            }
                        ],
                        "priority": 5,
                    },
                    {
                        "name": "multi_b",
                        "commands": [
                            {
                                "func": "f_run_dsi_workload",
                                "vars": {
                                    "test_control": "multi_b",
                                    "auto_workload_path": "src/Multi.yml",
                                    "mongodb_setup": "b",
                                },
                            }
                        ],
                        "priority": 5,
                    },
                ],
                "timeout": 64800,
            },
            to_file="./run/build/Tasks/Tasks.json",
        )

    def test_variant_tasks(self):
        self.assert_result(
            given_files=[EXPANSIONS, MULTI_UNMODIFIED, MATCHES_UNMODIFIED],
            and_args=["variant_tasks"],
            then_writes={
                "buildvariants": [
                    {
                        "name": "some-build-variant",
                        "tasks": [
                            {"name": "multi_unmodified_c"},
                            {"name": "multi_unmodified_d"},
                            {"name": "foo"},
                        ],
                    }
                ]
            },
            to_file="./run/build/Tasks/Tasks.json",
        )

    def test_patch_tasks(self):
        # "Patch tasks always run for the selected variant "
        # "even if their Requires blocks don't align",
        self.assert_result(
            given_files=[
                EXPANSIONS,
                MULTI_MODIFIED,
                MULTI_UNMODIFIED,
                NOT_MATCHES_MODIFIED,
                NOT_MATCHES_UNMODIFIED,
            ],
            and_args=["patch_tasks"],
            then_writes={
                "buildvariants": [
                    {
                        "name": "some-build-variant",
                        "tasks": [
                            {"name": "multi_a"},
                            {"name": "multi_b"},
                            {"name": "not_matches_modified"},
                        ],
                    }
                ]
            },
            to_file="./run/build/Tasks/Tasks.json",
        )


class LegacyHappyCaseTests(BaseTestClass):
    def test_all_tasks(self):
        self.assert_result(
            given_files=[EMPTY_UNMODIFIED, EMPTY_MODIFIED, MULTI_MODIFIED],
            and_args=["--generate-all-tasks", "--output", "build/all_tasks.json"],
            then_writes={
                "tasks": [
                    {
                        "name": "empty_unmodified",
                        "commands": [
                            {
                                "func": "prepare environment",
                                "vars": {
                                    "test": "empty_unmodified",
                                    "auto_workload_path": "scale/EmptyUnmodified.yml",
                                },
                            },
                            {"func": "deploy cluster"},
                            {"func": "run test"},
                            {"func": "analyze"},
                        ],
                        "priority": 5,
                    },
                    {
                        "name": "empty_modified",
                        "commands": [
                            {
                                "func": "prepare environment",
                                "vars": {
                                    "test": "empty_modified",
                                    "auto_workload_path": "scale/EmptyModified.yml",
                                },
                            },
                            {"func": "deploy cluster"},
                            {"func": "run test"},
                            {"func": "analyze"},
                        ],
                        "priority": 5,
                    },
                    {
                        "name": "multi_a",
                        "commands": [
                            {
                                "func": "prepare environment",
                                "vars": {
                                    "test": "multi_a",
                                    "auto_workload_path": "src/Multi.yml",
                                    "setup": "a",
                                },
                            },
                            {"func": "deploy cluster"},
                            {"func": "run test"},
                            {"func": "analyze"},
                        ],
                        "priority": 5,
                    },
                    {
                        "name": "multi_b",
                        "commands": [
                            {
                                "func": "prepare environment",
                                "vars": {
                                    "test": "multi_b",
                                    "auto_workload_path": "src/Multi.yml",
                                    "setup": "b",
                                },
                            },
                            {"func": "deploy cluster"},
                            {"func": "run test"},
                            {"func": "analyze"},
                        ],
                        "priority": 5,
                    },
                ],
                "timeout": 64800,
            },
            to_file="../src/genny/genny/build/all_tasks.json",
        )

    def test_variant_tasks(self):
        self.assert_result(
            given_files=[BOOTSTRAP, MATCHES_UNMODIFIED, NOT_MATCHES_MODIFIED],
            and_args=[
                "--output",
                "build/auto_tasks.json",
                "--variants",
                "some-variant",
                "--autorun",
            ],
            then_writes={"buildvariants": [{"name": "some-variant", "tasks": [{"name": "foo"}]}]},
            to_file="../src/genny/genny/build/auto_tasks.json",
        )

    def test_patch_tasks(self):
        self.assert_result(
            given_files=[BOOTSTRAP, MATCHES_UNMODIFIED, MULTI_MODIFIED, MULTI_UNMODIFIED],
            and_args=[
                "--output",
                "build/patch_tasks.json",
                "--variants",
                "some-variant",
                "--modified",
            ],
            then_writes={
                "buildvariants": [
                    {"name": "some-variant", "tasks": [{"name": "multi_a"}, {"name": "multi_b"}]}
                ]
            },
            to_file="./genny/genny/build/patch_tasks.json",
        )


# Example Input Files


BOOTSTRAP = MockFile(
    base_name="bootstrap.yml", modified=False, yaml_conts={"mongodb_setup": "matches"}
)

EXPANSIONS = MockFile(
    base_name="expansions.yml",
    modified=False,
    yaml_conts={"build_variant": "some-build-variant", "mongodb_setup": "matches"},
)

MULTI_MODIFIED = MockFile(
    base_name="src/workloads/src/Multi.yml",
    modified=True,
    yaml_conts={
        "AutoRun": {
            "Requires": {"mongodb_setup": ["matches"]},
            "PrepareEnvironmentWith": {"mongodb_setup": ["a", "b"]},
        }
    },
)


MULTI_UNMODIFIED = MockFile(
    base_name="src/workloads/src/MultiUnmodified.yml",
    modified=False,
    yaml_conts={
        "AutoRun": {
            "Requires": {"mongodb_setup": ["matches"]},
            "PrepareEnvironmentWith": {"mongodb_setup": ["c", "d"]},
        }
    },
)

EMPTY_MODIFIED = MockFile(
    base_name="src/workloads/scale/EmptyModified.yml", modified=True, yaml_conts={}
)
EMPTY_UNMODIFIED = MockFile(
    base_name="src/workloads/scale/EmptyUnmodified.yml", modified=False, yaml_conts={}
)


MATCHES_UNMODIFIED = MockFile(
    base_name="src/workloads/scale/Foo.yml",
    modified=False,
    yaml_conts={"AutoRun": {"Requires": {"mongodb_setup": ["matches"]}}},
)

NOT_MATCHES_UNMODIFIED = MockFile(
    base_name="src/workloads/scale/Foo.yml",
    modified=False,
    yaml_conts={"AutoRun": {"Requires": {"mongodb_setup": ["some-other-setup"]}}},
)


NOT_MATCHES_MODIFIED = MockFile(
    base_name="src/workloads/scale/NotMatchesModified.yml",
    modified=True,
    yaml_conts={"AutoRun": {"Requires": {"mongodb_setup": ["some-other-setup"]}}},
)


if __name__ == "__main__":
    unittest.main()
