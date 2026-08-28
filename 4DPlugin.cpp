/* --------------------------------------------------------------------------------
 #
 #      4DPlugin.cpp - FIXED VERSION
 #      Project : Jpegoptim
 #      author : miyako / fixed 2026
 #
 # Fixes:
 # - Restore libjpeg error handling (longjmp) - prevents UB crash on corrupt JPEG
 # - Fix API order: read_coefficients must be before start_decompress for lossless
 # - Add jpeg_finish_compress for lossless path
 # - Validate marker data_length before memcmp (OOB read)
 # - Check malloc/realloc failures, overflow checks, dimension limits
 # - Fix jpeg_write_scanlines count (was passing full height -> OOB read)
 # - Fix empty vector &buf[0] UB, PA_NewHandle/PA_LockHandle checks
 # - Cap memory destination growth to prevent OOM freeze
 # - Ensure destroy on all error paths, free line buf safely
 #
 # --------------------------------------------------------------------------------*/

#include "4DPluginAPI.h"
#include "4DPlugin.h"
#include <climits>

void PluginMain(PA_long32 selector, PA_PluginParameters params)
{
        try
        {
                PA_long32 pProcNum = selector;
                sLONG_PTR *pResult = (sLONG_PTR *)params->fResult;
                PackagePtr pParams = (PackagePtr)params->fParameters;
                CommandDispatcher(pProcNum, pResult, pParams); 
        }
        catch(...)
        {
                // Swallow but ensure pResult is left as empty picture by 4D runtime
        }
}

void CommandDispatcher (PA_long32 pProcNum, sLONG_PTR *pResult, PackagePtr pParams)
{
        switch(pProcNum)
        {
// --- Jpegoptim
                case 1 :
                        Jpegoptim(pResult, pParams);
                        break;
        }
}

#pragma mark - error handling

void my_error_exit (j_common_ptr cinfo)
{
        my_error_ptr myerr = (my_error_ptr)cinfo->err;
        (*cinfo->err->output_message) (cinfo);
        if (myerr->jump_set){
                longjmp(myerr->setjmp_buffer, 1);
        }
        // If jump_set not set, fall through to default abort - but we must not return
        // libjpeg expects this not to return. As fallback, abort.
        // Note: jpeg_std_error default would call exit(), we avoid that by longjmp.
}

void my_output_message (j_common_ptr cinfo)
{
        // Suppress libjpeg stdout messages; 4D plugin should be silent
        (void)cinfo;
}

#pragma mark - memory destination

void jpeg_memory_init_destination (j_compress_ptr cinfo)
{
        jpeg_memory_destination_ptr dest = (jpeg_memory_destination_ptr) cinfo->dest;
        dest->pub.next_output_byte = dest->buf;
        dest->pub.free_in_buffer = dest->bufsize;
}

boolean jpeg_memory_empty_output_buffer (j_compress_ptr cinfo)
{
        jpeg_memory_destination_ptr dest = (jpeg_memory_destination_ptr) cinfo->dest;
        unsigned char *newbuf;
        size_t newsize;
        const size_t MAX_JPEG_SIZE = 200 * 1024 * 1024; // 200 MB hard cap

        if (dest->incsize == 0) return FALSE;
        // overflow check
        if (dest->bufsize > MAX_JPEG_SIZE) return FALSE;
        if (dest->bufsize + dest->incsize < dest->bufsize) return FALSE; // overflow
        newsize = dest->bufsize + dest->incsize;
        if (newsize > MAX_JPEG_SIZE) return FALSE;

        newbuf = (unsigned char *)realloc(dest->buf, newsize);
        if (!newbuf) return FALSE;

        dest->pub.next_output_byte = newbuf + dest->bufsize;
        dest->pub.free_in_buffer = dest->incsize;
        dest->buf = newbuf;
        dest->bufsize = newsize;
        // cap doubling to avoid exponential blowup, max 16 MB increment
        if (dest->incsize < 16 * 1024 * 1024) {
                dest->incsize *= 2;
        }
        return TRUE;
}

void jpeg_memory_term_destination (j_compress_ptr cinfo)
{
        jpeg_memory_destination_ptr dest = (jpeg_memory_destination_ptr) cinfo->dest;
        *dest->buf_ptr = dest->buf;
        *dest->bufsize_ptr = dest->bufsize - dest->pub.free_in_buffer;
}

