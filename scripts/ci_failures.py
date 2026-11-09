"""Summarize downloaded JUnit reports without requiring job-log access."""

from pathlib import Path
from xml.etree import ElementTree

import tyro
from pydantic import BaseModel


class Reports(BaseModel, frozen=True):
    directory: Path


def report(options: Reports) -> None:
    for path in options.directory.rglob('*.xml'):
        root = ElementTree.parse(path).getroot()
        print(path)
        for case in root.iter('testcase'):
            for result in case:
                if result.tag in ('error', 'failure'):
                    print(case.get('classname'), case.get('name'))
                    print(result.get('message', '')[:1200])
                    for captured in case:
                        if captured.tag in ('system-out', 'system-err'):
                            print((captured.text or '')[-4000:])


if __name__ == '__main__':
    report(tyro.cli(Reports))
