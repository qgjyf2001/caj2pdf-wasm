// Copyright (C) 2004-2022 Artifex Software, Inc.
//
// This file is part of MuPDF.
//
// MuPDF is free software: you can redistribute it and/or modify it under the
// terms of the GNU Affero General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// MuPDF is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more
// details.
//
// You should have received a copy of the GNU Affero General Public License
// along with MuPDF. If not, see <https://www.gnu.org/licenses/agpl-3.0.en.html>
//
// Alternative licensing terms are available from the licensor.
// For commercial licensing, see <https://www.artifex.com/> or contact
// Artifex Software, Inc., 1305 Grant Avenue - Suite 200, Novato,
// CA 94945, U.S.A., +1(415)492-9861, for further information.

#include "mupdf/fitz.h"

#include <string.h>

fz_stream *
fz_open_archive_entry(fz_context *ctx, fz_archive *arch, const char *name)
{
	char *local_name;
	fz_stream *stream = NULL;

	if (arch == NULL || !arch->open_entry)
		fz_throw(ctx, FZ_ERROR_GENERIC, "cannot open archive entry");

	local_name = fz_cleanname(fz_strdup(ctx, name));

	fz_var(stream);

	fz_try(ctx)
		stream = arch->open_entry(ctx, arch, local_name);
	fz_always(ctx)
		fz_free(ctx, local_name);
	fz_catch(ctx)
		fz_rethrow(ctx);

	return stream;
}

fz_buffer *
fz_read_archive_entry(fz_context *ctx, fz_archive *arch, const char *name)
{
	char *local_name;
	fz_buffer *buf = NULL;

	if (arch == NULL || !arch->read_entry)
		fz_throw(ctx, FZ_ERROR_GENERIC, "cannot read archive entry");

	local_name = fz_cleanname(fz_strdup(ctx, name));

	fz_var(buf);

	fz_try(ctx)
		buf = arch->read_entry(ctx, arch, local_name);
	fz_always(ctx)
		fz_free(ctx, local_name);
	fz_catch(ctx)
		fz_rethrow(ctx);

	return buf;
}

int
fz_has_archive_entry(fz_context *ctx, fz_archive *arch, const char *name)
{
	char *local_name;
	int res = 0;

	if (arch == NULL)
		return 0;
	if (!arch->has_entry)
		fz_throw(ctx, FZ_ERROR_GENERIC, "cannot check if archive has entry");

	local_name = fz_cleanname(fz_strdup(ctx, name));

	fz_var(res);

	fz_try(ctx)
		res = arch->has_entry(ctx, arch, local_name);
	fz_always(ctx)
		fz_free(ctx, local_name);
	fz_catch(ctx)
		fz_rethrow(ctx);

	return res;
}

const char *
fz_list_archive_entry(fz_context *ctx, fz_archive *arch, int idx)
{
	if (arch == 0)
		return NULL;
	if (!arch->list_entry)
		fz_throw(ctx, FZ_ERROR_GENERIC, "cannot list archive entries");

	return arch->list_entry(ctx, arch, idx);
}

int
fz_count_archive_entries(fz_context *ctx, fz_archive *arch)
{
	if (arch == NULL)
		return 0;
	if (!arch->count_entries)
		fz_throw(ctx, FZ_ERROR_GENERIC, "cannot count archive entries");
	return arch->count_entries(ctx, arch);
}

const char *
fz_archive_format(fz_context *ctx, fz_archive *arch)
{
	if (arch == NULL)
		fz_throw(ctx, FZ_ERROR_GENERIC, "cannot read format of non-existent archive");
	return arch->format;
}

fz_archive *
fz_new_archive_of_size(fz_context *ctx, fz_stream *file, int size)
{
	fz_archive *arch;
	arch = Memento_label(fz_calloc(ctx, 1, size), "fz_archive");
	arch->refs = 1;
	arch->file = fz_keep_stream(ctx, file);
	return arch;
}

fz_archive *
fz_open_archive_with_stream(fz_context *ctx, fz_stream *file)
{
	fz_archive *arch = NULL;

	if (fz_is_zip_archive(ctx, file))
		arch = fz_open_zip_archive_with_stream(ctx, file);
	else if (fz_is_tar_archive(ctx, file))
		arch = fz_open_tar_archive_with_stream(ctx, file);
	else
		fz_throw(ctx, FZ_ERROR_GENERIC, "cannot recognize archive");

	return arch;
}

fz_archive *
fz_open_archive(fz_context *ctx, const char *filename)
{
	fz_stream *file;
	fz_archive *arch = NULL;

	file = fz_open_file(ctx, filename);

	fz_try(ctx)
		arch = fz_open_archive_with_stream(ctx, file);
	fz_always(ctx)
		fz_drop_stream(ctx, file);
	fz_catch(ctx)
		fz_rethrow(ctx);

	return arch;
}