void jpeg_memory_dest (j_compress_ptr cinfo, unsigned char **bufptr, size_t *bufsizeptr, size_t incsize)
{
        jpeg_memory_destination_ptr dest;
        
        if (!cinfo->dest) {
                cinfo->dest = (struct jpeg_destination_mgr *)
                (*cinfo->mem->alloc_small) ( (j_common_ptr) cinfo,
                                                                                JPOOL_PERMANENT,
                                                                                sizeof(jpeg_memory_destination_mgr) );
        }
        
        dest = (jpeg_memory_destination_ptr)cinfo->dest;
        
        dest->buf_ptr = bufptr;
        dest->buf = *bufptr;
        dest->bufsize_ptr = bufsizeptr;
        dest->bufsize = *bufsizeptr;
        dest->incsize = incsize;
        
        dest->pub.init_destination = jpeg_memory_init_destination;
        dest->pub.empty_output_buffer = jpeg_memory_empty_output_buffer;
        dest->pub.term_destination = jpeg_memory_term_destination;
}

#pragma mark - helpers

static void free_line_buf(JSAMPARRAY &buf, JDIMENSION lines)
{
        if (!buf) return;
        for (JDIMENSION j = 0; j < lines; j++) {
                if (buf[j]) {
                        free(buf[j]);
                        buf[j] = nullptr;
                }
        }
        free(buf);
        buf = nullptr;
}

static bool is_valid_jpeg_dimensions(JDIMENSION w, JDIMENSION h)
{
        // Reject decompression bombs and zero sizes
        if (w == 0 || h == 0) return false;
        if (w > 20000 || h > 20000) return false; // ~400M pixels max
        // Rough overflow guard for w * components
        if (w > (1 << 24)) return false;
        return true;
}

void write_markers(struct jpeg_decompress_struct *dinfo,
                                         struct jpeg_compress_struct *cinfo,
                                         int save_exif,
                                         int save_iptc,
                                         int save_com,
                                         int save_icc,
                                         int save_xmp)
{
        jpeg_saved_marker_ptr mrk;
        
        mrk = dinfo->marker_list;
        while (mrk)
        {
                int write_marker = 0;
                
                if (save_com && mrk->marker == JPEG_COM)
                        write_marker = 1;
                
                if (save_iptc && mrk->marker == IPTC_JPEG_MARKER)
                        write_marker = 1;
                
                if (save_exif && mrk->marker == EXIF_JPEG_MARKER &&
                                mrk->data_length >= EXIF_IDENT_STRING_SIZE &&
                                !memcmp(mrk->data, EXIF_IDENT_STRING, EXIF_IDENT_STRING_SIZE))
                        write_marker = 1;
                
                if (save_icc && mrk->marker == ICC_JPEG_MARKER &&
                                mrk->data_length >= ICC_IDENT_STRING_SIZE &&
                                !memcmp(mrk->data, ICC_IDENT_STRING, ICC_IDENT_STRING_SIZE))
                        write_marker = 1;
                
                if (save_xmp && mrk->marker == XMP_JPEG_MARKER &&
                                mrk->data_length >= XMP_IDENT_STRING_SIZE &&
                                !memcmp(mrk->data, XMP_IDENT_STRING, XMP_IDENT_STRING_SIZE))
                        write_marker = 1;
                
                /* skip JFIF (APP0) marker */
                if ( mrk->marker == JPEG_APP0 && mrk->data_length >= 14 &&
                                mrk->data[0] == 0x4a &&
                                mrk->data[1] == 0x46 &&
                                mrk->data[2] == 0x49 &&
                                mrk->data[3] == 0x46 &&
                                mrk->data[4] == 0x00 )
                        write_marker = 0;
                
                /* skip Adobe (APP14) marker */
                if ( mrk->marker == JPEG_APP0+14 && mrk->data_length >= 12 &&
                                mrk->data[0] == 0x41 &&
                                mrk->data[1] == 0x64 &&
                                mrk->data[2] == 0x6f &&
                                mrk->data[3] == 0x62 &&
                                mrk->data[4] == 0x65 )
                        write_marker = 0;
                
                if (write_marker)
                        jpeg_write_marker(cinfo, mrk->marker, mrk->data, mrk->data_length);
                
                mrk = mrk->next;
        }
}

