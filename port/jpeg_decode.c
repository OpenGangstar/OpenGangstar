#include "jpeg_decode.h"

#include <limits.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jpeglib.h"
#include "jerror.h"

_Static_assert(CHAR_BIT == 8 && sizeof(JSAMPLE) == 1, "Retail JPEG uses 8-bit samples.");
_Static_assert(sizeof(INT32) == 4 && sizeof(long) == 4, "IJG scalar arithmetic must remain 32-bit.");
_Static_assert(sizeof(size_t) == sizeof(void*), "JPEG allocator sizes must retain native pointer width.");
_Static_assert(JPEG_LIB_VERSION == 62 && JDCT_DEFAULT == JDCT_ISLOW, "Expected the retail 6b integer IDCT.");

typedef struct JpegError {
    struct jpeg_error_mgr base;
    jmp_buf recovery;
    char message[JMSG_LENGTH_MAX];
} JpegError;

typedef struct JpegContext {
    struct jpeg_decompress_struct codec;
    JpegError error;
    struct jpeg_source_mgr source;
    unsigned char* pixels;
} JpegContext;

static void jpeg_failure(j_common_ptr common) {
    JpegError* error = (JpegError*)common->err;
    (*error->base.format_message)(common, error->message);
    longjmp(error->recovery, 1);
}

static void jpeg_output_message(j_common_ptr common) {
    char message[JMSG_LENGTH_MAX];
    /* Original CImageLoaderJPG formats and logs nonfatal messages. Keep that
     * diagnostic inside C while IJG retains its own warning count/recovery. */
    (*common->err->format_message)(common, message);
    fprintf(stderr, "JPEG: %s\n", message);
}

static void jpeg_source_init(j_decompress_ptr codec) { (void)codec; }
static void jpeg_source_term(j_decompress_ptr codec) { (void)codec; }

static boolean jpeg_source_fill(j_decompress_ptr codec) {
    /* Retail's TRUE-without-new-bytes callback reads beyond truncated input.
     * Native bounded input instead fails; never synthesize an EOI/pixel tail. */
    ERREXIT(codec, JERR_INPUT_EOF);
    return FALSE;
}

static void jpeg_source_skip(j_decompress_ptr codec, long count) {
    if (count <= 0)
        return;
    if ((size_t)count > codec->src->bytes_in_buffer)
        ERREXIT(codec, JERR_INPUT_EOF);
    codec->src->next_input_byte += (size_t)count;
    codec->src->bytes_in_buffer -= (size_t)count;
}

static int jpeg_native_failure(OpenGangstarJpeg* result, const char* message) {
    result->error_code = -1;
    snprintf(result->error, sizeof(result->error), "%s", message);
    return 0;
}

int opengangstar_decode_jpeg(const unsigned char* bytes, size_t length, OpenGangstarJpeg* result) {
    JpegContext* context;
    if (!result)
        return 0;
    memset(result, 0, sizeof(*result));
    if (!bytes || !length || length > 64u * 1024u * 1024u)
        return jpeg_native_failure(result, "JPEG file size is outside the supported range.");
    context = (JpegContext*)calloc(1, sizeof(*context));
    if (!context)
        return jpeg_native_failure(result, "Could not allocate the JPEG decoder.");
    context->codec.err = jpeg_std_error(&context->error.base);
    context->error.base.error_exit = jpeg_failure;
    context->error.base.output_message = jpeg_output_message;
    /* All state changed after setjmp lives on the heap. The unmodified context
     * pointer/result argument remain defined on the longjmp cleanup branch. */
    if (setjmp(context->error.recovery)) {
        result->error_code = context->error.base.msg_code;
        snprintf(result->error, sizeof(result->error), "%s", context->error.message);
        result->warnings = context->error.base.num_warnings;
        jpeg_destroy_decompress(&context->codec);
        free(context->pixels);
        free(context);
        return 0;
    }
    jpeg_create_decompress(&context->codec);
    context->source.next_input_byte = bytes;
    context->source.bytes_in_buffer = length;
    context->source.init_source = jpeg_source_init;
    context->source.fill_input_buffer = jpeg_source_fill;
    context->source.skip_input_data = jpeg_source_skip;
    context->source.resync_to_restart = jpeg_resync_to_restart;
    context->source.term_source = jpeg_source_term;
    context->codec.src = &context->source;
    if (jpeg_read_header(&context->codec, TRUE) != JPEG_HEADER_OK)
        ERREXIT(&context->codec, JERR_CANT_SUSPEND);
    if (!context->codec.image_width || !context->codec.image_height ||
        context->codec.image_width > 4096 || context->codec.image_height > 4096) {
        jpeg_destroy_decompress(&context->codec);
        free(context);
        return jpeg_native_failure(result, "JPEG dimensions are outside the supported range.");
    }
    /* Exact original loader choices at 0x2bcfc6..0x2bcfd0. IJG's default
     * JDCT_ISLOW, no quantization and ordinary 1:1 output scale are retained. */
    context->codec.out_color_space = JCS_RGB;
    context->codec.output_components = 3;
    context->codec.do_fancy_upsampling = FALSE;
    if (!jpeg_start_decompress(&context->codec))
        ERREXIT(&context->codec, JERR_CANT_SUSPEND);
    if (context->codec.output_components != 3)
        ERREXIT(&context->codec, JERR_CONVERSION_NOTIMPL);
    context->pixels = (unsigned char*)malloc((size_t)context->codec.output_width * context->codec.output_height * 3);
    if (!context->pixels) {
        jpeg_destroy_decompress(&context->codec);
        free(context);
        return jpeg_native_failure(result, "Could not allocate JPEG pixels.");
    }
    while (context->codec.output_scanline < context->codec.output_height) {
        JSAMPROW row = context->pixels + (size_t)context->codec.output_scanline * context->codec.output_width * 3;
        if (jpeg_read_scanlines(&context->codec, &row, 1) != 1)
            ERREXIT(&context->codec, JERR_CANT_SUSPEND);
    }
    if (!jpeg_finish_decompress(&context->codec))
        ERREXIT(&context->codec, JERR_CANT_SUSPEND);
    result->width = context->codec.output_width;
    result->height = context->codec.output_height;
    result->rgb = context->pixels;
    result->warnings = context->error.base.num_warnings;
    jpeg_destroy_decompress(&context->codec);
    free(context);
    return 1;
}

void opengangstar_free_jpeg(OpenGangstarJpeg* result) {
    if (!result)
        return;
    free(result->rgb);
    result->rgb = NULL;
    result->width = result->height = 0;
}
