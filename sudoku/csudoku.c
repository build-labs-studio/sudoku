/* SuDoKu solving library
 *
 * Improves the performance of the pure Python version
 *
 * Copyright (c) 2008-2014 Aymeric Augustin
 */

#include "csudoku.h"
#include "math.h"
#include "strings.h"

#if PY_MAJOR_VERSION >= 3
#define _PyLong_AsLong PyLong_AsLong
#define _PyLong_CheckExact PyLong_CheckExact
#define _PyLong_FromLong PyLong_FromLong
#define _PyUnicode_FromString PyUnicode_FromString
#define _PyUnicode_AsUTF8Bytes(arg) PyBytes_AsString(PyUnicode_AsUTF8String(arg))
#else
#define _PyLong_AsLong PyInt_AsLong
#define _PyLong_CheckExact PyInt_CheckExact
#define _PyLong_FromLong PyInt_FromLong
#define _PyUnicode_FromString PyString_FromString
#define _PyUnicode_AsUTF8Bytes PyString_AsString
#endif

/******************************************************************************/

/* if self->g is NULL, it will hold a new reference to Py_None after execution */
/* always returns 0 */
static int
SuDoKu__reset(SuDoKu *self)
{
    int i;

    /* resolution */
    for (i = 0; i < 81; i++)
    {
        self->v[i] = 0;
        self->p[i] = 511; /* (1 << 9) - 1 */
        self->c[i] = 9;
        self->q[i] = -1;
    }
    self->q_i = 0;
    self->q_o = 0;
    self->n = 0;

    /* statistics */
    if (self->e)
    {
        Py_XDECREF(self->g); /* the deallocation function is safe */
        Py_INCREF(Py_None);
        self->g = Py_None;
    }

    return 0;
}

/* if t->g is NULL, it will hold a new reference to Py_None after execution */
/* always returns 0 */
static int
SuDoKu__copy(SuDoKu *self, SuDoKu *t)
{
    memcpy(t->o, self->o, 81 * sizeof(unsigned char));
    memcpy(t->v, self->v, 81 * sizeof(unsigned char));
    memcpy(t->p, self->p, 81 * sizeof(unsigned short int));
    memcpy(t->c, self->c, 81 * sizeof(unsigned char));
    memcpy(t->q, self->q, 81 * sizeof(unsigned char));
    t->q_i = self->q_i;
    t->q_o = self->q_o;
    t->n = self->n;

    /* self->g is not copied; that does not matter for the algorithm */
    if (self->e)
    {
        Py_XDECREF(t->g); /* the deallocation function is safe */
        Py_INCREF(Py_None);
        t->g = Py_None;
    }
    t->e = self->e;
#ifdef DEBUG
    t->d = self->d;
#endif

    return 0;
}

/* returns -1 on errors */
/* returns -2 if a Contradiction exception is raised */
/* self->g must not be NULL if self->e */
static int
SuDoKu__mark(SuDoKu *self, int i, unsigned char n)
{
    int *rel, j, r;

    if (self->v[i] == n)
    {
        return 0;
    }

    if (!((self->p[i] >> (n - 1)) & 1))
    {
#ifdef DEBUG
        if (self->d)
        {
            PySys_WriteStdout("    Attempt to assign %d at (%d, %d) which is forbidden\n", n, i / 9, i % 9);
        }
#endif
        if (self->e)
        {
            Py_DECREF(self->g);
            self->g = Py_BuildValue("ic", self->n, '-');
            if (self->g == NULL)
            {
                return -1;
            }
        }

        PyErr_SetNone(SuDoKu_Contradiction);
        return -2;
    }

    self->v[i] = n;
    self->n += 1;
    self->p[i] = 0;
    self->c[i] = 0;

    rel = SuDoKu__relations[i];
    for (j = 0; j < 20; j++)
    {
        r = SuDoKu__eliminate(self, rel[j], n);
        if (r < 0)
        {
            return r;
        }
    }

    while (self->q_o < self->q_i)
    {
        i = (int)self->q[self->q_o];
        self->q_o += 1;
        /* there is only one non-zero bit if self->p[i] at this point,
           ffs or fls could be used indifferently */
        n = (unsigned char)ffs(self->p[i]);
        r = SuDoKu__mark(self, i, n);
        if (r < 0)
        {
            return r;
        }
    }
    return 0;
}