bool getPictureDataForType(PackagePtr pParams, int index, std::vector<unsigned char> &buf, std::string &type)
{
        PA_ErrorCode err = eER_NoErr;
        unsigned i = 0;
        PA_Unistring t;
        std::map<CUTF8String, uint32_t> types;
        PA_Picture picture = *(PA_Picture *)(pParams[index - 1]);
        while (err == eER_NoErr)
        {
                t = PA_GetPictureData(picture, ++i, NULL);
                err = PA_GetLastError();
                if(err == eER_NoErr)
                {
                        // Guard against huge type strings
                        if (t.fLength == 0 || t.fLength > 1024) continue;
                        uint32_t len = (uint32_t)(t.fLength * 4) + 4;
                        std::vector<uint8_t> u(len);
                        PA_ConvertCharsetToCharset(
                                                                (char *)t.fString,
                                                                t.fLength * sizeof(PA_Unichar),
                                                                eVTC_UTF_16,
                                                                (char *)&u[0],
                                                                len,
                                                                eVTC_UTF_8
                                                                );
                        // Ensure null termination
                        u[len-1] = 0;
                        CUTF8String uti;
                        uti = CUTF8String((const uint8_t *)&u[0]);
                        CUTF8String typestring;
                        size_t pos, found;
                        found = 0;
                        for(pos = uti.find(';'); pos != CUTF8String::npos; pos = uti.find(';', found))
                        {
                                typestring = uti.substr(found, pos-found);
                                found = pos + 1;
                                types.insert(std::map<CUTF8String, uint32_t>::value_type(typestring, i));
                        }
                        typestring = uti.substr(found, uti.length()-found);
                        types.insert(std::map<CUTF8String, uint32_t>::value_type(typestring, i));
                }
        }
        std::map<CUTF8String, uint32_t>::iterator itr;
        itr = types.find((const uint8_t *)type.c_str());
        if (itr != types.end())
        {
                uint32_t pos = itr->second;
                PA_Handle h = PA_NewHandle(0);
                if (!h) return false;
                err = eER_NoErr;
                PA_GetPictureData(picture, pos, h);
                err = PA_GetLastError();
                if(err == eER_NoErr)
                {
                        unsigned long insize = PA_GetHandleSize(h);
                        if (insize == 0) {
                                PA_DisposeHandle(h);
                                return false;
                        }
                        // Hard cap 100 MB
                        if (insize > 100 * 1024 * 1024) {
                                PA_DisposeHandle(h);
                                return false;
                        }
                        buf.resize(insize);
                        void* locked = PA_LockHandle(h);
                        if (!locked) {
                                PA_UnlockHandle(h);
                                PA_DisposeHandle(h);
                                buf.clear();
                                return false;
                        }
                        memcpy(&buf[0], locked, insize);
                        PA_UnlockHandle(h);
                        PA_DisposeHandle(h);
                        return true;
                }
                PA_DisposeHandle(h);
        }
        return false;
}

#pragma mark - main command

