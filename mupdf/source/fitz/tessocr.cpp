#include "mupdf/fitz/config.h"

#ifndef OCR_DISABLED

#include <climits>
#include "tesseract/baseapi.h"
#include "tesseract/capi.h"          // for ETEXT_DESC

extern "C" {

#include "allheaders.h"

#include "tessocr.h"

/* Now the actual allocations to be used for leptonica. Unfortunately
 * we have to use a nasty global here. */
static fz_context *leptonica_mem = NULL;

void *leptonica_malloc(size_t size)
{
	void *ret = Memento_label(fz_malloc_no_throw(leptonica_mem, size), "leptonica_malloc");
#ifdef DEBUG_ALLOCS
	printf("%d LEPTONICA_MALLOC(%p) %d -> %p\n", event++, leptonica_mem, (int)size, ret);
	fflush(stdout);
#endif
	return ret;
}

void leptonica_free(void *ptr)
{
#ifdef DEBUG_ALLOCS
	printf("%d LEPTONICA_FREE(%p) %p\n", event++, leptonica_mem, ptr);
	fflush(stdout);
#endif
	fz_free(leptonica_mem, ptr);
}

void *leptonica_calloc(size_t numelm, size_t elemsize)
{
	void *ret = leptonica_malloc(numelm * elemsize);

	if (ret)
		memset(ret, 0, numelm * elemsize);
#ifdef DEBUG_ALLOCS
	printf("%d LEPTONICA_CALLOC %d,%d -> %p\n", event++, (int)numelm, (int)elemsize, ret);
	fflush(stdout);
#endif
	return ret;
}

/* Not currently actually used */
void *leptonica_realloc(void *ptr, size_t blocksize)
{
	void *ret = fz_realloc_no_throw(leptonica_mem, ptr, blocksize);

#ifdef DEBUG_ALLOCS
	printf("%d LEPTONICA_REALLOC %p,%d -> %p\n", event++, ptr, (int)blocksize, ret);
	fflush(stdout);
#endif
	return ret;
}

#if TESSERACT_MAJOR_VERSION >= 5

static bool
load_file(const char* filename, std::vector<char>* data)
{
	bool result = false;
	FILE *fp = fopen(filename, "rb");
	if (fp == NULL)
		return false;

	fseek(fp, 0, SEEK_END);
	long size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	// Trying to open a directory on Linux sets size to LONG_MAX. Catch it here.
	if (size > 0 && size < LONG_MAX)
	{
		// reserve an extra byte in case caller wants to append a '\0' character
		data->reserve(size + 1);
		data->resize(size);
		result = static_cast<long>(fread(&(*data)[0], 1, size, fp)) == size;
	}
	fclose(fp);
	return result;
}

static bool
tess_file_reader(const char *fname, std::vector<char> *out)
{
	/* FIXME: Look for inbuilt ones. */

	/* Then under TESSDATA */
	return load_file(fname, out);
}

#else

static bool
load_file(const char* filename, GenericVector<char>* data)
{
	bool result = false;
	FILE *fp = fopen(filename, "rb");
	if (fp == NULL)
		return false;

	fseek(fp, 0, SEEK_END);
	long size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	// Trying to open a directory on Linux sets size to LONG_MAX. Catch it here.
	if (size > 0 && size < LONG_MAX)
	{
		// reserve an extra byte in case caller wants to append a '\0' character
		data->reserve(size + 1);
		data->resize_no_init(size);
		result = static_cast<long>(fread(&(*data)[0], 1, size, fp)) == size;
	}
	fclose(fp);
	return result;
}

static bool
tess_file_reader(const STRING& fname, GenericVector<char> *out)
{
	/* FIXME: Look for inbuilt ones. */

	/* Then under TESSDATA */
	return load_file(fname.c_str(), out);
}
#endif

static void
set_leptonica_mem(fz_context *ctx)
{
	int die = 0;

	fz_lock(ctx, FZ_LOCK_ALLOC);
	die = (leptonica_mem != NULL);
	if (!die)
		leptonica_mem = ctx;
	fz_unlock(ctx, FZ_LOCK_ALLOC);
	if (die)
		fz_throw(ctx, FZ_ERROR_GENERIC, "Attempt to use Tesseract from 2 threads at once!");
}

static void
clear_leptonica_mem(fz_context *ctx)
{
	int die = 0;

	fz_lock(ctx, FZ_LOCK_ALLOC);
	die = (leptonica_mem == NULL);
	if (!die)
		leptonica_mem = NULL;
	fz_unlock(ctx, FZ_LOCK_ALLOC);
	if (die)
		fz_throw(ctx, FZ_ERROR_GENERIC, "Attempt to use Tesseract from 2 threads at once!");
}

void *ocr_init(fz_context *ctx, const char *language, const char *datadir)
{
	tesseract::TessBaseAPI *api;

	set_leptonica_mem(ctx);
	setPixMemoryManager(leptonica_malloc, leptonica_free);
	api = new tesseract::TessBaseAPI();

	if (api == NULL)
	{
		clear_leptonica_mem(ctx);
		setPixMemoryManager(malloc, free);
		fz_throw(ctx, FZ_ERROR_GENERIC, "Tesseract initialisation failed");
	}

	if (language == NULL || language[0] == 0)
		language = "eng";

	// Initialize tesseract-ocr with English, without specifying tessdata path
	if (api->Init(datadir, 0, /* data, data_size */
		language,
		tesseract::OcrEngineMode::OEM_DEFAULT,
		NULL, 0, /* configs, configs_size */
		NULL, NULL, /* vars_vec */
		false, /* set_only_non_debug_params */
		&tess_file_reader))
	{
		delete api;
		clear_leptonica_mem(ctx);
		setPixMemoryManager(malloc, free);
		fz_throw(ctx, FZ_ERROR_GENERIC, "Tesseract initialisation failed");
	}

	return api;
}

void ocr_fin(fz_context *ctx, void *api_)
{
	tesseract::TessBaseAPI *api = (tesseract::TessBaseAPI *)api_;

	if (api == NULL)
		return;

	api->End();
	delete api;
	clear_leptonica_mem(ctx);
	setPixMemoryManager(malloc, free);
}

static inline int isbigendian(void)
{
	static const int one = 1;
	return *(char*)&one == 0;
}


static Pix *
ocr_set_image(fz_context *ctx, tesseract::TessBaseAPI *api, fz_pixmap *pix)
{
	Pix *image = pixCreateHeader(pix->w, pix->h, 8);

	if (image == NULL)
		fz_throw(ctx, FZ_ERROR_MEMORY, "Tesseract image creation failed");
	pixSetData(image, (l_uint32 *)pix->samples);
	pixSetPadBits(image, 1);
	pixSetXRes(image, pix->xres);
	pixSetYRes(image, pix->yres);

	if (!isbigendian())
	{
		/* Frizzle the image */
		int x, y;
		uint32_t *d = (uint32_t *)pix->samples;
		for (y = pix->h; y > 0; y--)
			for (x = pix->w>>2; x > 0; x--)
			{
				uint32_t v = *d;
				((uint8_t *)d)[0] = v>>24;
				((uint8_t *)d)[1] = v>>16;
				((uint8_t *)d)[2] = v>>8;
				((uint8_t *)d)[3] = v;
				d++;
			}
	}
	/* pixWrite("test.pnm", image, IFF_PNM); */

	api->SetImage(image);

	return image;
}

static void
ocr_clear_image(fz_context *ctx, Pix *image)
{
	pixSetData(image, NULL);
	pixDestroy(&image);
}

typedef struct {
	fz_context *ctx;
	void *arg;
	int (*progress)(fz_context *, void *, int progress);
} progress_arg;

static bool
do_cancel(void *arg, int dummy)
{
	return true;
}

static bool
progress_callback(ETEXT_DESC *monitor, int l, int r, int t, int b)
{
	progress_arg *details = (progress_arg *)monitor->cancel_this;
	int cancel;

	if (!details->progress)
		return false;

	cancel = details->progress(details->ctx, details->arg, monitor->progress);
	if (cancel)
		monitor->cancel = do_cancel;

	return false;
}

void ocr_recognise(fz_context *ctx,
		void *api_,
		fz_pixmap *pix,
		void (*callback)(fz_context *ctx,
				void *arg,
				int unicode,
				const char *font_name,
				const int *line_bbox,
				const int *word_bbox,
				const int *char_bbox,
				int pointsize),
		int (*progress)(fz_context *ctx,
				void *arg,
				int progress),
		void *arg)
{
	tesseract::TessBaseAPI *api = (tesseract::TessBaseAPI *)api_;
	Pix *image;
	int code;
	int word_bbox[4];
	int char_bbox[4];
	int line_bbox[4];
	bool bold, italic, underlined, monospace, serif, smallcaps;
	int pointsize, font_id;
	const char* font_name;
	ETEXT_DESC monitor;
	progress_arg details;

	if (api == NULL)
		return;

	image = ocr_set_image(ctx, api, pix);

	monitor.cancel = nullptr;
	monitor.cancel_this = &details;
	details.ctx = ctx;
	details.arg = arg;
	details.progress = progress;
	monitor.progress_callback2 = progress_callback;

	code = api->Recognize(&monitor);
	if (code < 0)
	{
		ocr_clear_image(ctx, image);
		fz_throw(ctx, FZ_ERROR_GENERIC, "OCR recognise failed");
	}

	if (!isbigendian())
	{
		/* Frizzle the image */
		int x, y;
		uint32_t *d = (uint32_t *)pix->samples;
		for (y = pix->h; y > 0; y--)
			for (x = pix->w>>2; x > 0; x--)
			{
				uint32_t v = *d;
				((uint8_t *)d)[0] = v>>24;
				((uint8_t *)d)[1] = v>>16;
				((uint8_t *)d)[2] = v>>8;
				((uint8_t *)d)[3] = v;
				d++;
			}
	}

	tesseract::ResultIterator *res_it = api->GetIterator();

	fz_try(ctx)
	{
		while (!res_it->Empty(tesseract::RIL_BLOCK))
		{
			if (res_it->Empty(tesseract::RIL_WORD))
			{
				res_it->Next(tesseract::RIL_WORD);
				continue;
			}

			res_it->BoundingBox(tesseract::RIL_TEXTLINE,
					line_bbox, line_bbox+1,
					line_bbox+2, line_bbox+3);
			res_it->BoundingBox(tesseract::RIL_WORD,
					word_bbox, word_bbox+1,
					word_bbox+2, word_bbox+3);
			font_name = res_it->WordFontAttributes(&bold,
							&italic,
							&underlined,
							&monospace,
							&serif,
							&smallcaps,
							&pointsize,
							&font_id);
			do
			{
				const char *graph = res_it->GetUTF8Text(tesseract::RIL_SYMBOL);
				if (graph && graph[0] != 0)
				{
					int unicode;
					res_it->BoundingBox(tesseract::RIL_SYMBOL,
							char_bbox, char_bbox+1,
							char_bbox+2, char_bbox+3);
					fz_chartorune(&unicode, graph);
					callback(ctx, arg, unicode, font_name, line_bbox, word_bbox, char_bbox, pointsize);
				}
				delete[] graph;
				res_it->Next(tesseract::RIL_SYMBOL);
			}
			while (!res_it->Empty(tesseract::RIL_BLOCK) &&
				!res_it->IsAtBeginningOf(tesseract::RIL_WORD));
		}
	}
	fz_always(ctx)
	{
		delete res_it;
		ocr_clear_image(ctx, image);
	}
	fz_catch(ctx)
		fz_rethrow(ctx);
}

}

#endif