fz_archive *
fz_keep_archive(fz_context *ctx, fz_archive *arch)
{
	return (fz_archive *)fz_keep_imp(ctx, arch, &arch->refs);
}

void
fz_drop_archive(fz_context *ctx, fz_archive *arch)
{
	if (fz_drop_imp(ctx, arch, &arch->refs))
	{
		if (arch->drop_archive)
			arch->drop_archive(ctx, arch);
		fz_drop_stream(ctx, arch->file);
		fz_free(ctx, arch);
	}
}

/* In-memory archive using a fz_tree holding fz_buffers */

typedef struct
{
	fz_archive super;
	fz_tree *tree;
} fz_tree_archive;

static int has_tree_entry(fz_context *ctx, fz_archive *arch, const char *name)
{
	fz_tree *tree = ((fz_tree_archive*)arch)->tree;
	fz_buffer *ent = fz_tree_lookup(ctx, tree, name);
	return ent != NULL;
}

static fz_buffer *read_tree_entry(fz_context *ctx, fz_archive *arch, const char *name)
{
	fz_tree *tree = ((fz_tree_archive*)arch)->tree;
	fz_buffer *ent = fz_tree_lookup(ctx, tree, name);
	if (ent == NULL)
		fz_throw(ctx, FZ_ERROR_GENERIC, "Failed to read %s", name);
	return fz_keep_buffer(ctx, ent);
}

static fz_stream *open_tree_entry(fz_context *ctx, fz_archive *arch, const char *name)
{
	fz_tree *tree = ((fz_tree_archive*)arch)->tree;
	fz_buffer *ent = fz_tree_lookup(ctx, tree, name);
	if (ent == NULL)
		fz_throw(ctx, FZ_ERROR_GENERIC, "Failed to open %s", name);
	return fz_open_buffer(ctx, ent);
}

static void drop_tree_archive_entry(fz_context *ctx, void *ent)
{
	fz_drop_buffer(ctx, ent);
}

static void drop_tree_archive(fz_context *ctx, fz_archive *arch)
{
	fz_tree *tree = ((fz_tree_archive*)arch)->tree;
	fz_drop_tree(ctx, tree, drop_tree_archive_entry);
}

fz_archive *
fz_new_tree_archive(fz_context *ctx, fz_tree *tree)
{
	fz_tree_archive *arch;

	arch = fz_new_derived_archive(ctx, NULL, fz_tree_archive);
	arch->super.format = "tree";
	arch->super.has_entry = has_tree_entry;
	arch->super.read_entry = read_tree_entry;
	arch->super.open_entry = open_tree_entry;
	arch->super.drop_archive = drop_tree_archive;
	arch->tree = tree;

	return &arch->super;
}

void
fz_tree_archive_add_buffer(fz_context *ctx, fz_archive *arch_, const char *name, fz_buffer *buf)
{
	fz_tree_archive *arch = (fz_tree_archive *)arch_;

	if (arch == NULL || arch->super.has_entry != has_tree_entry)
		fz_throw(ctx, FZ_ERROR_GENERIC, "Cannot insert into a non-tree archive");
	buf = fz_keep_buffer(ctx, buf);

	fz_try(ctx)
		arch->tree = fz_tree_insert(ctx, arch->tree, name, buf);
	fz_catch(ctx)
	{
		fz_drop_buffer(ctx, buf);
		fz_rethrow(ctx);
	}
}

void
fz_tree_archive_add_data(fz_context *ctx, fz_archive *arch_, const char *name, const void *data, size_t size)
{
	fz_tree_archive *arch = (fz_tree_archive *)arch_;
	fz_buffer *buf;

	if (arch == NULL || arch->super.has_entry != has_tree_entry)
		fz_throw(ctx, FZ_ERROR_GENERIC, "Cannot insert into a non-tree archive");
	buf = fz_new_buffer_from_copied_data(ctx, data, size);

	fz_try(ctx)
		arch->tree = fz_tree_insert(ctx, arch->tree, name, buf);
	fz_catch(ctx)
	{
		fz_drop_buffer(ctx, buf);
		fz_rethrow(ctx);
	}
}

typedef struct
{
	fz_archive *arch;
	char *dir;
} multi_archive_entry;

typedef struct
{
	fz_archive super;
	int len;
	int max;
	multi_archive_entry *sub;
} fz_multi_archive;

