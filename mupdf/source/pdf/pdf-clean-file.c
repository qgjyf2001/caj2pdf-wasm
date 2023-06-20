// Copyright (C) 2004-2021 Artifex Software, Inc.
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
#include "mupdf/pdf.h"

#include <stdio.h>
#include <string.h>

typedef struct
{
	pdf_document *doc;
	fz_context *ctx;
} globals;

static int
string_in_names_list(fz_context *ctx, pdf_obj *p, pdf_obj *names_list)
{
	int n = pdf_array_len(ctx, names_list);
	int i;
	char *str = pdf_to_str_buf(ctx, p);

	for (i = 0; i < n ; i += 2)
	{
		if (!strcmp(pdf_to_str_buf(ctx, pdf_array_get(ctx, names_list, i)), str))
			return 1;
	}
	return 0;
}

/*
 * Recreate page tree to only retain specified pages.
 */

static void retainpage(fz_context *ctx, pdf_document *doc, pdf_obj *parent, pdf_obj *kids, int page)
{
	pdf_obj *pageref = pdf_lookup_page_obj(ctx, doc, page-1);

	pdf_flatten_inheritable_page_items(ctx, pageref);

	pdf_dict_put(ctx, pageref, PDF_NAME(Parent), parent);

	/* Store page object in new kids array */
	pdf_array_push(ctx, kids, pageref);
}

static int dest_is_valid_page(fz_context *ctx, pdf_obj *obj, int *page_object_nums, int pagecount)
{
	int i;
	int num = pdf_to_num(ctx, obj);

	if (num == 0)
		return 0;
	for (i = 0; i < pagecount; i++)
	{
		if (page_object_nums[i] == num)
			return 1;
	}
	return 0;
}

static int dest_is_valid(fz_context *ctx, pdf_obj *o, int page_count, int *page_object_nums, pdf_obj *names_list)
{
	pdf_obj *p;

	p = pdf_dict_get(ctx, o, PDF_NAME(A));
	if (pdf_name_eq(ctx, pdf_dict_get(ctx, p, PDF_NAME(S)), PDF_NAME(GoTo)) &&
		!string_in_names_list(ctx, pdf_dict_get(ctx, p, PDF_NAME(D)), names_list))
		return 0;

	p = pdf_dict_get(ctx, o, PDF_NAME(Dest));
	if (p == NULL)
		return 1; /* A name with no dest counts as valid. */
	else if (pdf_is_string(ctx, p))
		return string_in_names_list(ctx, p, names_list);
	else if (!dest_is_valid_page(ctx, pdf_array_get(ctx, p, 0), page_object_nums, page_count))
		return 0;

	return 1;
}

static int strip_stale_annot_refs(fz_context *ctx, pdf_obj *field, int page_count, int *page_object_nums)
{
	pdf_obj *kids = pdf_dict_get(ctx, field, PDF_NAME(Kids));
	int len = pdf_array_len(ctx, kids);
	int j;

	if (kids)
	{
		for (j = 0; j < len; j++)
		{
			if (strip_stale_annot_refs(ctx, pdf_array_get(ctx, kids, j), page_count, page_object_nums))
			{
				pdf_array_delete(ctx, kids, j);
				len--;
				j--;
			}
		}

		return pdf_array_len(ctx, kids) == 0;
	}
	else
	{
		pdf_obj *page = pdf_dict_get(ctx, field, PDF_NAME(P));
		int page_num = pdf_to_num(ctx, page);

		for (j = 0; j < page_count; j++)
			if (page_num == page_object_nums[j])
				return 0;

		return 1;
	}
}

static int strip_outlines(fz_context *ctx, pdf_document *doc, pdf_obj *outlines, int page_count, int *page_object_nums, pdf_obj *names_list);

