#define _DEFAULT_SOURCE

#include <assert.h>
#include <string.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <Imlib2.h>
#include <libheif/heif.h>

#include "imlib2/image.h"

int load2(ImlibImage *im, int load_data) {
	// Load file.
	int encoded_fd = fileno(im->fp);
	struct stat st;
	if (encoded_fd < 0)
		return LOAD_FAIL;
	if (fstat(encoded_fd, &st) < 0) {
		close(encoded_fd);
		return LOAD_FAIL;
	}

	int                       rc     = LOAD_FAIL;
	struct heif_error err;
	struct heif_image_handle *handle = NULL;
	struct heif_image        *img    = NULL;
	struct heif_context      *ctx    = heif_context_alloc();

	uint8_t *encoded_data = malloc(st.st_size);
	if (!encoded_data)
		goto quit;
	if (read(encoded_fd, encoded_data, st.st_size) != (long)st.st_size)
		goto quit;

	// Decode.
	err = heif_context_read_from_memory_without_copy(ctx, encoded_data, st.st_size, NULL);
	if (err.code != heif_error_Ok) {
		fprintf(stderr, "libheif: %s (error %d/%d)\n", err.message, err.code, err.subcode);
		goto quit;
	}
	err = heif_context_get_primary_image_handle(ctx, &handle);
	if (err.code != heif_error_Ok) {
		fprintf(stderr, "libheif: %s (error %d/%d)\n", err.message, err.code, err.subcode);
		goto quit;
	}

	// Imlib doesn't really have a good way to deal with this; just show a
	// warning I guess.
	int n = heif_context_get_number_of_top_level_images(ctx);
	if (n > 1)
		fprintf(stderr, "heif: %d images in '%s'; only showing the first one\n", n, im->real_file);

	if (!load_data) {
		rc = LOAD_SUCCESS;
		goto quit;
	}

	err = heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, NULL);
	if (err.code != heif_error_Ok) {
		fprintf(stderr, "libheif: %s (error %d/%d)\n", err.message, err.code, err.subcode);
		goto quit;
	}

	// Write data.
	im->w = heif_image_handle_get_width(handle);
	im->h = heif_image_handle_get_height(handle);
	if (!IMAGE_DIMENSIONS_OK(im->w, im->h))
		goto quit;

	if (heif_image_handle_has_alpha_channel(handle))
		UNSET_FLAG(im->flags, F_HAS_ALPHA);
	else
		SET_FLAG(im->flags, F_HAS_ALPHA);

	// Convert to BGRA as expected by imlib.
	uint8_t *data = (uint8_t *)malloc(4 * im->w * im->h);
	if (!data)
		goto quit;
	int stride = 0;
	const uint8_t *plane = heif_image_get_plane_readonly(img, heif_channel_interleaved, &stride);
	if (!plane)
		goto quit;
	for (int y = 0; y < im->h; y++)
		for (int x = 0; x < im->w; x++) {
			data[4*(y*im->w + x)]     = plane[y*stride + 4*x + 2];
			data[4*(y*im->w + x) + 1] = plane[y*stride + 4*x + 1];
			data[4*(y*im->w + x) + 2] = plane[y*stride + 4*x];
			data[4*(y*im->w + x) + 3] = plane[y*stride + 4*x + 3];
		}
	im->data = (DATA32 *)data;

	if (im->lc)
		__imlib_LoadProgressRows(im, 0, im->h);
	rc = LOAD_SUCCESS;
quit:
	if (rc <= 0)
		__imlib_FreeData(im);
	if (ctx)
		heif_context_free(ctx);
	if (handle)
		heif_image_handle_release(handle);
	if (img)
		heif_image_release(img);
	close(encoded_fd);
	free(encoded_data);
	return rc;
}

