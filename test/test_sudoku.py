#!/usr/bin/env python
# Copyright (c) 2008-2014 Aymeric Augustin

"""Tests for the Python and C implementations of the SuDoKu class.

This testing suite is designed to run the same tests on the Python and C
implementations. The public methods of either class must behave identically.

Therefore, each test class mixes two classes:
  - A class that determines whether the Python or the C implementation will
    be used, by setting the module class variable.
  - A test suite, extending unittest.TestCase, that groups test related
    to a give topic: resolution, generation, input or output ;

As a consequence, within a test suite, the SuDoKu class must always be
refered to as self.module.SuDoKu.
"""

import os.path
import re
import io
import sys
import unittest
sys.path[:0] = [os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))]
import sudoku.csudoku
import sudoku.pysudoku


sudokus = (
    ('__6___2_49_1________853_6____2__5______8_7___________675___4__8____8_5_3___2_9___',
     '536918274921476835478532619392645781615897342847123956759364128264781593183259467'),
    ('__6_9___4_____2__8__754__6___893_2__3________9___2___5______5__2___18____61_____7',
     '526891734143672958897543162658937241312485679974126385789264513235718496461359827'),
    ('__7_26__8__4__8______17___2_1__92__5______6_17______3_____5____48_7_____62_____54',
     '137426598294538167568179342813692475952347681746815239379254816485761923621983754'),
    ('__7_1_______3_28__6______1_85__________________1_7__951_9__4__6___7_9_5__26_8____',
     '287916534915342867643857912852491673794635128361278495179524386438769251526183749'),
)


class PyModule(object):

    module = sudoku.pysudoku


class CModule(object):

    module = sudoku.csudoku


class Properties(object):

    def testStr(self):
        problem = sudokus[0][0]
        s = self.module.SuDoKu()
        self.assertEqual(str(s), '_' * 81)
        s.from_string(problem)
        self.assertEqual(str(s), problem)
        s.resolve()
        self.assertEqual(str(s), problem)

    def testRepr(self):
        c = 'sudoku.SuDoKu'
        p = sudokus[0][0]
        s = self.module.SuDoKu()
        self.assertEqual(repr(s), '%s()' % c)
        s.d = True
        self.assertEqual(repr(s), '%s(debug=True)' % c)
        s.e = False
        self.assertEqual(repr(s), '%s(estimate=False, debug=True)' % c)
        s.d = False
        self.assertEqual(repr(s), '%s(estimate=False)' % c)
        s.e = True
        s.from_string(p)
        self.assertEqual(repr(s), '%s(problem="%s")' % (c, p))
        s.d = True
        self.assertEqual(repr(s), '%s(problem="%s", debug=True)' % (c, p))
        s.e = False
        self.assertEqual(repr(s), '%s(problem="%s", estimate=False, debug=True)' % (c, p))
        s.d = False
        self.assertEqual(repr(s), '%s(problem="%s", estimate=False)' % (c, p))
        s.e = True


class ResAndEst(object):

    def testResolve(self):
        forks = 0
        for i, (problem, solution) in enumerate(sudokus):
            s = self.module.SuDoKu(problem)
            solutions = s.resolve()
            self.assertEqual(len(solutions), 1)
            self.assertEqual(s.to_string(values=solutions[0]), solution)
            l, f = s.estimate()
            self.assertTrue(i + 1 <= l < i + 2)
            self.assertTrue(forks < f)
            forks = f

    def testRedundancyIsAllowedInProblems(self):
        problem, solution = sudokus[0]
        s = self.module.SuDoKu(problem[:40] + solution[40:])
        self.assertEqual(s.to_string(values=s.resolve()[0]), solution)

    def testContradictionsAreDetectedInProblems(self):
        problem, solution = sudokus[0]
        s = self.module.SuDoKu('66' + problem[2:])
        self.assertRaises(self.module.Contradiction, s.resolve)

    def testEstimationCanBeDisabled(self):
        s = self.module.SuDoKu(sudokus[0][0], estimate=False)
        s.resolve()
        self.assertEqual(None, s.estimate())

    def testDebugCanBeEnabled(self):
        problem, solution = sudokus[0]
        s = self.module.SuDoKu(problem, debug=True)
        sys.stdout = io.StringIO() if sys.version_info[0] >= 3 else io.BytesIO()
        s.resolve()
        output = sys.stdout.getvalue()
        sys.stdout = sys.__stdout__
        self.assertNotEqual(output.find(solution), -1)