/* returns -1 on errors */
/* returns -2 if a Contradiction exception is raised */
/* self->g must not be NULL if self->e */
static int
SuDoKu__eliminate(SuDoKu *self, int i, unsigned char n)
{
    if ((self->p[i] >> (n - 1)) & 1)
    {
        self->p[i] ^= 1 << (n - 1);
        self->c[i] -= 1;
        if (self->c[i] == 0)
        {
#ifdef DEBUG
            if (self->d)
            {
                PySys_WriteStdout("    Impossibility at (%d, %d), search depth = %d\n", i / 9, i % 9, self->n);
            }
#endif
            if (self->e)
            {
                Py_DECREF(self->g);
                self->g = Py_BuildValue("ic", self->n, '-');
                if (self->g == NULL)
                {
                    return -1;
                }
            }
            PyErr_SetNone(SuDoKu_Contradiction);
            return -2;
        }
        else if (self->c[i] == 1)
        {
            self->q[self->q_i] = (unsigned char)i;
            self->q_i += 1;
        }
    }
    return 0;
}

/* returns -1 if the grid is complete, a non-negative cell number otherwise */
static int
SuDoKu__search_min(SuDoKu *self)
{
    int i;
    unsigned char im, cm;

    im = -1;
    cm = 10;
    for (i = 0; i < 81; i++)
    {
        if (self->v[i] == 0 && self->c[i] < cm)
        {
            im = i;
            cm = self->c[i];
        }
    }
    return (int)im;
}

/* returns -1 on errors */
/* caller will receive ownership of a new reference to self->g
   and to *res; existing references will be discarded */
/* self->g may be NULL and *res must be NULL */
static int
SuDoKu__resolve_aux(SuDoKu *self, SuDoKu *ws, PyObject **res)
{
    PyObject *sg = NULL, *sres = NULL, *tmp = NULL;
    SuDoKu *t;
    int i, r;
    unsigned char n;
#ifdef DEBUG
    char output[82];
#endif

    *res = PyList_New(0);
    if (*res == NULL)
    {
        return -1;
    }

    if (self->n == 81)
    {
#ifdef DEBUG
        if (self->d)
        {
            SuDoKu__to_string(self, self->v, output);
            PySys_WriteStdout("    Found a solution: %s\n", output);
        }
#endif
        if (self->e)
        {
            Py_XDECREF(self->g);
            self->g = Py_BuildValue("ic", self->n, '+');
            if (self->g == NULL)
            {
                return -1;
            }
        }
        sres = SuDoKu_get2darray(self->v);
        if (PyList_Append(*res, sres) < 0)
        {
            Py_DECREF(sres);
            return -1;
        }
        Py_DECREF(sres);
        return 0;
    }

    if (self->e)
    {
        sg = PyList_New(0);
        if (sg == NULL)
        {
            return -1;
        }
        Py_XDECREF(self->g);
        /* reference to sg is stored without incrementing its refcounter */
        self->g = Py_BuildValue("iN", self->n, sg);
        if (self->g == NULL)
        {
            Py_DECREF(sg);
            return -1;
        }
    }

    t = &ws[self->n];
    i = SuDoKu__search_min(self);

    for (n = 1; n < 10; n++)
    {
        if ((self->p[i] >> (n - 1)) & 1)
        {
#ifdef DEBUG
            if (self->d)
            {
                PySys_WriteStdout("Trying %d at (%d, %d), search depth = %d\n", n, i / 9, i % 9, self->n);
            }
#endif
            /* will allocate an owned reference t->g if and only if self->e */
            SuDoKu__copy(self, t); /* no error codes */
            /* may reallocate t->g if and only if self->e */
            r = SuDoKu__mark(t, i, n);
            if (r == -2)
            {
                PyErr_Clear();
                if (self->e)
                {
                    /* saves a reference to t->g */
                    if (PyList_Append(sg, t->g) < 0)
                    {
                        Py_CLEAR(t->g);
                        return -1;
                    }
                    Py_CLEAR(t->g);
                }
                continue;
            }
            else if (r < 0)
            {
                /* this may happen if t->g could not be allocated */
                Py_XDECREF(t->g);
                return r;
            }

            /* owned references t->g and sres must be decref'd on exit */
            if (SuDoKu__resolve_aux(t, ws, &sres) < 0)
            {
                Py_XDECREF(t->g);
                Py_XDECREF(sres);
                return -1;
            }
            if (!PyList_CheckExact(sres))
            {
                PyErr_SetString(PyExc_SystemError,
                    "expected a list in sres after SuDoKu__resolve_aux");
                Py_XDECREF(t->g);
                Py_XDECREF(sres);
                return -1;
            }

            tmp = *res;
            *res = PySequence_Concat(*res, sres);
            Py_DECREF(tmp);
            if (*res == NULL)
            {
                Py_XDECREF(t->g);
                Py_XDECREF(sres);
                return -1;
            }
            Py_CLEAR(sres);

            if (self->e)
            {
                if (PyList_Append(sg, t->g) < 0)
                {
                    Py_XDECREF(t->g);
                    return -1;
                }
                Py_CLEAR(t->g);
            }
        }
    }

    return 0;
}

