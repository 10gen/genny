import os
import shutil
import subprocess
import datetime
from uuid import uuid4
from typing import Optional

import structlog
from contextlib import contextmanager

from cmd_runner import run_command, CmdOutput
from download import Downloader

SLOG = structlog.get_logger(__name__)


def _find_curator() -> Optional[str]:
    in_bin = os.path.join("bin", "curator")
    if os.path.exists(in_bin):
        return in_bin

    in_build = os.path.join("build", "curator", "curator")
    if os.path.exists(in_build):
        return in_build

    return None


def _get_poplar_args():
    """
    Returns the argument list used to create the Poplar gRPC process.

    If we are in the root of the genny repo, use the local executable.
    Otherwise we search the PATH.
    """
    curator = _find_curator()
    if curator is None:
        raise Exception(f"Curator not found in cwd {os.getcwd()}")
    return [curator, "poplar", "grpc"]


_DATE_FORMAT = "%Y-%m-%dT%H%M%SZ"
_METRICS_PATH = "build/CedarMetrics"


def _cleanup_metrics():
    if not os.path.exists(_METRICS_PATH):
        return
    uuid = str(uuid4())[:8]
    ts = datetime.datetime.utcnow().strftime(_DATE_FORMAT)
    dest = f"{_METRICS_PATH}-{ts}-{uuid}"
    shutil.move(_METRICS_PATH, dest)
    SLOG.info(
        "Moved existing metrics (presumably from a prior run).",
        existing=_METRICS_PATH,
        moved_to=dest,
        cwd=os.getcwd(),
    )


@contextmanager
def poplar_grpc(cleanup_metrics: bool):
    args = _get_poplar_args()

    if cleanup_metrics:
        _cleanup_metrics()

    SLOG.info("Running poplar", command=args, cwd=os.getcwd())
    poplar = subprocess.Popen(args)
    # stdout=subprocess.PIPE, stderr=subprocess.PIPE
    if poplar.poll() is not None:
        raise OSError("Failed to start Poplar.")
    try:
        yield poplar
    finally:
        try:
            poplar.terminate()
            exit_code = poplar.wait(timeout=10)
            if exit_code not in (0, -15):  # Termination or exit.
                raise OSError(f"Poplar exited with code: {exit_code}.")
        except:
            # If Poplar doesn't die then future runs can be broken.
            if poplar.poll() is None:
                poplar.kill()
            raise


# For now we put curator in ./src/genny/build/curator, but ideally it would be in ./bin
# or in the python venv (if we make 'pip install curator' a thing).
def ensure_curator_installed(genny_repo_root: str, os_family: str, distro: str):
    install_dir = os.path.join(genny_repo_root, "build")
    os.makedirs(install_dir, exist_ok=True)
    downloader = CuratorDownloader(os_family=os_family, distro=distro, install_dir=install_dir)
    downloader.fetch_and_install()


class CuratorDownloader(Downloader):
    # These build IDs are from the Curator Evergreen task.
    # https://evergreen.mongodb.com/waterfall/curator

    # Note that DSI also downloads Curator, the location is specified in defaults.yml.
    # Please try to keep the two versions consistent.
    CURATOR_VERSION = "d3da25b63141aa192c5ef51b7d4f34e2f3fc3880"

    def __init__(self, os_family: str, distro: str, install_dir: str):
        super().__init__(
            os_family=os_family, distro=distro, install_dir=install_dir, name="curator"
        )
        if self._os_family == "Darwin":
            self._distro = "macos"

        if "ubuntu" in self._distro:
            self._distro = "ubuntu1604"

        if self._distro in ("amazon2", "rhel8", "rhel62"):
            self._distro = "rhel70"

    def _get_url(self):
        return (
            "https://s3.amazonaws.com/boxes.10gen.com/build/curator/"
            "curator-dist-{distro}-{build}.tar.gz".format(
                distro=self._distro, build=CuratorDownloader.CURATOR_VERSION
            )
        )

    def _can_ignore(self):
        curator = _find_curator()
        if curator is None:
            return False
        res: CmdOutput = run_command(
            cmd=[curator, "-v"], check=True,
        )
        installed_version = "".join(res.stdout).strip()
        wanted_version = f"curator version {CuratorDownloader.CURATOR_VERSION}"
        SLOG.debug("Comparing curator versions", wanted=wanted_version, installed=installed_version)
        return installed_version == wanted_version
