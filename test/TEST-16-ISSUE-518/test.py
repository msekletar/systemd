#!/usr/bin/python

#
#  Copyright 2017 Michal Sekletar <msekleta@redhat.com>
#
#  systemd is free software; you can redistribute it and/or modify it
#  under the terms of the GNU Lesser General Public License as published by
#  the Free Software Foundation; either version 2.1 of the License, or
#  (at your option) any later version.
#
#  systemd is distributed in the hope that it will be useful, but
#  WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
#  Lesser General Public License for more details.
#
#  You should have received a copy of the GNU Lesser General Public License
#  along with python-systemd; If not, see <http://www.gnu.org/licenses/>.

import unittest
import time
import os
import tempfile

from pydbus import SystemBus
from enum import Enum

class UnitFileChange(Enum):
    NO_CHANGE = 0
    LINES_SWAPPED = 1
    COMMAND_ADDED_BEFORE = 2
    COMMAND_ADDED_AFTER = 3
    COMMAND_INTERLEAVED = 4
    REMOVAL = 5

class ExecutionResumeTest(unittest.TestCase):
    def setUp(self):
        self.unit = 'test-issue-518.service'
        self.unitfile_path = '/run/systemd/system/{0}'.format(self.unit)
        self.output_file = tempfile.mktemp()
        self.bus = SystemBus()
        self.manager = self.bus.get('org.freedesktop.systemd1', '/org/freedesktop/systemd1')
        self.unit_files = {}

        unit_file_content = '''
        [Service]
        Type=oneshot
        ExecStart=/bin/sleep 2
        ExecStart=/bin/bash -c "echo foo >> {0}"
        '''.format(self.output_file)
        self.unit_files[UnitFileChange.NO_CHANGE] = unit_file_content

        unit_file_content = '''
        [Service]
        Type=oneshot
        ExecStart=/bin/bash -c "echo foo >> {0}"
        ExecStart=/bin/sleep 2
        '''.format(self.output_file)
        self.unit_files[UnitFileChange.LINES_SWAPPED] = unit_file_content

        unit_file_content = '''
        [Service]
        Type=oneshot
        ExecStart=/bin/bash -c "echo bar >> {0}"
        ExecStart=/bin/sleep 2
        ExecStart=/bin/bash -c "echo foo >> {0}"
        '''.format(self.output_file)
        self.unit_files[UnitFileChange.COMMAND_ADDED_BEFORE] = unit_file_content

        unit_file_content = '''
        [Service]
        Type=oneshot
        ExecStart=/bin/sleep 2
        ExecStart=/bin/bash -c "echo foo >> {0}"
        ExecStart=/bin/bash -c "echo bar >> {0}"
        '''.format(self.output_file)
        self.unit_files[UnitFileChange.COMMAND_ADDED_AFTER] = unit_file_content

        unit_file_content = '''
        [Service]
        Type=oneshot
        ExecStart=/bin/bash -c "echo baz >> {0}"
        ExecStart=/bin/sleep 2
        ExecStart=/bin/bash -c "echo foo >> {0}"
        ExecStart=/bin/bash -c "echo bar >> {0}"
        '''.format(self.output_file)
        self.unit_files[UnitFileChange.COMMAND_INTERLEAVED] = unit_file_content

        unit_file_content = '''
        [Service]
        Type=oneshot
        ExecStart=/bin/bash -c "echo bar >> {0}"
        ExecStart=/bin/bash -c "echo baz >> {0}"
        '''.format(self.output_file)
        self.unit_files[UnitFileChange.REMOVAL] = unit_file_content

    def write_unit_file(self, unit_file_change):
        if not isinstance(unit_file_change, UnitFileChange):
            raise ValueError('Unknown unit file change')

        content = self.unit_files[unit_file_change]

        with open(self.unitfile_path, 'w') as f:
            f.write(content)

        self.manager.Reload()

    def check_output(self, expected_output):
        try:
            with open(self.output_file, 'r') as log:
                output = log.read()
        except IOError:
            self.fail()

        self.assertEqual(output, expected_output)

    def setup_unit(self):
        self.write_unit_file(UnitFileChange.NO_CHANGE)
        self.manager.StartUnit(self.unit, 'replace')

    def reload_unit(self):
        self.manager.Reload()
        time.sleep(4)

    def test_no_change(self):
        expected_output = 'foo\n'

        self.setup_unit()
        self.reload_unit()

        self.check_output(expected_output)

    def test_swapped(self):
        expected_output = ''

        self.setup_unit()
        self.write_unit_file(UnitFileChange.LINES_SWAPPED)
        self.reload_unit()

        self.assertTrue(not os.path.exists(self.output_file))

    def test_added_before(self):
        expected_output = 'foo\n'

        self.setup_unit()
        self.write_unit_file(UnitFileChange.COMMAND_ADDED_BEFORE)
        self.reload_unit()

        self.check_output(expected_output)

    def test_added_after(self):
        expected_output = 'foo\nbar\n'

        self.setup_unit()
        self.write_unit_file(UnitFileChange.COMMAND_ADDED_AFTER)
        self.reload_unit()

        self.check_output(expected_output)

    def test_interleaved(self):
        expected_output = 'foo\nbar\n'

        self.setup_unit()
        self.write_unit_file(UnitFileChange.COMMAND_INTERLEAVED)
        self.reload_unit()

        self.check_output(expected_output)

    def test_removal(self):
        self.setup_unit()
        self.write_unit_file(UnitFileChange.REMOVAL)
        self.reload_unit()

        self.assertTrue(not os.path.exists(self.output_file))

    def tearDown(self):
        try:
            os.remove(self.output_file)
            os.remove(self.unitfile_path)
        except OSError:
            # ignore error if log file doesn't exist
            pass

        self.manager.Reload()

if __name__ == '__main__':
    unittest.main()