#ifdef DEBUG
static int
SuDoKu__print_graph(PyObject *g)
{
    char p[163];

    memset(p, ' ', 162);
    p[162] = '\0';

    return SuDoKu__print_graph_aux(g, p, 0);
}

static int
SuDoKu__print_graph_aux(PyObject *g, char *p, int pl)
{
    int g0;
    PyObject *g1;
    char *c;
    Py_ssize_t i;
    char fmt[15];

    if (!PyArg_ParseTuple(g, "iO", &g0, &g1))
    {
        return -1;
    }

    if (PyList_CheckExact(g1))
    {
        PyOS_snprintf(fmt, 15, "%%.%ds%%02d\n", pl);
        PySys_WriteStdout(fmt, p, g0);
        for (i = 0; i < PyList_Size(g1); i++)
        {
            if (SuDoKu__print_graph_aux(PyList_GET_ITEM(g1, i), p, pl + 2) < 0)
            {
                return -1;
            }
        }
    }
    else
    {
        PyOS_snprintf(fmt, 15, "%%.%ds%%02d %%s\n", pl);
        c = _PyUnicode_AsUTF8Bytes(g1);
        if (c == NULL)
        {
            return -1;
        }
        PySys_WriteStdout(fmt, p, g0, c);
    }
    return 0;
}
#endif

static int
SuDoKu__graph_len(PyObject *g)
{
    return SuDoKu__graph_len_aux(g, 0);
}

static int
SuDoKu__graph_len_aux(PyObject *g, int d)
{
    int g0, l, sl;
    PyObject *g1;
    Py_ssize_t i;

    if (!PyArg_ParseTuple(g, "iO", &g0, &g1))
    {
        return -1;
    }

    l = g0 - d;
    if (PyList_CheckExact(g1))
    {
        for (i = 0; i < PyList_Size(g1); i++)
        {
            sl = SuDoKu__graph_len_aux(PyList_GET_ITEM(g1, i), g0);
            if (sl < 0)
            {
                return sl;
            }
            l += sl;
        }
    }
    return l;
}

static int
SuDoKu__graph_forks(PyObject *g)
{
    int g0, f, sf;
    PyObject *g1;
    Py_ssize_t i;

    if (!PyArg_ParseTuple(g, "iO", &g0, &g1))
    {
        return -1;
    }

    f = 0;
    if (PyList_CheckExact(g1))
    {
        for (i = 0; i < PyList_Size(g1); i++)
        {
            sf = SuDoKu__graph_forks(PyList_GET_ITEM(g1, i));
            if (sf < 0)
            {
                return sf;
            }
            f += sf + 1;
        }
    }
    return f;
}

