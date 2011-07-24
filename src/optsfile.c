/*
    roxterm - VTE/GTK terminal emulator with tabs
    Copyright (C) 2004-2011 Tony Houghton <h@realh.co.uk>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/


#include "defns.h"

#include <errno.h>
#include <stdarg.h>
#include <string.h>

#ifndef HAVE_G_MKDIR_WITH_PARENTS
#include <sys/stat.h>
#include <sys/types.h>
#endif

#include "dlg.h"
#include "globalopts.h"
#include "optsfile.h"

#ifndef HAVE_G_FILE_SET_CONTENTS
extern gboolean g_file_set_contents(const gchar * filename,
					const gchar * contents, gssize length, GError ** error);
#endif

static char **options_pathv = NULL;

/* Append 2 NULL-terminated arrays of strings, avoiding duplicates; the strings
 * themselves are copied */
static char **options_file_join_strvs(const char * const *v1,
		const char * const *v2)
{
	size_t n, v_end;
	char **result;

	for (n = 0; v1 && v1[n]; ++n);
	v_end = n;
	for (n = 0; v2 && v2[n]; ++n);
	if (n == 0 && v_end == 0)
		return NULL;
	result = g_new(char *, v_end + n + 1);
	for (n = 0; n < v_end; ++n)
	{
		result[n] = g_strdup(v1[n]);
	}
	for (n = 0; v2 && v2[n]; ++n)
	{
		size_t m;

		for (m = 0; m < v_end; ++m)
		{
			if (!strcmp(v2[n], result[m]))
			{
				break;
			}
		}
		if (m == v_end)
		{
			result[v_end++] = g_strdup(v2[n]);
		}
	}
	result[v_end] = NULL;
	/* Could realloc array in case it's larger than necessary due to
	 * duplicates, but I don't think it's worth the hassle */
	return result;
}

#if 0
static void options_file_debug_options_pathv(void)
{
	int n;

	if (!options_pathv)
	{
		g_debug("options_pathv NULL");
		return;
	}
	g_debug("options_pathv contains:");
	for (n = 0; options_pathv[n]; ++n)
	{
		g_debug(options_pathv[n]);
	}
}
#endif

static void options_file_append_leaf_to_pathv(char **pathv, const char *leaf)
{
	int n;

	g_return_if_fail(pathv);
	for (n = 0; pathv[n]; ++n)
	{
		char *old_path = pathv[n];

		pathv[n] = g_build_filename(old_path, leaf, NULL);
		g_free(old_path);
	}
}

/* Frees s */
static void options_file_prepend_str_to_v(char *s)
{
	char **old_pathv;
	const char * const str_v[2] = { s, NULL };

	old_pathv = options_pathv;
	options_pathv = options_file_join_strvs(str_v,
			(const char * const *) old_pathv);
	g_free(s);
	g_strfreev(old_pathv);
}

static void options_file_init_paths(void)
{
	if (options_pathv)
		return;

	/* Add in reverse order of priority because we prepend */

	/* XDG system conf dirs */
	options_pathv = options_file_join_strvs(g_get_system_config_dirs(), NULL);
	if (options_pathv)
		options_file_append_leaf_to_pathv(options_pathv, ROXTERM_LEAF_DIR);

	/* AppDir or datadir */
	if (global_options_appdir)
	{
		options_file_prepend_str_to_v(g_build_filename(global_options_appdir,
					"Config", NULL));
	}
	else
	{
		options_file_prepend_str_to_v(g_build_filename(PKG_DATA_DIR,
					"Config", NULL));
	}

	/* XDG home conf dir (highest priority) */
	options_file_prepend_str_to_v(g_build_filename(g_get_user_config_dir(),
				ROXTERM_LEAF_DIR, NULL));
}

const char * const *options_file_get_pathv(void)
{
	if (!options_pathv)
		options_file_init_paths();
	return (const char * const *) options_pathv;
}

/* Finds first element in pathv containing an object called leafname and
 * returns full filename, or NULL if not found */
static char *options_file_filename_from_pathv(const char *leafname,
		char **pathv)
{
	int n;

	for (n = 0; pathv[n]; ++n)
	{
		char *filename = g_build_filename(pathv[n], leafname, NULL);

		if (g_file_test(filename, G_FILE_TEST_EXISTS))
			return filename;
		g_free(filename);
	}
	return NULL;
}

static char *build_filename_from_valist(const char *first_element, va_list ap)
{
	const char *s;
	char *result = g_strdup(first_element);

	while ((s = va_arg(ap, const char *)) != NULL)
	{
		char *old = result;

		result = g_build_filename(old, s, NULL);
		g_free(old);
	}
	return result;
}

char *options_file_build_filename(const char *first_element, ...)
{
	char *filename;
	char *result;
	va_list ap;

	va_start(ap, first_element);
	filename = build_filename_from_valist(first_element, ap);
	va_end(ap);
	result = options_file_filename_from_pathv(filename, options_pathv);
	g_free(filename);
	return result;
}

GKeyFile *options_file_open(const char *leafname, const char *group_name)
{
	char *filename;
	GKeyFile *kf = g_key_file_new();
	GError *err = NULL;
	char *first_group;

	options_file_init_paths();
	filename = options_file_build_filename(leafname, NULL);
	if (!filename)
		return kf;

	if (!g_key_file_load_from_file(kf, filename,
				G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS,
				&err))
	{
		if (err && !STR_EMPTY(err->message))
			dlg_critical(NULL, _("Can't read options file %s: %s"),
					filename, err->message);
		else
			dlg_critical(NULL, _("Can't read options file %s"), filename);
		if (err)
			g_error_free(err);
		g_free(filename);
		return kf;
	}

	first_group = g_key_file_get_start_group(kf);
	if (strcmp(first_group, group_name))
	{
		dlg_critical(NULL, _("Options file %s does not start with group '%s'"),
				filename, group_name);
		g_key_file_free(kf);
		kf = g_key_file_new();
	}

	g_free(first_group);
	g_free(filename);

	return kf;
}