static int strip_outline(fz_context *ctx, pdf_document *doc, pdf_obj *outlines, int page_count, int *page_object_nums, pdf_obj *names_list, pdf_obj **pfirst, pdf_obj **plast)
{
	pdf_obj *prev = NULL;
	pdf_obj *first = NULL;
	pdf_obj *current;
	int count = 0;

	for (current = outlines; current != NULL; )
	{
		int nc;

		/* Strip any children to start with. This takes care of
		 * First/Last/Count for us. */
		nc = strip_outlines(ctx, doc, current, page_count, page_object_nums, names_list);

		if (!dest_is_valid(ctx, current, page_count, page_object_nums, names_list))
		{
			if (nc == 0)
			{
				/* Outline with invalid dest and no children. Drop it by
				 * pulling the next one in here. */
				pdf_obj *next = pdf_dict_get(ctx, current, PDF_NAME(Next));
				if (next == NULL)
				{
					/* There is no next one to pull in */
					if (prev != NULL)
						pdf_dict_del(ctx, prev, PDF_NAME(Next));
				}
				else if (prev != NULL)
				{
					pdf_dict_put(ctx, prev, PDF_NAME(Next), next);
					pdf_dict_put(ctx, next, PDF_NAME(Prev), prev);
				}
				else
				{
					pdf_dict_del(ctx, next, PDF_NAME(Prev));
				}
				current = next;
			}
			else
			{
				/* Outline with invalid dest, but children. Just drop the dest. */
				pdf_dict_del(ctx, current, PDF_NAME(Dest));
				pdf_dict_del(ctx, current, PDF_NAME(A));
				current = pdf_dict_get(ctx, current, PDF_NAME(Next));
			}
		}
		else
		{
			/* Keep this one */
			if (first == NULL)
				first = current;
			prev = current;
			current = pdf_dict_get(ctx, current, PDF_NAME(Next));
			count++;
		}
	}

	*pfirst = first;
	*plast = prev;

	return count;
}

static int strip_outlines(fz_context *ctx, pdf_document *doc, pdf_obj *outlines, int page_count, int *page_object_nums, pdf_obj *names_list)
{
	int nc;
	pdf_obj *first;
	pdf_obj *last;

	if (outlines == NULL)
		return 0;

	first = pdf_dict_get(ctx, outlines, PDF_NAME(First));
	if (first == NULL)
		nc = 0;
	else
		nc = strip_outline(ctx, doc, first, page_count, page_object_nums, names_list, &first, &last);

	if (nc == 0)
	{
		pdf_dict_del(ctx, outlines, PDF_NAME(First));
		pdf_dict_del(ctx, outlines, PDF_NAME(Last));
		pdf_dict_del(ctx, outlines, PDF_NAME(Count));
	}
	else
	{
		int old_count = pdf_dict_get_int(ctx, outlines, PDF_NAME(Count));
		pdf_dict_put(ctx, outlines, PDF_NAME(First), first);
		pdf_dict_put(ctx, outlines, PDF_NAME(Last), last);
		pdf_dict_put_int(ctx, outlines, PDF_NAME(Count), old_count > 0 ? nc : -nc);
	}

	return nc;
}