static int
SuDoKu__unique_sol_aux(SuDoKu *self, SuDoKu *ws)
{
    SuDoKu *t;
    int i, count, scount;
    unsigned char n;

    if (self->n == 81)
    {
        return 1;
    }

    t = &ws[self->n];
    i = SuDoKu__search_min(self);

    count = 0;
    for (n = 1; n < 10; n++)
    {
        if ((self->p[i] >> (n - 1)) & 1)
        {
            SuDoKu__copy(self, t); /* no error codes */
            if (SuDoKu__mark(t, i, n) == -2)
            {
                PyErr_Clear();
                continue;
            }
            scount = SuDoKu__unique_sol_aux(t, ws);
            if (scount < 0)
            {
                return scount;
            }
            count += scount;
            if (count > 1)
            {
                /* -2 means that several solutions were found */
                return -2;
            }
        }
    }
    return count;
}

static int
SuDoKu__unique_sol(SuDoKu *self)
{
    int i;
    /* workspace - actual PyObjects are not needed, C structs are enough */
    SuDoKu ws[81];

    SuDoKu__reset(self);

    for (i = 0; i < 81; i++)
    {
        if (self->o[i] > 0)
        {
            if (SuDoKu__mark(self, i, self->o[i]) < 0)
            {
                return -1;
            }
        }
    }

    return SuDoKu__unique_sol_aux(self, ws);
}

static int
SuDoKu__from_string(SuDoKu *self, const char *s, const int l)
{
    int i, k;
    char c;
    char err_msg[32];

    i = 0;
    for (k = 0; k < l; k++)
    {
        c = s[k];
        if (c == '\n' || c == '\r')
        {
            /* ignore non-significant characters */
            continue;
        }
        else if (i >= 81)
        {
            /* must be checked here to allow trailing whitespace */
            break;
        }
        else if (c >= '1' && c <= '9')
        {
            self->o[i] = (unsigned char)c - (unsigned char)'0';
        }
        else if (c == '_' || c == '-' || c == ' ' || c == '.' || c == '0')
        {
            /* nothing to do */
        }
        else
        {
            PyOS_snprintf(err_msg, 32, "Invalid character: %c.", c);
            PyErr_SetString(PyExc_ValueError, err_msg);
            return -1;
        }
        i += 1;
    }
    if (i < 81)
    {
        PyErr_SetString(PyExc_ValueError, "Bad input: not enough data.");
        return -1;
    }
    if (k < l)
    {
        PyErr_SetString(PyExc_ValueError, "Bad input: too much data.");
        return -1;
    }
    return 0;
}

static int
SuDoKu__to_console(SuDoKu *self, const unsigned char *v, char *s)
{
    int i, j, k;
    char *sep = " --- --- --- --- --- --- --- --- --- \n";
    char *lin = "|   |   |   |   |   |   |   |   |   |\n";
    char *p1, *p2;

    p1 = stpcpy(s, sep);
    for (i = 0; i < 9; i++)
    {
        p2 = stpcpy(p1, lin);
        for (j = 0; j < 9; j++)
        {
            k = v[9 * i + j];
            if (k == 0)
            {
                /* nothing to do */
            }
            else if (k >= 1 && k <= 9)
            {
                p1[4 * j + 2] = (char)(k + (unsigned char)'0');
            }
            else
            {
                PyErr_SetString(PyExc_ValueError, "Invalid value in grid.");
                return -1;
            }
        }
        p1 = stpcpy(p2, sep);
    }
    p1[-1] = '\0'; /* remove the last \n */
    return 0;
}

