/*
 * 	SnapFind (Release 0.9)
 *      An interactive image search application
 *
 *      Copyright (c) 2002-2005, Intel Corporation
 *      All Rights Reserved
 *
 *  This software is distributed under the terms of the Eclipse Public
 *  License, Version 1.0 which can be found in the file named LICENSE.
 *  ANY USE, REPRODUCTION OR DISTRIBUTION OF THIS SOFTWARE CONSTITUTES
 *  RECIPIENT'S ACCEPTANCE OF THIS AGREEMENT
 */

#ifndef	_GABOR_TOOLS_H_
#define	_GABOR_TOOLS_H_	1

#include "gabor.h"

typedef struct gtexture_args_t {
    char*               name;
    float	    	scale;
    int                 step;
    int                 min_matches;
    float               max_distance;
    int			num_angles;
    int			num_freq;
    int			radius;
    float		max_freq;
    float		min_freq;
    float		sigma;
    int                 num_samples;
    float **		response_list;
    gabor *		gobj;
} gtexture_args_t;
                                                                                



#ifdef __cplusplus
extern "C" {
#endif

int gabor_test_image(RGBImage * img, gtexture_args_t * targs, 
			bbox_list_t * blist);



#ifdef __cplusplus
}
#endif

#endif	/* ! _GABOR_TOOLS_H_ */
