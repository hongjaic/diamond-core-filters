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

#include <stdio.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <sys/queue.h>
#include "rgb.h"
#include "lib_results.h"
#include "lib_sfimage.h"
#include "texture_tools.h"
#include "img_search.h"
#include "dog_texture_search.h"
#include "lib_ocvimage.h"
#include "factory.h"

#define	MAX_DISPLAY_NAME	64

/* config tokens */
#define	METRIC_ID	"METRIC"
#define	CHANNEL_ID	"CHANNEL"
#define METHOD_ID	"METHOD"
#define DISTANCE_METRIC_ID	"DISTANCEMETRIC"

extern "C" {
	diamond_public
	void search_init();
}


void
search_init()
{
	texture_factory *fac;

	fac = new texture_factory;

	factory_register(fac);
}

texture_search::texture_search(const char *name, const char *descr)
		: example_search(name, descr)
{
	edit_window = NULL;
	similarity = 0.93;
	channels = 3;
	distance_metric = TEXTURE_DIST_PAIRWISE;
}

texture_search::~texture_search()
{
	return;
}


void
texture_search::set_similarity(char *data)
{

	similarity = atof(data);
	if (similarity < 0) {
		similarity = 0.0;
	} else if (similarity > 1.0) {
		similarity = 1.0;
	}
	return;
}

void
texture_search::set_similarity(double sim)
{
	similarity = sim;
	if (similarity < 0) {
		similarity = 0.0;
	} else if (similarity > 1.0) {
		similarity = 1.0;
	}
	return;
}

void
texture_search::set_channels(int num)
{
	assert((num == 1) || (num == 3));
	channels = num;
}

void
texture_search::set_channels(char *data)
{
	int	 num;
	num = atoi(data);
	set_channels(num);
}

int
texture_search::handle_config(int nconf, char **confv)
{
	int	err;

	if (strcmp(METRIC_ID, confv[0]) == 0) {
		assert(nconf > 1);
		set_similarity(confv[1]);
		err = 0;
	} else if (strcmp(CHANNEL_ID, confv[0]) == 0) {
		assert(nconf > 1);
		set_channels(confv[1]);
		err = 0;
	} else if (strcmp(METHOD_ID, confv[0]) == 0) {
		assert(nconf > 1);
		method = atoi(confv[1]);
		err = 0;
	} else if (strcmp(DISTANCE_METRIC_ID, confv[0]) == 0) {
		assert(nconf > 1);
		distance_metric = (texture_dist_t) atoi(confv[1]);
		err = 0;
	} else {
		err = example_search::handle_config(nconf, confv);
	}
	return(err);
}



static void
cb_update_menu_select(GtkWidget* item, GtkUpdateType  policy)
{
	/* XXXX do something ?? */
}


static void
cb_close_edit_window(GtkWidget* item, gpointer data)
{
	texture_search *	search;

	search = (texture_search *)data;
	search->close_edit_win();
}


void
texture_search::close_edit_win()
{

	/* save any changes from the edit windows */
	save_edits();

	/* call the parent class to give them change to cleanup */
	example_search::close_edit_win();

	edit_window = NULL;

}

static void
edit_search_done_cb(GtkButton *item, gpointer data)
{
	GtkWidget * widget = (GtkWidget *)data;
	gtk_widget_destroy(widget);
}