static int
SuDoKu__to_html(SuDoKu *self, const unsigned char *v, char *s)
{
    int i, j, k;
    char *p;

    p = stpcpy(s, "<table class=\"sudoku\">");
    for (i = 0; i < 9; i++)
    {
        p = stpcpy(p, "<tr>");
        for (j = 0; j < 9; j++)
        {
            p = stpcpy(p, "<td>");
            k = v[9 * i + j];
            if (k == 0)
            {
                p = stpcpy(p, "&nbsp;");
            }
            else if (k >= 1 && k <= 9)
            {
                p[0] = (char)(k + (unsigned char)'0');
                p[1] = '\0';
                p = &p[1];
            }
            else
            {
                PyErr_SetString(PyExc_ValueError, "Invalid value in grid.");
                return -1;
            }
            p = stpcpy(p, "</td>");
        }
        p = stpcpy(p, "</tr>");
    }
    p = stpcpy(p, "</table>");
    return 0;
}

static int
SuDoKu__to_string(SuDoKu *self, const unsigned char *v, char *s)
{
    int i;

    for (i = 0; i < 81; i++)
    {
        if (v[i] == 0)
        {
            s[i] = '_';
        }
        else if (v[i] >= 1 && v[i] <= 9)
        {
            s[i] = (char)(v[i] + (unsigned char)'0');
        }
        else
        {
            PyErr_SetString(PyExc_ValueError, "Invalid value in grid.");
            return -1;
        }
    }
    s[81] = '\0';
    return 0;
}


/******************************************************************************/

#ifdef DEBUG
static PyObject*
SuDoKu_debug(SuDoKu *self, PyObject *args)
{
    const char *msg = NULL;

    if (!PyArg_ParseTuple(args, "s", &msg))
    {
        return NULL;
    }

    PySys_WriteStdout("%s\n", msg);

    Py_RETURN_NONE;
}
#endif

static PyObject*
SuDoKu_resolve(SuDoKu *self)
{
    int i;
    /* workspace - actual PyObjects are not needed, except for the graph */
    SuDoKu ws[81];
    PyObject *results = NULL;

    /* step 0 */
    SuDoKu__reset(self);
    if (self->e)
    {
        /* make sure XDECREF is safe on ws[i].g */
        for (i = 0; i < 81; i++)
        {
            ws[i].g = NULL;
        }
    }

    /* step 1 */
    for (i = 0; i < 81; i++)
    {
        if (self->o[i] > 0)
        {
            if (SuDoKu__mark(self, i, self->o[i]) < 0)
            {
                return NULL;
            }
        }
    }

    /* step 2 */
    if (SuDoKu__resolve_aux(self, ws, &results) < 0)
    {
        return NULL;
    }
    if (!PyList_CheckExact(results))
    {
        PyErr_SetString(PyExc_SystemError,
            "expected a list in sres after SuDoKu__resolve_aux");
        return NULL;
    }

    return results;
}

static PyObject*
SuDoKu_estimate(SuDoKu *self)
{
    int l, f;

    if (!self->e || self->g == NULL)
    {
        Py_RETURN_NONE;
    }
#ifdef DEBUG
    if (self->d)
    {
        SuDoKu__print_graph(self->g);
    }
#endif

    l = SuDoKu__graph_len(self->g);
    if (l < 0)
    {
        return NULL;
    }
    f = SuDoKu__graph_forks(self->g);
    if (f < 0)
    {
        return NULL;
    }
    return Py_BuildValue("di", log((double)l / 81.0) + 1.0, f);
}