static void retainpages(fz_context *ctx, globals *glo, int argc, char **argv)
{
	pdf_obj *oldroot, *root, *pages, *kids, *countobj, *olddests;
	pdf_document *doc = glo->doc;
	int argidx = 0;
	pdf_obj *names_list = NULL;
	pdf_obj *outlines;
	pdf_obj *ocproperties;
	pdf_obj *allfields;
	int pagecount;
	int i;
	int *page_object_nums;

	/* Keep only pages/type and (reduced) dest entries to avoid
	 * references to unretained pages */
	oldroot = pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root));
	pages = pdf_dict_get(ctx, oldroot, PDF_NAME(Pages));
	olddests = pdf_load_name_tree(ctx, doc, PDF_NAME(Dests));
	outlines = pdf_dict_get(ctx, oldroot, PDF_NAME(Outlines));
	ocproperties = pdf_dict_get(ctx, oldroot, PDF_NAME(OCProperties));

	root = pdf_new_dict(ctx, doc, 3);
	pdf_dict_put(ctx, root, PDF_NAME(Type), pdf_dict_get(ctx, oldroot, PDF_NAME(Type)));
	pdf_dict_put(ctx, root, PDF_NAME(Pages), pdf_dict_get(ctx, oldroot, PDF_NAME(Pages)));
	if (outlines)
		pdf_dict_put(ctx, root, PDF_NAME(Outlines), outlines);
	if (ocproperties)
		pdf_dict_put(ctx, root, PDF_NAME(OCProperties), ocproperties);

	pdf_update_object(ctx, doc, pdf_to_num(ctx, oldroot), root);

	/* Create a new kids array with only the pages we want to keep */
	kids = pdf_new_array(ctx, doc, 1);

	/* Retain pages specified */
	while (argc - argidx)
	{
		int page, spage, epage;
		const char *pagelist = argv[argidx];

		pagecount = pdf_count_pages(ctx, doc);

		while ((pagelist = fz_parse_page_range(ctx, pagelist, &spage, &epage, pagecount)))
		{
			if (spage < epage)
				for (page = spage; page <= epage; ++page)
					retainpage(ctx, doc, pages, kids, page);
			else
				for (page = spage; page >= epage; --page)
					retainpage(ctx, doc, pages, kids, page);
		}

		argidx++;
	}

	/* Update page count and kids array */
	countobj = pdf_new_int(ctx, pdf_array_len(ctx, kids));
	pdf_dict_put_drop(ctx, pages, PDF_NAME(Count), countobj);
	pdf_dict_put_drop(ctx, pages, PDF_NAME(Kids), kids);

	pagecount = pdf_count_pages(ctx, doc);
	page_object_nums = fz_calloc(ctx, pagecount, sizeof(*page_object_nums));
	for (i = 0; i < pagecount; i++)
	{
		pdf_obj *pageref = pdf_lookup_page_obj(ctx, doc, i);
		page_object_nums[i] = pdf_to_num(ctx, pageref);
	}

	/* If we had an old Dests tree (now reformed as an olddests
	 * dictionary), keep any entries in there that point to
	 * valid pages. This may mean we keep more than we need, but
	 * it's safe at least. */
	if (olddests)
	{
		pdf_obj *names = pdf_new_dict(ctx, doc, 1);
		pdf_obj *dests = pdf_new_dict(ctx, doc, 1);
		int len = pdf_dict_len(ctx, olddests);

		names_list = pdf_new_array(ctx, doc, 32);

		for (i = 0; i < len; i++)
		{
			pdf_obj *key = pdf_dict_get_key(ctx, olddests, i);
			pdf_obj *val = pdf_dict_get_val(ctx, olddests, i);
			pdf_obj *dest = pdf_dict_get(ctx, val, PDF_NAME(D));

			dest = pdf_array_get(ctx, dest ? dest : val, 0);
			if (dest_is_valid_page(ctx, dest, page_object_nums, pagecount))
			{
				pdf_obj *key_str = pdf_new_string(ctx, pdf_to_name(ctx, key), strlen(pdf_to_name(ctx, key)));
				pdf_array_push_drop(ctx, names_list, key_str);
				pdf_array_push(ctx, names_list, val);
			}
		}

		pdf_dict_put(ctx, dests, PDF_NAME(Names), names_list);
		pdf_dict_put(ctx, names, PDF_NAME(Dests), dests);
		pdf_dict_put(ctx, root, PDF_NAME(Names), names);

		pdf_drop_obj(ctx, names);
		pdf_drop_obj(ctx, dests);
		pdf_drop_obj(ctx, olddests);
	}

	/* Edit each pages /Annot list to remove any links that point to nowhere. */
	for (i = 0; i < pagecount; i++)
	{
		pdf_obj *pageref = pdf_lookup_page_obj(ctx, doc, i);

		pdf_obj *annots = pdf_dict_get(ctx, pageref, PDF_NAME(Annots));

		int len = pdf_array_len(ctx, annots);
		int j;

		for (j = 0; j < len; j++)
		{
			pdf_obj *o = pdf_array_get(ctx, annots, j);

			if (!pdf_name_eq(ctx, pdf_dict_get(ctx, o, PDF_NAME(Subtype)), PDF_NAME(Link)))
				continue;

			if (!dest_is_valid(ctx, o, pagecount, page_object_nums, names_list))
			{
				/* Remove this annotation */
				pdf_array_delete(ctx, annots, j);
				len--;
				j--;
			}
		}
	}

	/* Locate all fields on retained pages */
	allfields = pdf_new_array(ctx, doc, 1);
	for (i = 0; i < pagecount; i++)
	{
		pdf_obj *pageref = pdf_lookup_page_obj(ctx, doc, i);

		pdf_obj *annots = pdf_dict_get(ctx, pageref, PDF_NAME(Annots));

		int len = pdf_array_len(ctx, annots);
		int j;

		for (j = 0; j < len; j++)
		{
			pdf_obj *f = pdf_array_get(ctx, annots, j);

			if (pdf_dict_get(ctx, f, PDF_NAME(Subtype)) == PDF_NAME(Widget))
				pdf_array_push(ctx, allfields, f);
		}
	}

	/* From non-terminal widget fields, strip out annot references not
	 * belonging to any retained page. */
	for (i = 0; i < pdf_array_len(ctx, allfields); i++)
	{
		pdf_obj *f = pdf_array_get(ctx, allfields, i);

		while (pdf_dict_get(ctx, f, PDF_NAME(Parent)))
			f = pdf_dict_get(ctx, f, PDF_NAME(Parent));

		strip_stale_annot_refs(ctx, f, pagecount, page_object_nums);
	}

	/* For terminal fields, if action destination is not valid,
	 * remove the action */
	for (i = 0; i < pdf_array_len(ctx, allfields); i++)
	{
		pdf_obj *f = pdf_array_get(ctx, allfields, i);

		if (!dest_is_valid(ctx, f, pagecount, page_object_nums, names_list))
			pdf_dict_del(ctx, f, PDF_NAME(A));
	}

	if (strip_outlines(ctx, doc, outlines, pagecount, page_object_nums, names_list) == 0)
	{
		pdf_dict_del(ctx, root, PDF_NAME(Outlines));
	}

	fz_free(ctx, page_object_nums);
	pdf_drop_obj(ctx, allfields);
	pdf_drop_obj(ctx, names_list);
	pdf_drop_obj(ctx, root);
}