static int has_multi_entry(fz_context *ctx, fz_archive *arch_, const char *name)
{
	fz_multi_archive *arch = (fz_multi_archive *)arch_;
	int i;

	for (i = arch->len-1; i >= 0; i--)
	{
		multi_archive_entry *e = &arch->sub[i];
		const char *subname = name;
		if (e->dir)
		{
			size_t n = strlen(e->dir);
			if (strncmp(e->dir, name, n) != 0)
				continue;
			subname += n;
		}
		if (fz_has_archive_entry(ctx, arch->sub[i].arch, subname))
			return 1;
	}
	return 0;
}

static fz_buffer *read_multi_entry(fz_context *ctx, fz_archive *arch_, const char *name)
{
	fz_multi_archive *arch = (fz_multi_archive *)arch_;
	int i;
	fz_buffer *res = NULL;

	for (i = arch->len-1; i >= 0; i--)
	{
		multi_archive_entry *e = &arch->sub[i];
		const char *subname = name;

		if (e->dir)
		{
			size_t n = strlen(e->dir);
			if (strncmp(e->dir, name, n) != 0)
				continue;
			subname += n;
		}

		fz_try(ctx)
			res = fz_read_archive_entry(ctx, arch->sub[i].arch, subname);
		fz_catch(ctx)
			res = NULL;

		if (res)
			break;
	}

	if (!res)
		fz_throw(ctx, FZ_ERROR_GENERIC, "Failed to read %s", name);

	return res;
}

static fz_stream *open_multi_entry(fz_context *ctx, fz_archive *arch_, const char *name)
{
	fz_multi_archive *arch = (fz_multi_archive *)arch_;
	int i;
	fz_stream *res = NULL;

	for (i = arch->len-1; i >= 0; i--)
	{
		multi_archive_entry *e = &arch->sub[i];
		const char *subname = name;

		if (e->dir)
		{
			size_t n = strlen(e->dir);
			if (strncmp(e->dir, name, n) != 0)
				continue;
			subname += n;
		}

		fz_try(ctx)
			res = fz_open_archive_entry(ctx, arch->sub[i].arch, subname);
		fz_catch(ctx)
			res = NULL;

		if (res)
			break;
	}

	if (!res)
		fz_throw(ctx, FZ_ERROR_GENERIC, "Failed to open %s", name);

	return res;
}

static void drop_multi_archive(fz_context *ctx, fz_archive *arch_)
{
	fz_multi_archive *arch = (fz_multi_archive *)arch_;
	int i;

	for (i = arch->len-1; i >= 0; i--)
	{
		multi_archive_entry *e = &arch->sub[i];
		fz_free(ctx, e->dir);
		fz_drop_archive(ctx, e->arch);
	}
	fz_free(ctx, arch->sub);
}

fz_archive *
fz_new_multi_archive(fz_context *ctx)
{
	fz_multi_archive *arch;

	arch = fz_new_derived_archive(ctx, NULL, fz_multi_archive);
	arch->super.format = "multi";
	arch->super.has_entry = has_multi_entry;
	arch->super.read_entry = read_multi_entry;
	arch->super.open_entry = open_multi_entry;
	arch->super.drop_archive = drop_multi_archive;
	arch->max = 0;
	arch->len = 0;
	arch->sub = NULL;

	return &arch->super;
}

void
fz_mount_multi_archive(fz_context *ctx, fz_archive *arch_, fz_archive *sub, const char *path)
{
	fz_multi_archive *arch = (fz_multi_archive *)arch_;
	char *clean_path = NULL;

	if (arch->super.has_entry != has_multi_entry)
		fz_throw(ctx, FZ_ERROR_GENERIC, "Cannot mount within a non-multi archive!");

	if (arch->len == arch->max)
	{
		int n = arch->max ? arch->max * 2 : 8;

		arch->sub = fz_realloc(ctx, arch->sub, sizeof(*arch->sub) * n);
		arch->max = n;
	}

	/* If we have a path, then strip any trailing slashes, and add just one. */
	if (path)
	{
		size_t n = strlen(path);

		clean_path = fz_malloc(ctx, n + 2);
		memcpy(clean_path, path, n);
		clean_path[n] = 0;
		fz_cleanname(clean_path);
		if (clean_path[0] == '.' && clean_path[1] == 0)
		{
			fz_free(ctx, clean_path);
			clean_path = NULL;
		}
		else
		{
			/* Do a strcat without doing a strcat to avoid the compiler
			 * complaining at us. We know that n here will be <= n above
			 * so this is safe. */
			n = strlen(clean_path);
			clean_path[n] = '/';
			clean_path[n + 1] = 0;
		}
	}

	arch->sub[arch->len].arch = fz_keep_archive(ctx, sub);
	arch->sub[arch->len].dir = clean_path;
	arch->len++;
}