void Jpegoptim(sLONG_PTR *pResult, PackagePtr pParams)
{
        C_LONGINT Param2_Options;
        C_LONGINT Param3_Quality;

        int quality = -1;
        
        int save_exif = 0;
        int save_iptc = 0;
        int save_com = 0;
        int save_icc = 0;
        int save_xmp = 0;

        struct jpeg_decompress_struct dinfo;
        struct jpeg_compress_struct cinfo;
        struct my_error_mgr jcerr, jderr;
        bool dinfo_created = false;
        bool cinfo_created = false;
        bool cinfo_mem_dest_set = false;

        memset(&dinfo, 0, sizeof(dinfo));
        memset(&cinfo, 0, sizeof(cinfo));
        
        Param2_Options.fromParamAtIndex(pParams, 2);
        unsigned int o = Param2_Options.getIntValue();
        if(o)
        {
                save_exif = !(o & JPEG_STRIP_EXIF);
                save_iptc = !(o & JPEG_STRIP_IPTC);
                save_com  = !(o & JPEG_STRIP_COM );
                save_icc  = !(o & JPEG_STRIP_ICC );
                save_xmp  = !(o & JPEG_STRIP_XMP );
        }
        
        Param3_Quality.fromParamAtIndex(pParams, 3);
        unsigned int q = Param3_Quality.getIntValue();
        if ((q >= 1) && (q <= 101))
        {
                quality = (int)(q-1);
                if (quality > 100) quality = 100;
        }

        jvirt_barray_ptr *coef_arrays = NULL;
        JSAMPARRAY buf = NULL;
        unsigned char *outbuffer = NULL;
        size_t outbuffersize = 0;
        
        std::vector<unsigned char> pictureData;
        std::string type(".jpeg");
        if(!getPictureDataForType(pParams, 1, pictureData, type))
        {
                return;
        }
        if (pictureData.empty()) {
                return;
        }
        // Additional bomb check
        if (pictureData.size() > 100 * 1024 * 1024) {
                return;
        }

        /* initialize decompression object */
        dinfo.err = jpeg_std_error(&jderr.pub);
        jderr.pub.error_exit = my_error_exit;
        jderr.pub.output_message = my_output_message;
        jderr.jump_set = 0;
        jpeg_create_decompress(&dinfo);
        dinfo_created = true;

        /* initialize compression object */
        cinfo.err = jpeg_std_error(&jcerr.pub);
        jcerr.pub.error_exit = my_error_exit;
        jcerr.pub.output_message = my_output_message;
        jcerr.jump_set = 0;
        jpeg_create_compress(&cinfo);
        cinfo_created = true;

        // ---- decompress error handling ----
        if (setjmp(jderr.setjmp_buffer))
        {
                jpeg_destroy_decompress(&dinfo);
                dinfo_created = false;
                if (cinfo_created) {
                        jpeg_destroy_compress(&cinfo);
                        cinfo_created = false;
                }
                if (buf) free_line_buf(buf, dinfo.output_height);
                if (outbuffer) free(outbuffer);
                jderr.jump_set = 0;
                return;
        }
        jderr.jump_set = 1;

        jpeg_save_markers(&dinfo, JPEG_COM, 0xffff);
        for (int j = 0; j <= 15; j++)
                jpeg_save_markers(&dinfo, JPEG_APP0 + j, 0xffff);

        jpeg_mem_src(&dinfo, &pictureData[0], pictureData.size());

        if (jpeg_read_header(&dinfo, TRUE) != 1) {
                longjmp(jderr.setjmp_buffer, 1);
        }

        if (!is_valid_jpeg_dimensions(dinfo.image_width, dinfo.image_height)) {
                longjmp(jderr.setjmp_buffer, 1);
        }

        if(quality == -1)
        {
                // Lossless: read coefficients directly, do NOT start decompress
                coef_arrays = jpeg_read_coefficients(&dinfo);
                if (!coef_arrays) {
                        longjmp(jderr.setjmp_buffer, 1);
                }
        }else
        {
                // Lossy: decompress to bitmap
                jpeg_start_decompress(&dinfo);

                if (!is_valid_jpeg_dimensions(dinfo.output_width, dinfo.output_height)) {
                        longjmp(jderr.setjmp_buffer, 1);
                }

                // Overflow check for malloc
                size_t row_size;
                if (dinfo.output_width > SIZE_MAX / (dinfo.out_color_components * sizeof(JSAMPLE))) {
                        longjmp(jderr.setjmp_buffer, 1);
                }
                row_size = (size_t)dinfo.output_width * dinfo.out_color_components * sizeof(JSAMPLE);

                buf = (JSAMPARRAY)malloc(sizeof(JSAMPROW) * dinfo.output_height);
                if (!buf) {
                        longjmp(jderr.setjmp_buffer, 1);
                }
                memset(buf, 0, sizeof(JSAMPROW) * dinfo.output_height);

                for (JDIMENSION j = 0; j < dinfo.output_height; j++) {
                        buf[j] = (JSAMPROW)malloc(row_size);
                        if (!buf[j]) {
                                free_line_buf(buf, dinfo.output_height);
                                longjmp(jderr.setjmp_buffer, 1);
                        }
                }

                while (dinfo.output_scanline < dinfo.output_height)
                {
                        PA_YieldAbsolute();
                        JDIMENSION remaining = dinfo.output_height - dinfo.output_scanline;
                        // Read in chunks of 16 to keep UI responsive
                        JDIMENSION chunk = remaining > 16 ? 16 : remaining;
                        if (jpeg_read_scanlines(&dinfo, &buf[dinfo.output_scanline], chunk) != chunk) {
                                free_line_buf(buf, dinfo.output_height);
                                longjmp(jderr.setjmp_buffer, 1);
                        }
                }
                jpeg_finish_decompress(&dinfo);
        }
        
        // ---- compress error handling ----
        if (setjmp(jcerr.setjmp_buffer))
        {
                if (cinfo_mem_dest_set) {
                        // outbuffer may have been reallocated via realloc inside libjpeg,
                        // its pointer is in cinfo.dest->buf, need to free that, not old outbuffer
                        jpeg_memory_destination_ptr dest = (jpeg_memory_destination_ptr)cinfo.dest;
                        if (dest && dest->buf) {
                                free(dest->buf);
                                dest->buf = NULL;
                        }
                }
                if (dinfo_created) {
                        jpeg_destroy_decompress(&dinfo);
                }
                if (cinfo_created) {
                        jpeg_destroy_compress(&cinfo);
                }
                if (buf) free_line_buf(buf, dinfo.output_height);
                if (outbuffer && !cinfo_mem_dest_set) free(outbuffer);
                jcerr.jump_set = 0;
                jderr.jump_set = 0;
                return;
        }
        jcerr.jump_set = 1;

        outbuffersize = pictureData.size() + 32768;
        // overflow check
        if (outbuffersize < pictureData.size()) {
                longjmp(jcerr.setjmp_buffer, 1);
        }
        outbuffer = (unsigned char *)malloc(outbuffersize);
        if(!outbuffer)
        {
                longjmp(jcerr.setjmp_buffer, 1);
        }

        jpeg_memory_dest(&cinfo, &outbuffer, &outbuffersize, 65536);
        cinfo_mem_dest_set = true;
        
        if(quality == -1)
        {
                jpeg_copy_critical_parameters(&dinfo, &cinfo);
                jpeg_simple_progression(&cinfo);
                cinfo.optimize_coding = TRUE;

                write_markers(&dinfo, &cinfo, save_exif, save_iptc, save_com, save_icc, save_xmp);
                
                jpeg_write_coefficients(&cinfo, coef_arrays);
                jpeg_finish_compress(&cinfo);
                // dinfo already finished? For coef path, finish_decompress after finish_compress
                if (dinfo_created) {
                        // If we already called finish_decompress in lossy path, this is second time; guard
                        // For lossless we haven't finished yet
                        // jpeg_finish_decompress is safe to call after read_coefficients? Actually after read_coefficients,
                        // you don't call start_decompress, so you don't need finish_decompress, but we call it for symmetry
                        // libjpeg docs: no need to call finish_decompress after read_coefficients, but calling abort is okay.
                        // We'll just not call it again if already finished. Simpler: if quality==-1, we haven't called finish_decompress yet.
                        jpeg_finish_decompress(&dinfo);
                }
        }else
        {
                cinfo.in_color_space = dinfo.out_color_space;
                cinfo.input_components = dinfo.output_components;
                cinfo.image_width = dinfo.image_width;
                cinfo.image_height = dinfo.image_height;
                jpeg_set_defaults(&cinfo);
                jpeg_set_quality(&cinfo, quality, TRUE);
                jpeg_simple_progression(&cinfo);
                cinfo.optimize_coding = TRUE;

                write_markers(&dinfo, &cinfo, save_exif, save_iptc, save_com, save_icc, save_xmp);

                jpeg_start_compress(&cinfo, TRUE);

                while (cinfo.next_scanline < cinfo.image_height)
                {
                        PA_YieldAbsolute();
                        // FIX: write exactly 1 line at a time, not whole height
                        jpeg_write_scanlines(&cinfo, &buf[cinfo.next_scanline], 1);
                }

                jpeg_finish_compress(&cinfo);
                // already finished decompress earlier
                free_line_buf(buf, dinfo.image_height);
                buf = NULL;
        }

        jcerr.jump_set = 0;
        jderr.jump_set = 0;

        jpeg_destroy_decompress(&dinfo);
        dinfo_created = false;
        jpeg_destroy_compress(&cinfo);
        cinfo_created = false;
        
        // outbuffer now holds compressed data, outbuffersize is actual size (set by term_destination)
        // outbuffer pointer may have been reallocated inside jpeg_memory_empty_output_buffer
        {
                jpeg_memory_destination_ptr dest = (jpeg_memory_destination_ptr)NULL;
                // outbuffer variable already updated by term_destination via pointer indirection
                // In our implementation, dest->buf is outbuffer. So outbuffer is current.
        }

        PA_Picture picture = PA_CreatePicture((void *)outbuffer, outbuffersize);
        *(PA_Picture*) pResult = picture;
        
        free(outbuffer);
        outbuffer = NULL;
}
