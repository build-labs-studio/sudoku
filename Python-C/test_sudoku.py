#!/usr/bin/env python
# Copyright (C) 2008-2009 Aymeric Augustin


import re, unittest
from sudoku import *


sudokus = (
    ('__6___2_49_1________853_6____2__5______8_7___________675___4__8____8_5_3___2_9___',
     '536918274921476835478532619392645781615897342847123956759364128264781593183259467'),
    ('_7_5_____1_6____7___2_1_6_4_______984____8_5__1__23___6_9__2____________3___74__9',
     '874536921136249875952817634267451398493768152518923746649382517721695483385174269'),
    ('__6_9___4_____2__8__754__6___893_2__3________9___2___5______5__2___18____61_____7',
     '526891734143672958897543162658937241312485679974126385789264513235718496461359827'),
    ('__7_26__8__4__8______17___2_1__92__5______6_17______3_____5____48_7_____62_____54',
     '137426598294538167568179342813692475952347681746815239379254816485761923621983754'),
    ('__7_1_______3_28__6______1_85__________________1_7__951_9__4__6___7_9_5__26_8____',
     '287916534915342867643857912852491673794635128361278495179524386438769251526183749'),
)


class TestResolutionAndEstimation(unittest.TestCase):

    def testResolve(self):
        forks, estimations = [], []
        for problem, solution in sudokus:
            s = SuDoKu()
            s.from_string(problem)
            solutions = s.resolve()
            self.assertEqual(len(solutions), 1)
            self.assertEqual(s.to_string(solutions[0]), solution)
            forks.append(s.graph_forks())
            estimations.append(s.estimate())
        self.assertEqual(estimations, sorted(estimations))
        self.assertEqual(forks, sorted(forks))

    def testRedundancyIsAllowedInProblems(self):
        problem, solution = sudokus[0]
        s = SuDoKu(problem[:40] + solution[40:])
        self.assertEqual(s.to_string(s.resolve()[0]), solution)

    def testContradictionsAreDetectedInProblems(self):
        problem, solution = sudokus[0]
        s = SuDoKu('66' + problem[2:])
        self.assertRaises(Contradiction, s.resolve)


class TestGeneration(unittest.TestCase):

    def testGenerate(self):
        s = SuDoKu()
        s.generate()
        solutions = s.resolve()
        self.assertEqual(len(solutions), 1)


class TestInput(unittest.TestCase):

    def setUp(self):
        self.problem = sudokus[0][0]

    def testFromStringSavesValuesInOriginalArray(self):
        s = SuDoKu(self.problem)
        self.assertEqual(s.o[0][0], 0)
        self.assertEqual(s.o[0][2], 6)
        self.assertEqual(s.o[2][4], 3)
        self.assertEqual(s.o[8][5], 9)

    def testFromStringAcceptsLineBreaks(self):
        s = SuDoKu(self.problem)
        lines = [self.problem[i:i+9] for i in range(0, 81, 9)]
        t = SuDoKu('\n'.join(lines))
        self.assertEqual(t.o, s.o)
        t = SuDoKu('\r\n'.join(lines))
        self.assertEqual(t.o, s.o)
        t = SuDoKu('\r'.join(lines))
        self.assertEqual(t.o, s.o)

    def testFromStringAcceptsSeveralCharactersToMarkEmptyCells(self):
        s = SuDoKu(self.problem)
        altered = (self.problem[:20].replace('_', '-')
                 + self.problem[20:40].replace('_', ' ')
                 + self.problem[40:60].replace('_', '.')
                 + self.problem[60:].replace('_', '0')
                  )
        t = SuDoKu(altered)
        self.assertEqual(t.o, s.o)

    def testFromStringRejectsOtherCharacters(self):
        for i in range(127):
            if chr(i) in '123456789\r\n_- .0':
                continue
            altered = chr(i) + self.problem[1:]
            self.assertRaises(ValueError, SuDoKu, altered)

    def testFromStringChecksInputLength(self):
        self.assertRaises(ValueError, SuDoKu, self.problem + '_')
        self.assertRaises(ValueError, SuDoKu, self.problem[:-1])

    def testFromStringDoesNotEnforceRules(self):
        s = SuDoKu('66' + self.problem[2:])
        self.assertEqual(s.o[0][0], s.o[0][1])


class TestOutput(unittest.TestCase):

    def setUp(self):
        self.problem = sudokus[2][0]
        self.s = SuDoKu(self.problem)

    def testToConsole(self):
        output = re.sub(r'[^1-9_]+', '',
                        self.s.to_console().replace('   ', ' _ '))
        self.assertEqual(output, self.problem)

    def testToHtml(self):
        output = re.sub(r'<.+?>', '', self.s.to_html()).replace('&nbsp;', '_')
        self.assertEqual(output, self.problem)

    def testToString(self):
        output = self.s.to_string()
        self.assertEqual(output, self.problem)


if __name__ == '__main__':
    unittest.main()