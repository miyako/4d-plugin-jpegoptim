/* --------------------------------------------------------------------------------
 #
 #	4DPlugin.cpp
 #	source generated by 4D Plugin Wizard
 #	Project : Jpegoptim
 #	author : miyako
 #	2016/04/29
 #
 # --------------------------------------------------------------------------------*/


#include "4DPluginAPI.h"
#include "4DPlugin.h"

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

#pragma mark -

// ----------------------------------- Jpegoptim ----------------------------------

void my_error_exit (j_common_ptr cinfo)
{
//	my_error_ptr myerr = (my_error_ptr)cinfo->err;
//	
//	(*cinfo->err->output_message) (cinfo);
//	if (myerr->jump_set)
//		longjmp(myerr->setjmp_buffer,1);

}

void my_output_message (j_common_ptr cinfo)
{

}

#pragma mark -

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
	/* abort if incsize is 0 (no expansion of buffer allowed) */
	if (dest->incsize == 0) return FALSE;
	/* otherwise, try expanding buffer... */
	newbuf = (unsigned char *)realloc(dest->buf,dest->bufsize + dest->incsize);
	if (!newbuf) return FALSE;
	dest->pub.next_output_byte = newbuf + dest->bufsize;
	dest->pub.free_in_buffer = dest->incsize;
	dest->buf = newbuf;
	dest->bufsize += dest->incsize;
	dest->incsize *= 2;
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
	
	/* allocate destination manager object for compress object, if needed */
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

#define FREE_LINE_BUF(buf,lines)  {				\
    int j;							\
    for (j=0;j<lines;j++) free(buf[j]);				\
    free(buf);							\
    buf=NULL;							\
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
	
	int write_marker;

	mrk=dinfo->marker_list;
	while (mrk)
	{
		write_marker=0;
		
		/* check for markers to save... */
		
		if (save_com && mrk->marker == JPEG_COM)
			write_marker++;
		
		if (save_iptc && mrk->marker == IPTC_JPEG_MARKER)
			write_marker++;
		
		if (save_exif && mrk->marker == EXIF_JPEG_MARKER &&
				!memcmp(mrk->data,EXIF_IDENT_STRING,EXIF_IDENT_STRING_SIZE))
			write_marker++;
		
		if (save_icc && mrk->marker == ICC_JPEG_MARKER &&
				!memcmp(mrk->data,ICC_IDENT_STRING,ICC_IDENT_STRING_SIZE))
			write_marker++;
		
		if (save_xmp && mrk->marker == XMP_JPEG_MARKER &&
				!memcmp(mrk->data,XMP_IDENT_STRING,XMP_IDENT_STRING_SIZE))
			write_marker++;
		
		/* libjpeg emits some markers automatically so skip these to avoid duplicates... */
		
		/* skip JFIF (APP0) marker */
		if ( mrk->marker == JPEG_APP0 && mrk->data_length >= 14 &&
				mrk->data[0] == 0x4a &&
				mrk->data[1] == 0x46 &&
				mrk->data[2] == 0x49 &&
				mrk->data[3] == 0x46 &&
				mrk->data[4] == 0x00 )
			write_marker=0;
		
		/* skip Adobe (APP14) marker */
		if ( mrk->marker == JPEG_APP0+14 && mrk->data_length >= 12 &&
				mrk->data[0] == 0x41 &&
				mrk->data[1] == 0x64 &&
				mrk->data[2] == 0x6f &&
				mrk->data[3] == 0x62 &&
				mrk->data[4] == 0x65 )
			write_marker=0;
		
		if (write_marker)
			jpeg_write_marker(cinfo,mrk->marker,mrk->data,mrk->data_length);
		
		mrk=mrk->next;
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
			uint32_t len = (uint32_t)(t.fLength * 4) + sizeof(uint8_t);
			std::vector<uint8_t> u(len);
			PA_ConvertCharsetToCharset(
																 (char *)t.fString,
																 t.fLength * sizeof(PA_Unichar),
																 eVTC_UTF_16,
																 (char *)&u[0],
																 len,
																 eVTC_UTF_8
																 );
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
		err = eER_NoErr;
		PA_GetPictureData(picture, pos, h);
		err = PA_GetLastError();
		if(err == eER_NoErr)
		{
			unsigned long insize = PA_GetHandleSize(h);
			buf.resize(insize);
			memcpy(&buf[0], (const void *)PA_LockHandle(h), insize);
			PA_UnlockHandle(h);
			PA_DisposeHandle(h);
			return true;
		}
	}
	return false;
}

