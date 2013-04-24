/* -*- mode: c; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- 
 *
 * $Id$
 *
 * Copyright (c) Erik Lindahl, David van der Spoel 2003,2004.
 * Coordinate compression (c) by Frans van Hoesel. 
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 */
 
#include <stdio.h>
#include <stdlib.h>
#include "xdrfile.h"
#include "xdrfile_xtc.h"
	
#define MAGIC 1995

enum { FALSE, TRUE };

static int xtc_header(XDRFILE *xd,int *natoms,int *step,float *time,mybool bRead)
{
	int result,magic,n=1;
	
	/* Note: read is same as write. He he he */
	magic  = MAGIC;
	if ((result = xdrfile_write_int(&magic,n,xd)) != n)
		{
			if (bRead)
				return exdrENDOFFILE;
			else
				return exdrINT;
		}
	if (magic != MAGIC)
		return exdrMAGIC;
	if ((result = xdrfile_write_int(natoms,n,xd)) != n)
		return exdrINT;
	if ((result = xdrfile_write_int(step,n,xd)) != n)
		return exdrINT;
	if ((result = xdrfile_write_float(time,n,xd)) != n)
		return exdrFLOAT;
	
	return exdrOK;
}

static int xtc_coord(XDRFILE *xd,int *natoms,matrix box,rvec *x,float *prec,
					 mybool bRead)
{
	int result;
    
	/* box */
	result = xdrfile_read_float(box[0],DIM*DIM,xd);
	if (DIM*DIM != result)
		return exdrFLOAT;
	else 
		{
			if (bRead)
				{
					result = xdrfile_decompress_coord_float(x[0],natoms,prec,xd); 
					if (result != *natoms)
						return exdr3DX;
				}
			else
				{
					result = xdrfile_compress_coord_float(x[0],*natoms,*prec,xd); 
					if (result != *natoms)
						return exdr3DX;
				}
		}
	return exdrOK;
}

int read_xtc_natoms(char *fn,int *natoms)
{
	XDRFILE *xd;
	int step,result;
	float time;
	
	xd = xdrfile_open(fn,"r");
	if (NULL == xd)
		return exdrFILENOTFOUND;
	result = xtc_header(xd,natoms,&step,&time,TRUE);
	xdrfile_close(xd);
	
	return result;
}

//int read_xtc_numframes(char *fn, int *numframes)
//{
//	XDRFILE *xd;
//	int step, natoms;
//	float time, prec;
//	matrix box;
//	rvec *x;
//	int result; // ???
//	
//	if ((result = read_xtc_natoms(fn,&natoms)) != exdrOK)
//		return result;
//
//	xd = xdrfile_open(fn,"r");
//	if (NULL == xd)
//		return exdrFILENOTFOUND;
//
//	if ((x=(rvec *)malloc(sizeof(rvec)*natoms))==NULL) {
//		fprintf(stderr,"Cannot allocate memory for coordinates.\n");
//		return exdrNOMEM;
//	}
//
//	// loop through all frames :-p
//	*numframes = 0;
//	while (exdrOK == read_xtc(xd, natoms, &step, &time, box, x, &prec)) {
//		(*numframes)++;
//	}
//	free(x);
//	xdrfile_close(xd);
//	
//	return exdrOK;
//}

int read_xtc(XDRFILE *xd,
			 int natoms,int *step,float *time,
			 matrix box,rvec *x,float *prec)
/* Read subsequent frames */
{
	int result;
  
	if ((result = xtc_header(xd,&natoms,step,time,TRUE)) != exdrOK)
		return result;
	  
	if ((result = xtc_coord(xd,&natoms,box,x,prec,1)) != exdrOK)
		return result;
  
	return exdrOK;
}

int read_xtc_numframes(XDRFILE *xd, int *numframes, int64_t **offsets)
{
    int framebytes;
    int64_t filesize;
    int est_nframes;

    /* Estimation of number of frames, with 20% allowance for error. */
    xdr_seek(xd, 0L, SEEK_END);
    filesize = xdr_tell(xd);
    fprintf(stdout,"In the function.\n");
    if (xdr_seek(xd, (int64_t) XTC_HEADER_SIZE, SEEK_SET) != exdrOK)
        return exdrNR;
    if (xdrfile_read_int(&framebytes,1,xd) == 0)
        return exdrENDOFFILE;
    framebytes = (framebytes + 3) & ~0x03; //Rounding to the next 32-bit boundary
    est_nframes = (int) (filesize/((int64_t) (framebytes+XTC_HEADER_SIZE)) + 1); // add one because it'd be easy to underestimate low frame numbers. 
    est_nframes += est_nframes/5;
    fprintf(stdout,"Estimated %d frames.\n", est_nframes);

    /* Allocate memory for the frame index array */
	if ((*offsets=(int64_t *)malloc(sizeof(int64_t)*est_nframes))==NULL)
    {
		fprintf(stderr,"Cannot allocate memory for frame index.\n");
		return exdrNOMEM;
    }
    fprintf(stdout,"Memory allocated at %d. (next array position at %d).\n", (int)*offsets, (int)&(*offsets)[1]);
    (*offsets)[0] = 0L;
    *numframes = 1;
    while (1)
    {
        fprintf(stdout,"Starting loop.\n");
        if (xdr_seek(xd, (int64_t) (framebytes+XTC_HEADER_SIZE), SEEK_CUR) != exdrOK) {
            free(*offsets);
            return exdrNR;
        }
        fprintf(stdout,"Done seeking, now at pos %d. framebytes at %d\n", (int) xdr_tell(xd), (int) &framebytes);
        if (xdrfile_read_int(&framebytes,1,xd) == 0)
            break;
        /* Read was successful; this is another frame */
        fprintf(stdout,"Done reading nbytes. Now at frame %d, offset %d\n", *numframes, (int)(*offsets)[*numframes-1]);
        /* Check if we need to enlarge array */
        if (*numframes == est_nframes){
            fprintf(stdout,"Enlarging frame index array.\n");
            est_nframes += est_nframes/5 + 1; // Increase in 20% stretches
            if ((*offsets = realloc(*offsets, sizeof(int64_t)*est_nframes))==NULL)
            {
	    	    fprintf(stderr,"Cannot allocate memory for frame index.\n");
                free(*offsets);
	    	    return exdrNOMEM;
            }
            fprintf(stdout,"Enlarged frame index array to %d slots.\n", est_nframes);
        }
        fprintf(stdout,"About to add new offset to index. %d, pointer at %d. Current value %d\n", (int) (xdr_tell(xd) - (int64_t) XTC_HEADER_SIZE - 4L), (int)*offsets, (int) (*offsets)[*numframes]);
        (*offsets)[*numframes] = xdr_tell(xd) - 4L - (int64_t) (XTC_HEADER_SIZE); //Account for the header and the nbytes bytes we read.
        fprintf(stdout,"Done adding.\n");
        (*numframes)++;
        fprintf(stdout,"Done incrementing numframes.\n");
        framebytes = (framebytes + 3) & ~0x03; //Rounding to the next 32-bit boundary
        fprintf(stdout,"Done rounding framebytes.\n");
    }
    fprintf(stdout,"Broke out of loop.\n");
	return exdrOK;
}


int write_xtc(XDRFILE *xd,
			  int natoms,int step,float time,
			  matrix box,rvec *x,float prec)
/* Write a frame to xtc file */
{
	int result;
  
	if ((result = xtc_header(xd,&natoms,&step,&time,FALSE)) != exdrOK)
		return result;

	if ((result = xtc_coord(xd,&natoms,box,x,&prec,0)) != exdrOK)
		return result;
  
	return exdrOK;
}

