/*
 *  SnapFind
 *  An interactive image search application
 *  Version 1
 *
 *  Copyright (c) 2002-2005 Intel Corporation
 *  Copyright (c) 2006 Larry Huston <larry@thehustons.net>
 *  Copyright (c) 2007 Carnegie Mellon University
 *  All Rights Reserved.
 *
 *  This software is distributed under the terms of the Eclipse Public
 *  License, Version 1.0 which can be found in the file named LICENSE.
 *  ANY USE, REPRODUCTION OR DISTRIBUTION OF THIS SOFTWARE CONSTITUTES
 *  RECIPIENT'S ACCEPTANCE OF THIS AGREEMENT
 */

/*
 * texture filter
 */

#include <opencv/cv.h>
#include <stdio.h>
#include <stdint.h>
#include "lib_results.h"
#include "lib_sfimage.h"
#include "lib_filter.h"
#include "texture_tools.h"
#include "lib_ocvimage.h"

#define VERBOSE 1

#define ASSERT(exp)							\
if(!(exp)) {								\
  lf_log(LOGL_ERR, "Assertion %s failed at ", #exp);		\
  lf_log(LOGL_ERR, "%s, line %d.", __FILE__, __LINE__);	\
  pass = 0;								\
  goto done;								\
}


static int
read_texture_args(const char *fname, texture_args_t *texture_args,
                  int argc, const char * const *args)
{

	int pass = 0;
	int s_index, f_index, f_vals;

	texture_args->name = strdup(fname);
	assert(texture_args->name != NULL);

	texture_args->scale = atof(args[0]);
	texture_args->box_width = atoi(args[1]);
	texture_args->box_height = atoi(args[2]);
	texture_args->step = atoi(args[3]);
	texture_args->min_matches = atoi(args[4]);
	// <-- for normalized
	//texture_args->max_distance = (1.0 - atof(args[6])) * NUM_LAP_PYR_LEVELS;
	texture_args->max_distance = (1.0 - atof(args[5]));

	texture_args->num_channels = atoi(args[6]);

	texture_args->texture_distance = (texture_dist_t) atoi(args[7]);

	texture_args->num_samples = atoi(args[8]);

	/* read in the arguments for each sample */
	argc -= 9; args += 9;

	texture_args->sample_values = (double **)
      	    malloc(sizeof(double *) * texture_args->num_samples);
	assert(texture_args->sample_values != NULL);

	f_vals = NUM_LAP_PYR_LEVELS * texture_args->num_channels;
	for (s_index = 0; s_index < texture_args->num_samples && argc >= f_vals;
	     s_index++) {
		texture_args->sample_values[s_index] =
		    (double *)malloc(sizeof(double) * f_vals);
		for (f_index = 0; f_index < f_vals; f_index++) {
			texture_args->sample_values[s_index][f_index] = atof(*args);
			argc--; args++;
		}
	}
	texture_args->num_samples = s_index;
	pass = 1;
	return pass;
}


static int
f_init_texture_detect(int numarg, const char * const *args,
		      int blob_len, const void *blob,
		      const char *filt_name, void **f_datap)
{
	texture_args_t*	targs;
	int				err;

	lf_log(LOGL_TRACE, "f_init_texture_detect: filter name %s", 
	     filt_name);
	targs = (texture_args_t *)malloc(sizeof(*targs));
	assert(targs);

	err = read_texture_args(filt_name, targs, numarg, args);
	assert(err);

	*f_datap = targs;
	return(0);
}


static int
f_eval_texture_detect(lf_obj_handle_t ohandle, void *f_datap)
{

	int		pass = 0;
	int		err;
	IplImage 	*img = NULL;
	IplImage 	*dst_img = NULL;
	RGBImage      * rgb_img = NULL;
	size_t 		len;
	float			min_similarity;
	texture_args_t  *targs = (texture_args_t *)f_datap;
	bbox_list_t		blist;
	bbox_t	*		cur_box;
	const void    *	dptr;

	lf_log(LOGL_TRACE, "f_texture_detect: enter");

	err = lf_ref_attr(ohandle, RGB_IMAGE, &len, &dptr);
	rgb_img = (RGBImage *)dptr;
	assert(err == 0);

	if (targs->num_channels == 1) {
		img = get_gray_ipl_image(rgb_img);
	} else if (targs->num_channels == 3) {
		img = get_rgb_ipl_image(rgb_img);
	}
	ASSERT(img);

	dst_img = cvCreateImage(cvSize(img->width, img->height), IPL_DEPTH_8U, 1);

	TAILQ_INIT(&blist);

	if (targs->texture_distance == TEXTURE_DIST_MAHALANOBIS) {
		pass = texture_test_entire_image_mahalanobis(img, targs, &blist);
	} else if (targs->texture_distance == TEXTURE_DIST_VARIANCE) {
		pass = texture_test_entire_image_variance(img, targs, &blist);
	} else if (targs->texture_distance == TEXTURE_DIST_PAIRWISE) {
		pass = texture_test_entire_image_pairwise(img, targs, &blist);
	} else {
		assert(0);
	}
	if (pass >= targs->min_matches) {

		save_patches(ohandle, targs->name, &blist);


		min_similarity = 2.0;
		while (!(TAILQ_EMPTY(&blist))) {
			cur_box = TAILQ_FIRST(&blist);
			if ((1.0 - cur_box->distance) < min_similarity) {
				min_similarity = 1.0 - cur_box->distance;
			}
			TAILQ_REMOVE(&blist, cur_box, link);
		}

		if (min_similarity == 2.0) {
			pass = 0;
		} else {
			pass = (int)(100.0 * min_similarity);
		}
	} else {
		while (!(TAILQ_EMPTY(&blist))) {
			cur_box = TAILQ_FIRST(&blist);
			TAILQ_REMOVE(&blist, cur_box, link);
		}
		pass = 0;
	}
done:

	if (dst_img)
		cvReleaseImage(&dst_img);

	if (img) {
		cvReleaseImage(&img);
	}
	return pass;
}

LF_MAIN(f_init_texture_detect, f_eval_texture_detect)