static void pdf_add_outline(fz_context *ctx, fz_outline_iterator* iter, char* outline) 
{
	int prev_level = 1;
	int offset = 0;
	int len = strlen(outline);
	int cnt = 0;
	while (1) {
		int current_level;
		int page;
		char title[1024]={0};
		char uri[20] = {0};
		int current_read;	
		sscanf(&outline[offset], "%d %d %[^\n]%n", &current_level, &page, &title,&current_read);
		sprintf(uri,"#page=%d",page);	
		fz_outline_item item = {title, uri, 0};
		
		printf("title:%s uri:%s\n",item.title,item.uri);
		if (prev_level == current_level) {
			iter->insert(ctx,iter,&item);
		}
		if (prev_level < current_level) {
			iter->prev(ctx,iter);
			iter->down(ctx,iter);
			iter->insert(ctx,iter,&item);
		}
		if (prev_level > current_level) {
			iter->up(ctx,iter);
			iter->next(ctx,iter);
			iter->insert(ctx,iter,&item);
		}
		
		prev_level = current_level;
		offset+=current_read+1;
		if (offset == len) {
			break;
		}
	}
}

int pdf_clean_file(fz_context *ctx, char *infile, int size,char *outline,char **out_buffer, char *password, pdf_write_options *opts, int argc, char *argv[])
{
	globals glo = { 0 };

	glo.ctx = ctx;
	int out_size=0;
	fz_stream* stream;
	fz_try(ctx)
	{	
		stream=fz_open_memory(ctx,infile,size);
		glo.doc=pdf_open_document_with_stream(ctx,stream);
		if (pdf_needs_password(ctx, glo.doc))
			if (!pdf_authenticate_password(ctx, glo.doc, password))
				fz_throw(glo.ctx, FZ_ERROR_GENERIC, "cannot authenticate password: %s", infile);

		/* Only retain the specified subset of the pages */
		if (argc)
			retainpages(ctx, &glo, argc, argv);

		fz_buffer* buffer = fz_new_buffer(ctx, 1);
		fz_output* out=fz_new_output_with_buffer(ctx,buffer);
		
		fz_outline_iterator* iter = glo.doc->super.outline_iterator(ctx, glo.doc);
		pdf_add_outline(ctx, iter, outline);
		fz_drop_outline_iterator(ctx, iter);

		pdf_write_document(ctx, glo.doc, out,opts);
		fz_close_output(ctx, out);
		fz_drop_output(ctx, out);
		
		char *ptr;
		out_size=fz_buffer_storage(ctx,buffer,&ptr);
		*out_buffer=malloc(out_size);
		memcpy(*out_buffer,ptr,out_size);
		fz_drop_buffer(ctx,buffer);
	}
	fz_always(ctx)
	{
		fz_drop_stream(ctx, stream);
		pdf_drop_document(ctx, glo.doc);
	}
	fz_catch(ctx)
	{
		fz_rethrow(ctx);
	}
	return out_size;
}

char* internal_mupdf_clean(char* input,int size,char* outline,int *length) {
	char *password = "";
	pdf_write_options opts = pdf_default_write_options;
	int errors = 0;
	fz_context *ctx;
	char *buffer;

	opts.dont_regenerate_id = 1;

	ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
	if (!ctx)
	{
		fprintf(stderr, "cannot initialise context\n");
		exit(1);
	}
	fz_try(ctx)
	{
		int output_size=pdf_clean_file(ctx, input,size, outline, &buffer, password, &opts, 0, NULL);
		if (length!=NULL) {
			*length = output_size;
		}
	}
	fz_catch(ctx)
	{
		errors++;
	}
	fz_drop_context(ctx);
	return buffer;
}

#include <emscripten/emscripten.h>
EMSCRIPTEN_KEEPALIVE int mupdf_clean_length(char *input,int size,char *outline) {
	int length=0;
	char *buffer = internal_mupdf_clean(input, size, outline, &length);
	free(buffer);
	return length;
}

EMSCRIPTEN_KEEPALIVE char* mupdf_clean(char *input,int size,char *outline) {
	return internal_mupdf_clean(input, size, outline, NULL);
}