static PyObject*
SuDoKu_generate(SuDoKu *self)
{
    int e, i, j, k, m, r, order[81];
    unsigned char n;
#ifdef DEBUG
    int count;
#endif
    /* step 0 */
    SuDoKu__reset(self);
    e = self->e;
    self->e = 0;

    /* step 1 */
#ifdef DEBUG
    if (self->d)
    {
        PySys_WriteStdout("Generating a random grid...\n");
    }
    count = 0;
#endif
    srandomdev();
    /* shuffle order, this actually works and can be proved by recurrence */
    for (i = 0; i < 81; i++)
    {
        j = random() % (i + 1);
        order[i] = order[j];
        order[j] = i;
    }
    while (1)
    {
#ifdef DEBUG
        count += 1;
#endif
        SuDoKu__reset(self);
        r = 1;
        for (i = 0; i < 81; i++)
        {
            if (self->v[order[i]] == 0)
            {
                /* choose a random possibility at position order[i] */
                j = self->p[order[i]];
                m = (random() % (int)self->c[order[i]]);
                for (k = 0; k < m; k++)
                {
                    /* set least significant bit to 0 */
                    j &= j - 1;
                }
                n = (unsigned char)ffs(j);
                if (SuDoKu__mark(self, order[i], n) == -2)
                {
                    PyErr_Clear();
                    r = 0;
                    break;
                }
            }
        }
        if (r)
        {
            break;
        }
    }
#ifdef DEBUG
    if (self->d)
    {
        PySys_WriteStdout("    Found a grid after %d tries.\n", count);
    }
#endif

    /* step 2 */
#ifdef DEBUG
    if (self->d)
    {
        PySys_WriteStdout("Minimizing problem...\n");
    }
#endif
    memcpy(self->o, self->v, 81 * sizeof(unsigned char));
    /* shuffle order */
    for (i = 0; i < 81; i++)
    {
        j = random() % (i + 1);
        order[i] = order[j];
        order[j] = i;
    }
    for (i = 0; i < 81; i++)
    {
        n = self->o[order[i]];
        self->o[order[i]] = 0;
        r = SuDoKu__unique_sol(self);
        if (r >= 0)
        {
#ifdef DEBUG
            if (self->d)
            {
                PySys_WriteStdout("    Removing %d at (%d, %d)\n", n, order[i] / 9, order[i] % 9);
            }
#endif
        }
        else if (r == -2)
        {
#ifdef DEBUG
            if (self->d)
            {
                PySys_WriteStdout("    Keeping %d at (%d, %d)\n", n, order[i] / 9, order[i] % 9);
            }
#endif
            self->o[order[i]] = n;
        }
        else
        {
            return NULL;
        }
    }
#ifdef DEBUG
    if (self->d)
    {
        PySys_WriteStdout("    Done.\n");
    }
#endif

    self->e = e;
    return SuDoKu_get2darray(self->o);
}

static PyObject*
SuDoKu_from_string(SuDoKu *self, PyObject *args)
{
    int l = 0;
    const char *s = NULL;

    if (!PyArg_ParseTuple(args, "s#", &s, &l))
    {
        return NULL;
    }

    if (SuDoKu__from_string(self, s, l) < 0)
    {
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject*
SuDoKu_to_string(SuDoKu *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"format", "values", NULL};
    const char *format = "string";
    PyObject *values = NULL;
    unsigned char cvalues[81];
    int coutlen;
    static int (*formatter)(SuDoKu *self, const unsigned char *v, char *s);
    char err_msg[32];
    char *coutput = NULL;
    PyObject *output = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|sO", kwlist, &format, &values))
    {
        return NULL;
    }

    if (values == NULL)
    {
        memcpy(cvalues, self->o, 81 * sizeof(unsigned char));
    }
    else
    {
        if (SuDoKu_set2darray(cvalues, values) < 0)
        {
            return NULL;
        }
    }

    if (strcmp(format, "console") == 0)
    {
        coutlen = 19 /* lines */ * 38 /* columns */ + 1 /* \0 */;
        formatter = SuDoKu__to_console;
    }
    else if (strcmp(format, "html") == 0)
    {
        coutlen = 9 /* rows */ * 144 /* max row len */ + 30 + 1 /* \0 */;
        formatter = SuDoKu__to_html;
    }
    else if (strcmp(format, "string") == 0)
    {
        coutlen = 81 /* characters */ + 1 /* \0 */;
        formatter = SuDoKu__to_string;
    }
    else
    {
        PyOS_snprintf(err_msg, 32, "Invalid format: %s.", format);
        PyErr_SetString(PyExc_ValueError, err_msg);
        return NULL;
    }

    coutput = (char *)calloc(coutlen, sizeof(char));
    if (coutput == NULL)
    {
        return PyErr_NoMemory();
    }

    if (formatter(self, cvalues, coutput) >= 0)
    {
        /* can be NULL, will be returned after free-ing coutput */
        output = _PyUnicode_FromString(coutput);
    }

    free(coutput);
    return output;
}

/******************************************************************************/

static PyObject*
SuDoKu_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    SuDoKu* self;

    self = (SuDoKu*)type->tp_alloc(type, 0);

    if (self != NULL)
    {
        self->e = 1;
        self->g = NULL;
#ifdef DEBUG
        self->d = 0;
#endif
    }

    return (PyObject*)self;
}