#pragma mark -

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
		quality = (q-1);
	}

	jvirt_barray_ptr *coef_arrays = NULL;
	JSAMPARRAY buf = NULL;
	
	/* get jpeg data */
	std::vector<unsigned char>pictureData;
	std::string type(".jpeg");
	if(getPictureDataForType(pParams, 1, pictureData, type))
	{
		/* initialize decompression object */
		dinfo.err = jpeg_std_error(&jderr.pub);
		jpeg_create_decompress(&dinfo);
		jderr.pub.error_exit=my_error_exit;
		jderr.pub.output_message=my_output_message;
		jderr.jump_set = 0;
		
		/* initialize compression object */
		cinfo.err = jpeg_std_error(&jcerr.pub);
		jpeg_create_compress(&cinfo);
		jcerr.pub.error_exit=my_error_exit;
		jcerr.pub.output_message=my_output_message;
		jcerr.jump_set = 0;
		
		if (setjmp(jderr.setjmp_buffer))
		{
			/* error handler for decompress */
			jpeg_abort_decompress(&dinfo);

			jderr.jump_set=0;
		} else {
			jderr.jump_set=1;
		}

		/* prepare to decompress */
		jpeg_save_markers(&dinfo, JPEG_COM, 0xffff);
		for (int j=0;j<=15;j++)
			jpeg_save_markers(&dinfo, JPEG_APP0+j, 0xffff);
		jpeg_mem_src(&dinfo, &pictureData[0], pictureData.size());
		jpeg_read_header(&dinfo, TRUE);
		
		jpeg_start_decompress(&dinfo);

		if(quality == -1)
		{
			coef_arrays = jpeg_read_coefficients(&dinfo);
		}else
		{
			buf = (JSAMPARRAY)malloc(sizeof(JSAMPROW)*dinfo.output_height);
			for (int j=0;j<dinfo.output_height;j++) {
				buf[j]=(JSAMPROW)malloc(sizeof(JSAMPLE)*dinfo.output_width*dinfo.out_color_components);
			}
			while (dinfo.output_scanline < dinfo.output_height)
			{
				PA_YieldAbsolute();
				jpeg_read_scanlines(&dinfo,&buf[dinfo.output_scanline], dinfo.output_height-dinfo.output_scanline);
			}
		}
		
		if (setjmp(jcerr.setjmp_buffer))
		{
			/* error handler for compress failures */
			jpeg_abort_compress(&cinfo);
			jpeg_abort_decompress(&dinfo);
			
			jcerr.jump_set=0;
		} else {
			jcerr.jump_set=1;
		}
	
		size_t outbuffersize = pictureData.size() + 32768;
		unsigned char *outbuffer = (unsigned char *)malloc(outbuffersize);
			
		if(outbuffer)
		{
			jpeg_memory_dest(&cinfo, &outbuffer, &outbuffersize, 65536);
			
			if(quality == -1)
			{
				jpeg_copy_critical_parameters(&dinfo, &cinfo);
				jpeg_simple_progression(&cinfo);
				cinfo.optimize_coding = TRUE;
			}else
			{
				cinfo.in_color_space=dinfo.out_color_space;
				cinfo.input_components=dinfo.output_components;
				cinfo.image_width=dinfo.image_width;
				cinfo.image_height=dinfo.image_height;
				jpeg_set_defaults(&cinfo);
				jpeg_set_quality(&cinfo,quality,TRUE);
				jpeg_simple_progression(&cinfo);
				cinfo.optimize_coding = TRUE;
				jpeg_start_compress(&cinfo,TRUE);
			}

			write_markers(&dinfo,&cinfo,
										save_exif,
										save_iptc,
										save_com,
										save_icc,
										save_xmp);
			
			if(quality == -1)
			{
				jpeg_write_coefficients(&cinfo, coef_arrays);
			}else
			{
				while (cinfo.next_scanline < cinfo.image_height)
				{
					PA_YieldAbsolute();
					jpeg_write_scanlines(&cinfo,&buf[cinfo.next_scanline], dinfo.output_height);
				}
			}

			jpeg_finish_decompress(&dinfo);
			if(quality != -1)
			{
				jpeg_finish_compress(&cinfo);
				FREE_LINE_BUF(buf,dinfo.output_height);
			}
			jpeg_destroy_decompress(&dinfo);
			jpeg_destroy_compress(&cinfo);
			
			PA_Picture picture = PA_CreatePicture((void *)outbuffer, outbuffersize);
			*(PA_Picture*) pResult = picture;
			
			free(outbuffer);
		}
		
	}
	
}