void
texture_search::edit_search()
{
	GtkWidget * widget;
	GtkWidget * box;
	GtkWidget * opt;
	GtkWidget * item;
	GtkWidget * frame;
	GtkWidget * hbox;
	GtkWidget * container;
	GtkWidget * menu;
	char		name[MAX_DISPLAY_NAME];

	/* see if it already exists */
	if (edit_window != NULL) {
		/* raise to top ??? */
		gdk_window_raise(GTK_WIDGET(edit_window)->window);
		return;
	}

	edit_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	snprintf(name, MAX_DISPLAY_NAME - 1, "Edit %s", get_name());
	gtk_window_set_title(GTK_WINDOW(edit_window), name);
	g_signal_connect(G_OBJECT(edit_window), "destroy",
	                 G_CALLBACK(cb_close_edit_window), this);
	box = gtk_vbox_new(FALSE, 10);


	hbox = gtk_hbox_new(FALSE, 10);
	gtk_box_pack_start(GTK_BOX(box), hbox, FALSE, TRUE, 0);

	widget = gtk_button_new_with_label("Close");
	g_signal_connect(G_OBJECT(widget), "clicked",
	                 G_CALLBACK(edit_search_done_cb), edit_window);
	GTK_WIDGET_SET_FLAGS(widget, GTK_CAN_DEFAULT);
	gtk_box_pack_start(GTK_BOX(hbox), widget, FALSE, TRUE, 0);


	/*
		 * Get the controls from the img_search.
	 */
	widget = img_search_display();
	gtk_box_pack_start(GTK_BOX(box), widget, FALSE, TRUE, 0);

	/*
	 	 * Create the texture parameters.
	 */

	frame = gtk_frame_new("Texture Params");
	container = gtk_vbox_new(FALSE, 10);
	gtk_container_add(GTK_CONTAINER(frame), container);

	widget = create_slider_entry("Min similarity", 0.0, 1.0, 2,
	                             similarity, 0.05, &sim_adj);
	gtk_box_pack_start(GTK_BOX(container), widget, FALSE, TRUE, 0);

	hbox = gtk_hbox_new(FALSE, 10);
	gtk_box_pack_start(GTK_BOX(container), hbox, FALSE, TRUE, 0);

	gray_widget = gtk_radio_button_new_with_label(NULL, "Grayscale");
	gtk_box_pack_start(GTK_BOX(hbox), gray_widget, FALSE, TRUE, 0);
	rgb_widget = gtk_radio_button_new_with_label_from_widget(
	                 GTK_RADIO_BUTTON(gray_widget), "Color");
	gtk_box_pack_start(GTK_BOX(hbox), rgb_widget, FALSE, TRUE, 0);

	if (channels == 3) {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rgb_widget), TRUE);
	} else {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gray_widget), TRUE);
	}


	distance_menu = gtk_option_menu_new();
	menu = gtk_menu_new();

	/* these must be declared as the order of the enum  */
	item = gtk_menu_item_new_with_label("Mahalanobis");
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	item = gtk_menu_item_new_with_label("Variance");
	gtk_menu_shell_append(GTK_MENU_SHELL (menu), item);
	item = gtk_menu_item_new_with_label("Pairwise");
	gtk_menu_shell_append (GTK_MENU_SHELL(menu), item);

	gtk_option_menu_set_menu(GTK_OPTION_MENU(distance_menu), menu);
	gtk_box_pack_start(GTK_BOX(container), distance_menu, FALSE, TRUE, 0);
	/* set the default value in the GUI */
	gtk_option_menu_set_history(GTK_OPTION_MENU(distance_menu), (guint)distance_metric);




	opt = gtk_option_menu_new();
	menu = gtk_menu_new();

	item = make_menu_item("Difference of Gaussians",
	                      G_CALLBACK(cb_update_menu_select), GINT_TO_POINTER(0));
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	gtk_option_menu_set_menu(GTK_OPTION_MENU (opt), menu);
	gtk_box_pack_start(GTK_BOX(container), opt, FALSE, TRUE, 0);

	gtk_box_pack_start(GTK_BOX(box), frame, FALSE, TRUE, 0);

	/*
	 * Get the controls from the window search class.
		 */
	widget = get_window_cntrl();
	gtk_box_pack_start(GTK_BOX(box), widget, FALSE, TRUE, 0);

	/*
		 * Get the controls from the example search class.
	 */
	widget = example_display();
	gtk_box_pack_start(GTK_BOX(box), widget, TRUE, TRUE, 0);

	gtk_container_add(GTK_CONTAINER(edit_window), box);
	gtk_widget_show_all(edit_window);

}

/*
 * This method reads the values from the current edit
 * window if there is an active one.
 */

void
texture_search::save_edits()
{
	double	sim;
	int		color;

	/* no active edit window, so return */
	if (edit_window == NULL) {
		return;
	}

	/* get the similarity and save */
	sim = gtk_adjustment_get_value(GTK_ADJUSTMENT(sim_adj));
	set_similarity(sim);

	color = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(rgb_widget));
	if (color) {
		set_channels(3);
	} else {
		set_channels(1);
	}

	distance_metric =
	    (texture_dist_t)gtk_option_menu_get_history(GTK_OPTION_MENU(distance_menu));

	/* call the parent class */
	example_search::save_edits();
}

/*
 * This write the relevant section of the filter specification file
 * for this search.
 */

void
texture_search::write_fspec(FILE *ostream)
{
	const char	*metric;

	save_edits();
	/*
		 * First we write the header section that corrspons
		 * to the filter, the filter name, the assocaited functions.
		 */

	fprintf(ostream, "\n");
	fprintf(ostream, "FILTER %s \n", get_name());
	fprintf(ostream, "THRESHOLD %f \n", 100.0 * similarity);
	fprintf(ostream, "SIGNATURE @\n");

	/*
	 * Next we write call the parent to write out the releated args,
	 * not that since the args are passed as a vector of strings
	 * we need keep the order the args are written constant or silly
	 * things will happen.
	 */
	example_search::write_fspec(ostream);

	/*
	 * Now write the state needed that is just dependant on the histogram
	 * search.  This will have the histo releated parameters
	 * as well as the linearized histograms.
	 */

	fprintf(ostream, "ARG  %f  # similarity \n", 0.0);
	fprintf(ostream, "ARG  %d  # channels \n", channels);
	switch (distance_metric) {
	case TEXTURE_DIST_MAHALANOBIS:
		metric = "mahalanobis";
		break;
	case TEXTURE_DIST_VARIANCE:
		metric = "variance";
		break;
	case TEXTURE_DIST_PAIRWISE:
		metric = "pairwise";
		break;
	default:
		abort();
	}
	fprintf(ostream, "ARG  %s  # distance type \n", metric);

	fprintf(ostream, "REQUIRES  RGB # dependencies \n");
	fprintf(ostream, "MERIT  100 # some relative cost \n");

	set_auxiliary_data_from_examples();
}


void
texture_search::write_config(FILE *ostream, const char *dirname)
{
	save_edits();

	/* create the search configuration */
	fprintf(ostream, "\n\n");
	fprintf(ostream, "SEARCH texture %s\n", get_name());
	fprintf(ostream, "%s %f \n", METRIC_ID, similarity);
	fprintf(ostream, "%s %d \n", CHANNEL_ID, channels);
	fprintf(ostream, "%s %d \n", METHOD_ID, method);
	fprintf(ostream, "%s %d \n", DISTANCE_METRIC_ID, distance_metric);

	example_search::write_config(ostream, dirname);
	return;
}


bool
texture_search::is_editable(void)
{
	return true;
}