static int
SuDoKu_init(SuDoKu *self, PyObject *args, PyObject *kwds)
{
    const char *problem = NULL;
    int len = 0;

#ifdef DEBUG
    static char *kwlist[] = {"problem", "estimate", "debug", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|s#bb", kwlist,
                                     &problem, &len, &self->e, &self->d))
#else
    static char *kwlist[] = {"problem", "estimate", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|s#b", kwlist,
                                     &problem, &len, &self->e))
#endif
    {
        return -1;
    }

    if (problem != NULL)
    {
        if (SuDoKu__from_string(self, problem, len) < 0)
        {
            return -1;
        }
    }

    return 0;
}

static PyObject*
SuDoKu_str(SuDoKu *self)
{
    char *coutput;
    PyObject *output = NULL;

    coutput = (char *)calloc(82, sizeof(char));
    if (coutput == NULL)
    {
        return PyErr_NoMemory();
    }

    if (SuDoKu__to_string(self, self->o, coutput) < 0)
    {
        free(coutput);
        return NULL;
    }

    /* can be NULL, will be returned after free-ing coutput */
    output = _PyUnicode_FromString(coutput);
    free(coutput);
    return output;
}

static PyObject*
SuDoKu_repr(SuDoKu *self)
{
    char *coutput, *p;
    PyObject *output = NULL;
    int i;
    char first = 1, show_problem = 0;

    coutput = (char *)calloc(135, sizeof(char));
    if (coutput == NULL)
    {
        return PyErr_NoMemory();
    }

    p = stpcpy(coutput, "sudoku.SuDoKu(");

    for (i = 0; i < 81; i++)
    {
        if (self->o[i])
        {
            show_problem = 1;
            break;
        }
    }
    if (show_problem)
    {
        first = 0;
        p = stpcpy(p, "problem=\"");
        if (SuDoKu__to_string(self, self->o, p) < 0)
        {
            free(coutput);
            return NULL;
        }
        p += 81 * sizeof(char);
        p = stpcpy(p, "\"");
    }

    if (!self->e)
    {
        if (first)
        {
            first = 0;
        }
        else
        {
            p = stpcpy(p, ", ");
        }
        p = stpcpy(p, "estimate=False");
    }
#ifdef DEBUG
    if (self->d)
    {
        if (first)
        {
            first = 0;
        }
        else
        {
            p = stpcpy(p, ", ");
        }
        p = stpcpy(p, "debug=True");
    }
#endif
    p = stpcpy(p, ")");
    /* can be NULL, will be returned after free-ing coutput */
    output = _PyUnicode_FromString(coutput);
    free(coutput);
    return output;
}

static int
SuDoKu_traverse(SuDoKu *self, visitproc visit, void *arg)
{
    Py_VISIT(self->g);
    return 0;
}

static int
SuDoKu_clear(SuDoKu *self)
{
    Py_CLEAR(self->g);
    return 0;
}