gboolean options_file_mkdir_with_parents(const char *dirname)
{
	if (!dirname || !dirname[0] ||
		(!dirname[1] && (dirname[0] == '.' || dirname[0] == '/')))
	{
		g_critical(_("Invalid directory name '%s'"), dirname);
		return FALSE;
	}
#ifdef HAVE_G_MKDIR_WITH_PARENTS
	if (g_mkdir_with_parents(dirname, 0755) == -1)
	{
		dlg_critical(NULL, _("Failed to create directory '%s': %s"),
			dirname, strerror(errno));
		return FALSE;
	}
#else
	if (mkdir(dirname, 0755))
	{
		char *parent;

		switch (errno)
		{
			case EEXIST:
				return TRUE;
			case ENOENT:
				/* Recursively try to make parent */
				parent = g_path_get_dirname(dirname);
				if (parent && !strcmp(parent, dirname))
				{
					g_critical(_("Unable to create parent directory of %s"),
						dirname);
					g_free(parent);
					return FALSE;
				}
				if (options_file_mkdir_with_parents(parent))
				{
					/* Successfully made parent, try again to make target */
					g_free(parent);
					if (!mkdir(dirname, 0755))
						return TRUE;
				}
				else
				{
					g_free(parent);
				}
				/* Fall through to default (failure) if couldn't create parent
				 */
			default:
				dlg_critical(NULL, _("Failed to create directory '%s': %s"),
					dirname, strerror(errno));
				return FALSE;
		}
	}
#endif /* HAVE_G_MKDIR_WITH_PARENTS */
	return TRUE;
}

char *options_file_filename_for_saving(const char *leafname, ...)
{
	static char *xdg_dir = NULL;
	va_list ap;
	char *result;
	char *partial;
	
	if (!xdg_dir)
	{
		xdg_dir = g_build_filename(g_get_user_config_dir(),
			ROXTERM_LEAF_DIR, NULL);
		if (!g_file_test(xdg_dir, G_FILE_TEST_IS_DIR))
			options_file_mkdir_with_parents(xdg_dir);
	}
	va_start(ap, leafname);
	partial = build_filename_from_valist(leafname, ap);
	result = g_build_filename(xdg_dir, partial, NULL);
	g_free(partial);
	va_end(ap);
	return result;
}

void options_file_save(GKeyFile *kf, const char *leafname)
{
	char *pathname = options_file_filename_for_saving(leafname, NULL);
	char *file_data;
	gsize data_len;
	GError *err = NULL;

	if (!pathname)
		return;
	/* leafname may actually be a relative path, so make sure any directories
	 * in it exist */
	if (strchr(leafname, G_DIR_SEPARATOR))
	{
		char *dirname = g_path_get_dirname(pathname);

		options_file_mkdir_with_parents(dirname);
		g_free(dirname);
	}
	file_data = g_key_file_to_data(kf, &data_len, &err);
	if (err)
	{
		if (err && !STR_EMPTY(err->message))
		{
			dlg_critical(NULL, _("Unable to generate options file %s: %s"),
					pathname, err->message);
		}
		else
		{
			dlg_critical(NULL, _("Unable to generate options file %s"),
					pathname);
		}
		g_error_free(err);
	}
	else if (file_data)
	{
		if (!g_file_set_contents(pathname, file_data, data_len, &err))
		{
			if (err && !STR_EMPTY(err->message))
			{
				dlg_critical(NULL, _("Unable to save options file %s: %s"),
						pathname, err->message);
			}
			else
			{
				dlg_critical(NULL, _("Unable to save options file %s"),
						pathname);
			}
			if (err)
				g_error_free(err);
		}
	}
	g_free(pathname);
	g_free(file_data);
}

static void report_lookup_err(GError *err,
		const char *key, const char *group_name)
{
	if (err && err->code != G_KEY_FILE_ERROR_KEY_NOT_FOUND
			&& err->code != G_KEY_FILE_ERROR_GROUP_NOT_FOUND)
	{
		dlg_critical(NULL, _("Error looking up option '%s' in '%s': %s"),
				key, group_name, err->message);
	}
	if (err)
		g_error_free(err);
}

char *options_file_lookup_string_with_default(
		GKeyFile *kf, const char *group_name,
		const char *key, const char *default_value)
{
	GError *err = NULL;
	char *result = g_key_file_get_string(kf, group_name, key, &err);

	if (result && !result[0])
	{
		g_free(result);
		result = NULL;
	}
	if (!result)
	{
		if (default_value)
			result = g_strdup(default_value);
		report_lookup_err(err, key, group_name);
	}
	return result;
}

int options_file_lookup_int_with_default(
		GKeyFile *kf, const char *group_name,
		const char *key, int default_value)
{
	GError *err = NULL;
	int result = g_key_file_get_integer(kf, group_name, key, &err);

	if (err)
	{
		result = default_value;
		report_lookup_err(err, key, group_name);
	}
	return result;
}

/* vi:set sw=4 ts=4 noet cindent cino= */
