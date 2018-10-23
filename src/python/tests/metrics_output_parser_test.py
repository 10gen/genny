"""Tests for metrics_output_parser"""

import os
import unittest

import genny.metrics_output_parser as parser


def parse_string(input_str):
    lines = [line.strip() for line in input_str.split("\n")]
    return parser.parse(lines, "InputString").timers()


def parse_file(path):
    full_path = os.path.join('.', 'tests', 'fixtures', 'metrics_output_parser-' + path + '.txt')
    with open(full_path, 'r') as f:
        return parser.parse(f, full_path).timers()


class GennyOutputParserTest(unittest.TestCase):
    def raises_parse_error(self, input_str):
        with self.assertRaises(parser.ParseError):
            parse_string(input_str)

    def test_no_clocks(self):
        self.raises_parse_error("""
        Timers
        1234,A.0.o,345
        """)

    def test_missing_clocks(self):
        self.raises_parse_error("""
        Clocks

        Timers
        1234,A.0.o,345
        """)

    def test_timers_before_clocks(self):
        self.raises_parse_error("""
        Timers
        1234,A.0.o,345

        Clocks
        SystemTime,23439048
        MetricsTime,303947
        """)

    def test_csv_no_sections_have_data(self):
        self.assertEqual(
            parse_string("""
        Clocks
        
        Gauges
        
        Counters
        
        Timers
        """), {})

    def test_empty_input(self):
        self.assertEqual(parse_string(""), {})

    def test_fixture1(self):
        actual = parse_file('fixture1')
        print(actual)
        self.assertEqual(
            actual, {
                'InsertTest.output': {
                    'mean': 1252307.75,
                    'n': 4,
                    'threads': {0, 1},
                    'started': 1537814141061109,
                    'ended': 1537814143687260
                },
                'HelloTest.output': {
                    'mean': 55527.25,
                    'n': 4,
                    'threads': {0, 1},
                    'started': 1537814141061476,
                    'ended': 1537814143457943
                }
            })

    def test_fixture2(self):
        actual = parse_file('fixture2')
        print(actual)
        self.assertEqual(
            actual, {
                'InsertRemoveTest.remove': {
                    'mean': 4297048.190765498,
                    'n': 823,
                    'threads': {
                        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
                        21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
                        40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58,
                        59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77,
                        78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96,
                        97, 98, 99
                    },
                    'started': 1540233103870294,
                    'ended': 1540233383199723
                },
                'InsertRemoveTest.insert': {
                    'mean': 8656706.697448373,
                    'n': 823,
                    'threads': {
                        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
                        21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
                        40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58,
                        59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77,
                        78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96,
                        97, 98, 99
                    },
                    'started': 1540233073074953,
                    'ended': 1540233380763649
                },
                'Genny.Setup': {
                    'mean': 8694761.0,
                    'n': 1,
                    'threads': {0},
                    'started': 1540233035593684,
                    'ended': 1540233044288445
                }
            })