static void
SuDoKu_dealloc(SuDoKu *self)
{
    Py_CLEAR(self->g);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

/* returns a new reference */
static PyObject *
SuDoKu_get2darray(unsigned char *a)
{
    PyObject *v, *r, *c; /* values, row, cell */
    int i, j;

    v = PyList_New(9);
    if (v == NULL)
    {
        return NULL;
    }
    for (i = 0; i < 9; i++)
    {
        r = PyList_New(9);
        if (r == NULL)
        {
            Py_DECREF(v);
            return NULL;
        }
        for (j = 0; j < 9; j++)
        {
            c = _PyLong_FromLong((long)a[9 * i + j]);
            if (c == NULL)
            {
                Py_DECREF(v);
                Py_DECREF(r);
                return NULL;
            }
            PyList_SET_ITEM(r, j, c);
        }
        PyList_SET_ITEM(v, i, r);
    }
    return v;
}

static int
SuDoKu_set2darray(unsigned char *a, PyObject *v)
{
    PyObject *r, *c; /* row, cell */
    int i, j;

    if (v == NULL)
    {
        return -1;
    }
    else if (!PyList_CheckExact(v) || PyList_Size(v) != 9)
    {
        PyErr_SetString(PyExc_ValueError,
            "SuDoKu_set2darray expects a grid with 9 rows");
        return -1;
    }
    for (i = 0; i < 9; i++)
    {
        r = PyList_GetItem(v, i);
        if (r == NULL)
        {
            return -1;
        }
        else if (!PyList_CheckExact(r) || PyList_Size(r) != 9)
        {
            PyErr_SetString(PyExc_ValueError,
                "SuDoKu_set2darray expects a grid with 9 columns");
            return -1;
        }
        for (j = 0; j < 9; j++)
        {
            c = PyList_GetItem(r, j);
            if (c == NULL)
            {
                return -1;
            }
            else if (!_PyLong_CheckExact(c))
            {
                PyErr_SetString(PyExc_ValueError,
                    "SuDoKu_set2darray expects a grid of integers");
                return -1;
            }
            a[9 * i + j] = (unsigned char)_PyLong_AsLong(c);
        }
    }
    return 0;
}

/* returns a new reference */
static PyObject *
SuDoKu_getv(SuDoKu *self, void *closure)
{
    return SuDoKu_get2darray(self->v);
}

static int
SuDoKu_setv(SuDoKu *self, PyObject *v, void *closure)
{
    return SuDoKu_set2darray(self->v, v);
}

/* returns a new reference */
static PyObject *
SuDoKu_geto(SuDoKu *self, void *closure)
{
    return SuDoKu_get2darray(self->o);
}

static int
SuDoKu_seto(SuDoKu *self, PyObject *v, void *closure)
{
    return SuDoKu_set2darray(self->o, v);
}

/******************************************************************************/

static PyObject *
init_csudoku(void)
{
    PyObject *d, *m;

    if (PyType_Ready(&SuDoKuType) < 0) return NULL;

    /* create SuDoKu_Contradiction */
    d = Py_BuildValue("{ss}", "__doc__",
                              "Contradiction in input, no solution exists.");
    if (d == NULL) return NULL;

    SuDoKu_Contradiction = PyErr_NewException("sudoku.csudoku.Contradiction", NULL, d);
    Py_DECREF(d);
    if (SuDoKu_Contradiction == NULL) return NULL;

    /* initialize module */
#if PY_MAJOR_VERSION >= 3
    m = PyModule_Create(&moduledef);
#else
    m = Py_InitModule3("sudoku.csudoku", module_methods,
                       "SuDoKu generator and solver (C implementation).");
#endif
    if (m == NULL) return NULL;

    /* insert a new reference to objects in the module dictionnary */
    Py_INCREF(&SuDoKuType);
    if (PyModule_AddObject(m, "SuDoKu", (PyObject *)&SuDoKuType) < 0)
    {
        Py_DECREF(&SuDoKuType);
        return NULL;
    }
    Py_INCREF(SuDoKu_Contradiction);
    if (PyModule_AddObject(m, "Contradiction", SuDoKu_Contradiction) < 0)
    {
        Py_DECREF(SuDoKu_Contradiction);
        return NULL;
    }

    return m;
}

#if PY_MAJOR_VERSION >= 3
PyMODINIT_FUNC PyInit_csudoku(void)
{
    return init_csudoku();
}
#else
PyMODINIT_FUNC initcsudoku(void)
{
    init_csudoku();
}
#endif
