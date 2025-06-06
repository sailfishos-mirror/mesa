# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# ALPINE_X86_64_LAVA_TRIGGER_TAG

from __future__ import annotations

import re
from dataclasses import dataclass, field
from datetime import datetime, timedelta, UTC
from math import floor
from typing import TYPE_CHECKING, Any, Optional

from lava.utils.console_format import CONSOLE_LOG

if TYPE_CHECKING:
    from lava.utils.log_section import LogSectionType


# TODO: Add section final status to assist with monitoring
@dataclass
class GitlabSection:
    id: str
    header: str
    type: LogSectionType
    start_collapsed: bool = False
    suppress_end: bool = False
    suppress_start: bool = False
    timestamp_relative_to: Optional[datetime] = None
    escape: str = "\x1b[0K"
    colour: str = f"{CONSOLE_LOG['FG_CYAN']}"
    __start_time: Optional[datetime] = field(default=None, init=False)
    __end_time: Optional[datetime] = field(default=None, init=False)

    @classmethod
    def section_id_filter(cls, value: str) -> str:
        return str(re.sub(r"[^\w_-]+", "-", value))

    def __post_init__(self) -> None:
        self.id = self.section_id_filter(self.id)

    @property
    def has_started(self) -> bool:
        return self.__start_time is not None

    @property
    def has_finished(self) -> bool:
        return self.__end_time is not None

    @property
    def start_time(self) -> Optional[datetime]:
        return self.__start_time

    @property
    def end_time(self) -> Optional[datetime]:
        return self.__end_time

    def get_timestamp(self, time: datetime) -> str:
        unix_ts = datetime.timestamp(time)
        return str(int(unix_ts))

    def section(self, marker: str, header: str, time: datetime) -> str:
        preamble = f"{self.escape}section_{marker}"
        collapse = marker == "start" and self.start_collapsed
        collapsed = "[collapsed=true]" if collapse else ""
        section_id = f"{self.id}{collapsed}"

        timestamp = self.get_timestamp(time)
        before_header = ":".join([preamble, timestamp, section_id])
        if self.timestamp_relative_to and self.start_time is not None:
            delta = self.start_time - self.timestamp_relative_to
            # time drift can occur because we are dealing with timestamps from different sources
            # clamp the delta to 0 if it's negative
            delta = max(delta, timedelta(seconds=0))
            reltime = f"[{floor(delta.seconds / 60):02}:{(delta.seconds % 60):02}] "
        else:
            reltime = ""
        colored_header = f"{self.colour}{reltime}{header}\x1b[0m" if header else ""
        header_wrapper = "\r" + f"{self.escape}{colored_header}"

        return f"{before_header}{header_wrapper}"

    def __str__(self) -> str:
        status = "NS" if not self.has_started else "F" if self.has_finished else "IP"
        delta = self.delta_time()
        elapsed_time = "N/A" if delta is None else str(delta)
        return (
            f"GitlabSection({self.id}, {self.header}, {self.type}, "
            f"SC={self.start_collapsed}, S={status}, ST={self.start_time}, "
            f"ET={self.end_time}, ET={elapsed_time})"
        )

    def __enter__(self) -> "GitlabSection":
        if start_log_line := self.start():
            print(start_log_line)
        return self

    def __exit__(
        self,
        *args: list[Any],
        **kwargs: dict[str, Any],
    ) -> None:
        if end_log_line := self.end():
            print(end_log_line)

    def start(self) -> str:
        assert not self.has_finished, "Starting an already finished section"
        self.__start_time = datetime.now(tz=UTC)
        return self.print_start_section()

    def print_start_section(self) -> str:
        if self.suppress_start:
            return ""
        if self.__start_time is None:
            raise RuntimeError("Start time is not set.")
        return self.section(marker="start", header=self.header, time=self.__start_time)

    def end(self) -> str:
        assert self.__start_time is not None, "Ending an uninitialized section"
        self.__end_time = datetime.now(tz=UTC)
        if self.__end_time < self.__start_time:
            print(
                CONSOLE_LOG["FG_YELLOW"]
                + f"Warning: Section {self.id} ended before it started, clamping the delta time to 0"
                + CONSOLE_LOG["RESET"]
            )
        return self.print_end_section()

    def print_end_section(self) -> str:
        if self.suppress_end:
            return ""
        if self.__end_time is None:
            raise RuntimeError("End time is not set.")
        return self.section(marker="end", header="", time=self.__end_time)

    def _delta_time(self) -> Optional[timedelta]:
        """
        Return the delta time between the start and end of the section.
        If the section has not ended, return the delta time between the start and now.
        If the section has not started and not ended, return None.
        """
        if self.__start_time is None:
            return None

        if self.__end_time is None:
            return datetime.now(tz=UTC) - self.__start_time

        return self.__end_time - self.__start_time

    def delta_time(self) -> Optional[timedelta]:
        """
        Clamp the delta time to zero if it's negative, time drift can occur since we have timestamps
        coming from GitLab jobs, LAVA dispatcher and DUTs.
        """
        delta = self._delta_time()
        if delta is None:
            return None
        return max(delta, timedelta(seconds=0))
