/* fileharbor-python.c - run the Python tools inside the file manager. New file, not in upstream Nemo. */

#include <config.h>

/* Python.h must come first */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "fileharbor-python.h"

static gboolean py_ready = FALSE;
static gboolean py_failed = FALSE;

static char *
tools_dir (void)
{
	const char *env = g_getenv ("FILEHARBOR_TOOLS_DIR");

	return env != NULL ? g_strdup (env) : g_build_filename (NEMO_DATADIR, "tools", NULL);
}

static char *
fetch_error_text (void)
{
	PyObject *type = NULL, *value = NULL, *tb = NULL, *text = NULL;
	char *out;

	PyErr_Fetch (&type, &value, &tb);
	PyErr_NormalizeException (&type, &value, &tb);
	if (value != NULL) {
		text = PyObject_Str (value);
	}
	if (text != NULL && PyUnicode_Check (text)) {
		out = g_strdup (PyUnicode_AsUTF8 (text));
	} else {
		out = g_strdup ("unknown Python error");
	}
	/* the full traceback goes to the log for debugging */
	if (tb != NULL || value != NULL) {
		PyObject *tbmod = PyImport_ImportModule ("traceback");

		if (tbmod != NULL) {
			PyObject *lines = PyObject_CallMethod (tbmod, "format_exception", "OOO",
							       type ? type : Py_None, value ? value : Py_None, tb ? tb : Py_None);

			if (lines != NULL) {
				PyObject *joined = PyUnicode_Join (PyUnicode_FromString (""), lines);

				if (joined != NULL && PyUnicode_Check (joined))
					g_warning ("FileHarbor tool error:\n%s", PyUnicode_AsUTF8 (joined));
				Py_XDECREF (joined);
				Py_DECREF (lines);
			}
			Py_DECREF (tbmod);
		}
	}
	PyErr_Clear ();
	Py_XDECREF (text);
	Py_XDECREF (type);
	Py_XDECREF (value);
	Py_XDECREF (tb);
	return out;
}

static gboolean
ensure_python (GError **error)
{
	char *dir;
	PyObject *path, *item;

	if (py_ready)
		return TRUE;
	if (py_failed) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Python could not be started earlier in this session.");
		return FALSE;
	}
	Py_InitializeEx (0);               /* 0: do not install Python's own signal handlers */
	if (!Py_IsInitialized ()) {
		py_failed = TRUE;
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Python could not be started.");
		return FALSE;
	}
	dir = tools_dir ();
	path = PySys_GetObject ("path");     /* borrowed */
	item = PyUnicode_FromString (dir);
	if (path != NULL && item != NULL)
		PyList_Insert (path, 0, item);
	Py_XDECREF (item);
	g_free (dir);
	/* Python threads (ffmpeg readers, the resizer) need the GIL, which this thread holds right
	 * now. Give it up; every later call takes it with PyGILState_Ensure. */
	PyEval_SaveThread ();
	py_ready = TRUE;
	return TRUE;
}

GtkWidget *
fh_python_create_tool (const char *tool, char **files, GError **error)
{
	PyGILState_STATE gil;
	PyObject *mod = NULL, *fn = NULL, *list = NULL, *res = NULL;
	GtkWidget *widget = NULL;
	char *msg;
	int i;

	if (!ensure_python (error))
		return NULL;

	gil = PyGILState_Ensure ();
	mod = PyImport_ImportModule ("fhtools.embed");
	if (mod == NULL)
		goto fail;
	fn = PyObject_GetAttrString (mod, "create");
	if (fn == NULL)
		goto fail;
	list = PyList_New (0);
	for (i = 0; files != NULL && files[i] != NULL; i++) {
		PyObject *s = PyUnicode_DecodeFSDefault (files[i]);

		if (s != NULL) {
			PyList_Append (list, s);
			Py_DECREF (s);
		}
	}
	res = PyObject_CallFunction (fn, "sO", tool, list);
	if (res == NULL)
		goto fail;
	widget = GTK_WIDGET (PyLong_AsVoidPtr (res));
	if (!GTK_IS_WIDGET (widget)) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "The tool did not return a widget.");
		widget = NULL;
	}
	goto done;
fail:
	msg = fetch_error_text ();
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", msg);
	g_free (msg);
	widget = NULL;
done:
	Py_XDECREF (res);
	Py_XDECREF (list);
	Py_XDECREF (fn);
	Py_XDECREF (mod);
	PyGILState_Release (gil);
	return widget;
}
