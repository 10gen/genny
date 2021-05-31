import json
import shutil
import unittest
import tempfile
from typing import NamedTuple, List, Optional
from unittest.mock import MagicMock

from genny.tasks.auto_tasks import (
    CurrentBuildInfo,
    CLIOperation,
    WorkloadLister,
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

    def load(self, workspace_root: str, path: str):
        if not self.exists(path):
            raise Exception(f"Yaml file {path} not configured.")
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
    def setUp(self):
        self.workspace_root = tempfile.mkdtemp()

    def cleanUp(self):
        shutil.rmtree(self.workspace_root)

    def assert_result(
        self, given_files: List[MockFile], and_mode: str, then_writes: dict, to_file: str
    ):
        # Create "dumb" mocks.
        lister: WorkloadLister = MagicMock(name="lister", spec=WorkloadLister, instance=True)
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

        # And send them off into the world.
        build = CurrentBuildInfo(reader, workspace_root=".")
        op = CLIOperation.create(
            and_mode, reader, genny_repo_root=".", workspace_root=self.workspace_root
        )
        repo = Repo(lister, reader, workspace_root=self.workspace_root)
        tasks = repo.tasks(op, build)
        writer = ConfigWriter(op)

        config = writer.write(tasks, write=False)
        parsed = json.loads(config.to_json())

        print("parsed")
        print(parsed)
        print("then_writes")
        print(then_writes)
        try:
            self.assertDictEqual(then_writes, parsed)
        except AssertionError:
            # print(parsed)
            raise


TIMEOUT_COMMAND = {
    "command": "timeout.update",
    "params": {"exec_timeout_secs": 86400, "timeout_secs": 7200},
}


def expansions_mock(exp_vars) -> MockFile:
    yaml_conts = {"build_variant": "some-build-variant"}
    yaml_conts.update(exp_vars)
    l = MockFile(base_name="expansions.yml", modified=False, yaml_conts=yaml_conts,)
    print(l)
    return l


class AutoTasksTests(BaseTestClass):
    def test_all_tasks(self):
        self.assert_result(
            given_files=[EMPTY_UNMODIFIED, MULTI_MODIFIED_EQ, EXPANSIONS],
            and_mode="all_tasks",
            then_writes={
                "tasks": [
                    {
                        "name": "empty_unmodified",
                        "commands": [
                            TIMEOUT_COMMAND,
                            {
                                "func": "f_run_dsi_workload",
                                "vars": {
                                    "test_control": "empty_unmodified",
                                    "auto_workload_path": "scale/EmptyUnmodified.yml",
                                },
                            },
                        ],
                        "priority": 5,
                    },
                    {
                        "name": "multi_a",
                        "commands": [
                            TIMEOUT_COMMAND,
                            {
                                "func": "f_run_dsi_workload",
                                "vars": {
                                    "test_control": "multi_a",
                                    "auto_workload_path": "src/Multi.yml",
                                    "mongodb_setup": "a",
                                },
                            },
                        ],
                        "priority": 5,
                    },
                    {
                        "name": "multi_b",
                        "commands": [
                            TIMEOUT_COMMAND,
                            {
                                "func": "f_run_dsi_workload",
                                "vars": {
                                    "test_control": "multi_b",
                                    "auto_workload_path": "src/Multi.yml",
                                    "mongodb_setup": "b",
                                },
                            },
                        ],
                        "priority": 5,
                    },
                ],
                "timeout": 64800,
            },
            to_file="./build/TaskJSON/Tasks.json",
        )

    def run_test_variant_tasks(self, given_files, then_writes_tasks):
        then_writes = {"buildvariants": [{"name": "some-build-variant"}]}
        then_writes["buildvariants"][0].update(then_writes_tasks)
        self.assert_result(
            given_files=given_files,
            and_mode="variant_tasks",
            then_writes=then_writes,
            to_file="./build/TaskJSON/Tasks.json",
        )

    def test_variant_tasks_1(self):
        """Basic test.

        Tests (true) $eq and $neq with scalar values.
        Tests When with single (true) condition.
        Tests multiple When/ThenRun blocks.
        """
        expansions = expansions_mock({"mongodb_setup": "matches"})
        given_files = [
            expansions,
            MockFile(
                base_name="src/workloads/src/MultiUnmodified.yml",
                modified=False,
                yaml_conts={
                    "AutoRun": [
                        {
                            "When": {"mongodb_setup": {"$eq": "matches"}},
                            "ThenRun": [{"mongodb_setup": "c"}, {"mongodb_setup": "d"}],
                        }
                    ]
                },
            ),
            MockFile(
                base_name="src/workloads/scale/Foo.yml",
                modified=False,
                yaml_conts={"AutoRun": [{"When": {"mongodb_setup": {"$neq": "not-matches"}}}]},
            ),
        ]
        then_writes_tasks = {
            "tasks": [
                {"name": "multi_unmodified_c"},
                {"name": "multi_unmodified_d"},
                {"name": "foo"},
            ]
        }
        self.run_test_variant_tasks(given_files=given_files, then_writes_tasks=then_writes_tasks)

    def test_variant_tasks_2(self):
        """Similar to above test.

        Tests (false) $eq and $neq with scalar values.
        Tests When with single (false) condition.
        Tests multiple When/ThenRun blocks.
        """
        expansions = expansions_mock({"mongodb_setup": "matches"})
        given_files = [
            expansions,
            MockFile(
                base_name="src/workloads/src/MultiUnmodified.yml",
                modified=False,
                yaml_conts={
                    "AutoRun": [
                        {
                            "When": {
                                "mongodb_setup": {"$eq": "not-matches"}  # this invalidates the When
                            },
                            "ThenRun": [{"mongodb_setup": "c"}, {"mongodb_setup": "d"}],
                        }
                    ]
                },
            ),
            MockFile(
                base_name="src/workloads/scale/Foo.yml",
                modified=False,
                yaml_conts={
                    "AutoRun": [
                        {
                            "When": {
                                "mongodb_setup": {"$neq": "matches"}  # this invalidates the When
                            }
                        }
                    ]
                },
            ),
        ]
        then_writes_tasks = {}
        self.run_test_variant_tasks(given_files=given_files, then_writes_tasks=then_writes_tasks)

    def test_variant_tasks_3(self):
        """
        More complex test.

        Tests (true) $eq and $neq conditions with lists of values.
        Tests When with multiple (true) conditions.
        Tests multiple When/ThenRun blocks.
        """
        expansions = expansions_mock({"mongodb_setup": "matches", "branch": "4.4"})
        given_files = [
            expansions,
            MockFile(
                base_name="src/workloads/src/MultiUnmodified.yml",
                modified=False,
                yaml_conts={
                    "AutoRun": [
                        {
                            "When": {
                                "mongodb_setup": {"$neq": ["something-else", "something-else-2"]},
                                "branch": {"$neq": ["4.0", "4.2"]},
                            },
                            "ThenRun": [
                                {"mongodb_setup": "c"},
                                {"mongodb_setup": "d"},
                                {"infrastructure_provisioning": "infra_a"},  # test that arb bootstrap_key works
                            ],
                        },
                        {
                            "When": {"mongodb_setup": {"$eq": ["matches", "matches1", "matches2"]}},
                            "ThenRun": [{"mongodb_setup": "e"}, {"mongodb_setup": "f"}],
                        },
                    ]
                },
            ),
            MockFile(
                base_name="src/workloads/scale/Foo.yml",
                modified=False,
                yaml_conts={"AutoRun": [{"When": {"mongodb_setup": {"$neq": "not-matches"}}}]},
            ),
        ]
        then_writes_tasks = {
            "tasks": [
                {"name": "multi_unmodified_c"},
                {"name": "multi_unmodified_d"},
                {"name": "multi_unmodified_e"},
                {"name": "multi_unmodified_f"},
                {"name": "multi_unmodified_infra_a"},
                {"name": "foo"},
            ],
        }
        self.run_test_variant_tasks(given_files=given_files, then_writes_tasks=then_writes_tasks)

    def test_variant_tasks_4(self):
        """
        Similar to above test.

        Tests (false) $eq and $neq conditions with lists of values.
        Tests When with multiple (one true, one false) conditions.
        Tests multiple When/ThenRun blocks.
        """
        expansions = expansions_mock({"mongodb_setup": "matches", "branch": "4.2"})
        given_files = [
            expansions,
            MockFile(
                base_name="src/workloads/src/MultiUnmodified.yml",
                modified=False,
                yaml_conts={
                    "AutoRun": [
                        {
                            "When": {
                                "mongodb_setup": {"$neq": ["something-else", "something-else-2"]},
                                "branch": {"$neq": ["4.0", "4.2"]},  # this invalidates the When
                            },
                            "ThenRun": [
                                {"mongodb_setup": "c"},
                                {"mongodb_setup": "d"},
                                {"infrastructure_provisioning": "infra_a"},
                            ],
                        },
                        {
                            "When": {
                                "mongodb_setup": {
                                    "$eq": ["not-matches", "not-matches1"]
                                }  # this invalidates the When
                            },
                            "ThenRun": [{"mongodb_setup": "e"}, {"mongodb_setup": "f"}],
                        },
                    ]
                },
            ),
        ]
        then_writes_tasks = {}
        self.run_test_variant_tasks(given_files=given_files, then_writes_tasks=then_writes_tasks)

    def test_patch_tasks(self):
        # Patch tasks always run for the selected variant
        # but only if "When" condition is met.
        self.assert_result(
            given_files=[
                EXPANSIONS,
                MULTI_MODIFIED_EQ,
                MULTI_UNMODIFIED_EQ,
                NOT_MATCHES_MODIFIED,
                NOT_MATCHES_UNMODIFIED,
            ],
            and_mode="patch_tasks",
            then_writes={
                "buildvariants": [
                    {
                        "name": "some-build-variant",
                        "tasks": [{"name": "multi_a"}, {"name": "multi_b"}],
                    }
                ]
            },
            to_file="./build/TaskJSON/Tasks.json",
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

MULTI_MODIFIED_EQ = MockFile(
    base_name="src/workloads/src/Multi.yml",
    modified=True,
    yaml_conts={
        "AutoRun": [
            {
                "When": {"mongodb_setup": {"$eq": "matches"}},
                "ThenRun": [{"mongodb_setup": "a"}, {"mongodb_setup": "b"}],
            }
        ]
    },
)

MULTI_UNMODIFIED_EQ = MockFile(
    base_name="src/workloads/src/MultiUnmodified.yml",
    modified=False,
    yaml_conts={
        "AutoRun": [
            {
                "When": {"mongodb_setup": {"$eq": "matches"}},
                "ThenRun": [{"mongodb_setup": "c"}, {"mongodb_setup": "d"}],
            }
        ]
    },
)

MULTI_UNMODIFIED_NEQ = MockFile(
    base_name="src/workloads/src/MultiUnmodified.yml",
    modified=False,
    yaml_conts={
        "AutoRun": [
            {
                "When": {"mongodb_setup": {"$eq": "some-other-setup"}},
                "ThenRun": [{"mongodb_setup": "c"}, {"mongodb_setup": "d"}],
            }
        ]
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
    yaml_conts={"AutoRun": [{"When": {"mongodb_setup": {"$eq": "matches"}},}]},
)

NOT_MATCHES_UNMODIFIED = MockFile(
    base_name="src/workloads/scale/Foo.yml",
    modified=False,
    yaml_conts={"AutoRun": [{"When": {"mongodb_setup": {"$eq": "some-other-setup"}},}]},
)


NOT_MATCHES_MODIFIED = MockFile(
    base_name="src/workloads/scale/NotMatchesModified.yml",
    modified=True,
    yaml_conts={"AutoRun": [{"When": {"mongodb_setup": {"$eq": "some-other-setup"}},}]},
)


if __name__ == "__main__":
    unittest.main()