class Generation(object):

    def testGenerate(self):
        s = self.module.SuDoKu()
        s.generate()
        non_zero = len([None for i in range(9) for j in range(9) if s.o[i][j]])
        self.assertTrue(0 < non_zero < 81)
        solutions = s.resolve()
        self.assertEqual(len(solutions), 1)


class Input(object):

    def setUp(self):
        self.problem = sudokus[0][0]

    def testFromStringSavesValuesInOriginalArray(self):
        s = self.module.SuDoKu()
        s.from_string(self.problem)
        self.assertEqual(s.o[0][0], 0)
        self.assertEqual(s.o[0][2], 6)
        self.assertEqual(s.o[2][4], 3)
        self.assertEqual(s.o[8][5], 9)

    def testFromStringAcceptsLineBreaks(self):
        s = self.module.SuDoKu(self.problem)
        lines = [self.problem[i:i+9] for i in range(0, 81, 9)]
        t = self.module.SuDoKu(''.join([l + '\n' for l in lines]))
        self.assertEqual(t.o, s.o)
        t = self.module.SuDoKu(''.join([l + '\r\n' for l in lines]))
        self.assertEqual(t.o, s.o)
        t = self.module.SuDoKu(''.join([l + '\r' for l in lines]))
        self.assertEqual(t.o, s.o)

    def testFromStringAcceptsSeveralCharactersToMarkEmptyCells(self):
        s = self.module.SuDoKu(self.problem)
        altered = (self.problem[:20].replace('_', '-')
                 + self.problem[20:40].replace('_', ' ')
                 + self.problem[40:60].replace('_', '.')
                 + self.problem[60:].replace('_', '0'))
        t = self.module.SuDoKu(altered)
        self.assertEqual(t.o, s.o)

    def testFromStringRejectsOtherCharacters(self):
        for i in range(127):
            if chr(i) in '123456789\r\n_- .0':
                continue
            altered = chr(i) + self.problem[1:]
            self.assertRaises(ValueError, self.module.SuDoKu, altered)

    def testFromStringChecksInputLength(self):
        self.assertRaises(ValueError, self.module.SuDoKu, self.problem + '_')
        self.assertRaises(ValueError, self.module.SuDoKu, self.problem[:-1])

    def testFromStringDoesNotEnforceRules(self):
        s = self.module.SuDoKu('66' + self.problem[2:])
        self.assertEqual(s.o[0][0], s.o[0][1])


class Output(object):

    def setUp(self):
        self.problem, self.solution = sudokus[0]
        self.s = self.module.SuDoKu(self.problem)
        self.g = self.s.resolve()[0]

    def testProblemWithInvalidFormat(self):
        self.assertRaises(ValueError, self.s.to_string, 'spam')

    def testSolutionWithInvalidFormat(self):
        self.assertRaises(ValueError, self.s.to_string, 'eggs', self.g)

    def testProblemToConsole(self):
        output = self.s.to_string('console')
        output = re.sub(r'[^1-9_]+', '', output.replace('   ', ' _ '))
        self.assertEqual(output, self.problem)

    def testSolutionToConsole(self):
        output = self.s.to_string('console', self.g)
        output = re.sub(r'[^1-9_]+', '', output.replace('   ', ' _ '))
        self.assertEqual(output, self.solution)

    def testProblemToHtml(self):
        output = self.s.to_string('html')
        output = re.sub(r'<.+?>', '', output).replace('&nbsp;', '_')
        self.assertEqual(output, self.problem)

    def testSolutionToHtml(self):
        output = self.s.to_string('html', self.g)
        output = re.sub(r'<.+?>', '', output).replace('&nbsp;', '_')
        self.assertEqual(output, self.solution)

    def testProblemToString(self):
        output = self.s.to_string('string')
        self.assertEqual(output, self.problem)

    def testSolutionToString(self):
        output = self.s.to_string('string', self.g)
        self.assertEqual(output, self.solution)


class TestPyProperties(PyModule, Properties, unittest.TestCase):
    pass


class TestCProperties(CModule, Properties, unittest.TestCase):
    pass


class TestPyResAndEst(PyModule, ResAndEst, unittest.TestCase):
    pass


class TestCResAndEst(CModule, ResAndEst, unittest.TestCase):
    pass


class TestPyGeneration(PyModule, Generation, unittest.TestCase):
    pass


class TestCGeneration(CModule, Generation, unittest.TestCase):
    pass


class TestPyInput(PyModule, Input, unittest.TestCase):
    pass


class TestCInput(CModule, Input, unittest.TestCase):
    pass


class TestPyOutput(PyModule, Output, unittest.TestCase):
    pass


class TestCOutput(CModule, Output, unittest.TestCase):
    pass


if __name__ == '__main__':
    unittest.main()
