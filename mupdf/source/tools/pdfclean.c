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

/*
 * PDF cleaning tool: general purpose pdf syntax washer.
 *
 * Rewrite PDF with pretty printed objects.
 * Garbage collect unreachable objects.
 * Inflate compressed streams.
 * Create subset documents.
 *
 * TODO: linearize document for fast web view
 */

#include "mupdf/fitz.h"
#include "mupdf/pdf.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static int usage(void)
{
	fprintf(stderr,
		"usage: mutool clean [options] input.pdf [output.pdf] [pages]\n"
		"\t-p -\tpassword\n"
		"\t-g\tgarbage collect unused objects\n"
		"\t-gg\tin addition to -g compact xref table\n"
		"\t-ggg\tin addition to -gg merge duplicate objects\n"
		"\t-gggg\tin addition to -ggg check streams for duplication\n"
		"\t-l\tlinearize PDF\n"
		"\t-D\tsave file without encryption\n"
		"\t-E -\tsave file with new encryption (rc4-40, rc4-128, aes-128, or aes-256)\n"
		"\t-O -\towner password (only if encrypting)\n"
		"\t-U -\tuser password (only if encrypting)\n"
		"\t-P -\tpermission flags (only if encrypting)\n"
		"\t-a\tascii hex encode binary streams\n"
		"\t-d\tdecompress streams\n"
		"\t-z\tdeflate uncompressed streams\n"
		"\t-f\tcompress font streams\n"
		"\t-i\tcompress image streams\n"
		"\t-c\tclean content streams\n"
		"\t-s\tsanitize content streams\n"
		"\t-A\tcreate appearance streams for annotations\n"
		"\t-AA\trecreate appearance streams for annotations\n"
		"\t-m\tpreserve metadata\n"
		"\tpages\tcomma separated list of page numbers and ranges\n"
		);
	return 1;
}

static int encrypt_method_from_string(const char *name)
{
	if (!strcmp(name, "rc4-40")) return PDF_ENCRYPT_RC4_40;
	if (!strcmp(name, "rc4-128")) return PDF_ENCRYPT_RC4_128;
	if (!strcmp(name, "aes-128")) return PDF_ENCRYPT_AES_128;
	if (!strcmp(name, "aes-256")) return PDF_ENCRYPT_AES_256;
	return PDF_ENCRYPT_UNKNOWN;
}

__attribute__((__used__)) void wasm_export() {
	FILE *f;
	int len;
	char *data;
	
	f=fopen("1.pdf","rb");
	fseek(f,0,SEEK_END);
	len=ftell(f);
	fseek(f,0,SEEK_SET);
	data=(char*)malloc(len+1);
	fread(data,1,len,f);
	fclose(f);

	int output_size;
	
	mupdf_clean(data,len);
	mupdf_clean_length(data,len);
}

int pdfclean_main(int argc, char **argv)
{	
	return 0;
}