char save(ImlibImage *im, ImlibProgressFunction progress, char progress_granularity) {
	(void)progress; (void)progress_granularity;

	int                       rc     = LOAD_FAIL;
	struct heif_error         err;
	struct heif_encoder*      enc    = NULL;
	struct heif_image_handle* handle = NULL;
	struct heif_image*        img    = NULL;
	struct heif_context*      ctx    = heif_context_alloc();

	int encoded_fd = open(im->real_file, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (encoded_fd < 0)
		return LOAD_FAIL;

	// Set options.

	// TODO: also supports:
	// heif_compression_AV1
	// heif_compression_AVC
	// heif_compression_JPEG
	err = heif_context_get_encoder_for_format(ctx, heif_compression_HEVC, &enc);
	if (err.code != heif_error_Ok) {
		fprintf(stderr, "libheif: %s (error %d/%d)\n", err.message, err.code, err.subcode);
		goto quit;
	}

	// TODO: also supports lossless encoding.
	//  err = heif_encoder_set_lossless(enc, 1);
	float quality = 50;  // TODO: is this a reasonable default?
	ImlibImageTag *quality_tag = __imlib_GetTag(im, "quality");
	if (quality_tag) {
		quality = quality_tag->val;
		if (quality < 0) {
			fprintf(stderr, "Warning: 'quality' setting %.0f too low for HEIF, using 0\n", quality);
			quality = 0;
		}
		if (quality > 100) {
			fprintf(stderr, "Warning: 'quality' setting %.0f too high for HEIF, using 100\n", quality);
			quality = 100;
		}
	}
	err = heif_encoder_set_lossy_quality(enc, quality);
	if (err.code != heif_error_Ok) {
		fprintf(stderr, "libheif: %s (error %d/%d)\n", err.message, err.code, err.subcode);
		goto quit;
	}


	// Create new image.
	//
	// TODO: this doesn't work; I think you need to add a new alpha channel
	// instead? Can't really be bothered to find out now.
	//if ((im->flags&F_HAS_ALPHA) != 0)
	//	err = heif_image_create(im->w, im->h, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, &img);
	//else
		err = heif_image_create(im->w, im->h, heif_colorspace_RGB, heif_chroma_interleaved_RGB, &img);
	if (err.code != heif_error_Ok) {
		fprintf(stderr, "libheif: %s (error %d/%d)\n", err.message, err.code, err.subcode);
		goto quit;
	}
	err = heif_image_add_plane(img, heif_channel_interleaved, im->w, im->h, 8);
	if (err.code != heif_error_Ok) {
		fprintf(stderr, "libheif: %s (error %d/%d)\n", err.message, err.code, err.subcode);
		goto quit;
	}

	int stride = 0;
	uint8_t *plane = heif_image_get_plane(img, heif_channel_interleaved, &stride);
	if (!plane)
		goto quit;

	DATA32 pixel;
	for (int y = 0; y < im->h; y++)
		for (int x = 0; x < im->w; x++) {
			pixel = im->data[y*im->w + x];
			plane[y*stride + 3*x]     = PIXEL_R(pixel);
			plane[y*stride + 3*x + 1] = PIXEL_G(pixel);
			plane[y*stride + 3*x + 2] = PIXEL_B(pixel);
			//plane[y*stride + 4*x + 3] = PIXEL_A(pixel);
		}

	// Encode.
	err = heif_context_encode_image(ctx, img, enc, NULL, &handle);
	if (err.code != heif_error_Ok) {
		fprintf(stderr, "libheif: %s (error %d/%d)\n", err.message, err.code, err.subcode);
		goto quit;
	}

	// Write.
	err = heif_context_write_to_file(ctx, im->real_file);
	if (err.code != heif_error_Ok) {
		fprintf(stderr, "libheif: %s (error %d/%d)\n", err.message, err.code, err.subcode);
		goto quit;
	}

	if (im->lc)
		__imlib_LoadProgressRows(im, 0, im->h);
	rc = LOAD_SUCCESS;
quit:
	if (ctx)
		heif_context_free(ctx);
	if (handle)
		heif_image_handle_release(handle);
	if (img)
		heif_image_release(img);
	if (enc)
		heif_encoder_release(enc);
	close(encoded_fd);
	return rc;
}

void formats(ImlibLoader *l) {
	static const char  *const list_formats[] = {"heif", "heic", "avif"};
	__imlib_LoaderSetFormats(l, list_formats, sizeof(list_formats) / sizeof(char *));
}
