#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <math.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <libgen.h>		/* dirname */
#include <assert.h>
#include <stdint.h>
#include <signal.h>
#include <getopt.h>

#include "filter_api.h"
#include "searchlet_api.h"
#include "gui_thread.h"

#include "queue.h"
#include "ring.h"
#include "rtimer.h"
#include "sf_consts.h"

#include "face_search.h"
#include "face_image.h" 
#include "rgb.h"
#include "face.h"
#include "fil_tools.h"
#include "image_tools.h"
#include "face_widgets.h"
#include "texture_tools.h"
#include "img_search.h"
#include "sfind_search.h"
#include "search_support.h"
#include "gtk_image_tools.h"
#include "sfind_tools.h"
#include "import_sample.h"
#include "snapfind.h"

/* XXX fix this */
extern img_search *snap_searches[];
extern int num_searches;
void update_search_entry(img_search *cur_search, int row);

#define	MIN_DIMENSION	6

import_win_t	import_window = {NULL};

/* 
 * global state used for highlighting (running filters locally)
 */
static struct {
	pthread_mutex_t mutex;
	int 		thread_running;
	pthread_t 	thread;
} highlight_info = { PTHREAD_MUTEX_INITIALIZER, 0 };

/* forward function declarations */

static void kill_highlight_thread(int run);


static inline int
min(int a, int b) { 
	return ( (a < b) ? a : b );
}
static inline int
max(int a, int b) { 
	return ( (a > b) ? a : b );
}


/*
 * make pixbuf from img
 * XXX move to library
 */
static GdkPixbuf*
pb_from_img(RGBImage *img) 
{
	GdkPixbuf *pbuf;

	/* NB pixbuf refers to data */
	pbuf = gdk_pixbuf_new_from_data((const guchar *)&img->data[0], 
					GDK_COLORSPACE_RGB, 1, 8, 
					img->columns, img->rows, 
					(img->columns*sizeof(RGBPixel)),
					NULL,
					NULL);
	if (pbuf == NULL) {
		printf("failed to allocate pbuf\n");
		exit(1);
	}
	return pbuf;
}


#ifdef	XXX
/* 
 * draw a bounding box into image at scale. bbox is read from object(!)
 */
static region_t
draw_bounding_box(RGBImage *img, int scale, 
		  lf_fhandle_t fhandle, ls_obj_handle_t ohandle,
		  RGBPixel color, RGBPixel mask, char *fmt, int i) 
{
	search_param_t 	param;	
	int 		err;
	bbox_t		bbox;

	err = read_param(fhandle, ohandle, fmt, &param, i);
				
	bbox.min_x = param.bbox.xmin;
	bbox.min_y = param.bbox.ymin;
	bbox.max_x = param.bbox.xmin + param.bbox.xsiz - 1;
	bbox.max_y = param.bbox.ymin + param.bbox.ysiz - 1;

	if (err) {
		//printf("XXXX failed to get bbox %d\n", i);
	} else {
		image_draw_bbox_scale(img, &bbox, scale, mask, color);
		//image_fill_bbox_scale(img, &bbox, scale, mask, color);
	}
	
	return param.bbox;
}
#endif

static gboolean
expose_event(GtkWidget *widget, GdkEventExpose *event, gpointer data) 
{
	RGBImage *img = (RGBImage *)data;
	int width, height;

	width = min(event->area.width, img->width - event->area.x);
	height = min(event->area.height, img->height - event->area.y);

	if(width <= 0 || height <= 0) {
		goto done;
	}
	assert(widget == import_window.drawing_area);

	gdk_window_clear_area(widget->window,
			       event->area.x, event->area.y,
			       event->area.width, event->area.height); 
	gdk_gc_set_clip_rectangle (widget->style->fg_gc[widget->state],
				   &event->area);

	for(int i=0; i<IMP_MAX_LAYERS; i++) {
		int pht = gdk_pixbuf_get_height(import_window.pixbufs[i]);
		assert(event->area.y + height <= pht);
		assert(width >= 0);
		assert(height >= 0);
		gdk_pixbuf_render_to_drawable_alpha(import_window.pixbufs[i],
						    widget->window,
						    event->area.x, event->area.y,
						    event->area.x, event->area.y,
						    width, height,
						    GDK_PIXBUF_ALPHA_FULL, 1, /* ignored */
						    GDK_RGB_DITHER_MAX,
						    0, 0);
	}

	gdk_gc_set_clip_rectangle(widget->style->fg_gc[widget->state],
				   NULL);

done:
	return TRUE;
}



/* draw all the bounding boxes */
static void
cb_draw_res_layer(GtkWidget *widget, gpointer ptr) 
{

	GUI_CALLBACK_ENTER();
	
	/* although we clear the pixbuf data here, we still need to
	 * generate refreshes for either the whole image or the parts
	 * that we cleared. */
	rgbimg_clear(import_window.layers[IMP_RES_LAYER]);

#ifdef	XXX
	/* draw faces (presumably there's only one checkbox, but that's ok) */
	gtk_container_foreach(GTK_CONTAINER(import_window.face_cb_area), draw_face_func, 
			      import_window.face_cb_area);

	/* draw histo bboxes, if on */
	gtk_container_foreach(GTK_CONTAINER(import_window.histo_cb_area), 	
		draw_hbbox_func, import_window.histo_cb_area);
#endif
	GUI_CALLBACK_LEAVE();
}


static GtkWidget *
describe_hbbox(lf_fhandle_t fhandle, ls_obj_handle_t ohandle, int i,
		      GtkWidget **button) 
{
	search_param_t 	param;	
	int 		err;

	GUI_THREAD_CHECK(); 
	
	err = read_param(fhandle, ohandle, HISTO_BBOX_FMT, &param, i);
	if (err) {
		printf("XXX failed to read parameter <%s> \n", HISTO_BBOX_FMT);
/* 		label = gtk_label_new("ERR"); */
/* 		gtk_box_pack_start(GTK_BOX(container), label, TRUE, TRUE, 0); */
/* 		gtk_widget_show(label); */
	} else {
		char buf[BUFSIZ];
		
		if(param.type == PARAM_HISTO) {
			sprintf(buf, "%s (similarity %.0f%%)", param.name,
				100 - 100.0*param.distance);
			*button = gtk_check_button_new_with_label(buf);
			g_signal_connect (G_OBJECT(*button), "toggled",
					  G_CALLBACK(cb_draw_res_layer), GINT_TO_POINTER(i));
			gtk_object_set_user_data(GTK_OBJECT(*button), GINT_TO_POINTER(i));


			gtk_widget_show(*button);
		} else {
			printf("param type not histo !!! %d \n", param.type);
		}
	}
	return *button;
}




static gboolean
realize_event(GtkWidget *widget, GdkEventAny *event, gpointer data) 
{
	
	assert(widget == import_window.drawing_area);

	for(int i=0; i<IMP_MAX_LAYERS; i++) {
		import_window.pixbufs[i] = pb_from_img(import_window.layers[i]);
	}
	
	return TRUE;
}


#ifdef	XXXX

static void
draw_hbbox_func(GtkWidget *widget, void *ptr) 
{
	int i = GPOINTER_TO_INT(gtk_object_get_user_data(GTK_OBJECT(widget)));
	region_t region;
	RGBPixel mask = colorMask;
	RGBPixel color = green;

	GUI_THREAD_CHECK(); 

	if(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
		/* don't draw, but still need to refresh */
		mask = clearMask;
		
		/* can't draw clear, lest we wipe out overlapping box */
		//color = clearColor;
	}

	region = draw_bounding_box(import_window.layers[IMP_RES_LAYER], 1, fhandle,
				   import_window.hooks->ohandle,
				   color, mask, HISTO_BBOX_FMT, i);
	
	/* refresh */
	gtk_widget_queue_draw_area(import_window.drawing_area,
				   region.xmin, region.ymin,
				   region.xsiz, region.ysiz);
}

static void
draw_face_func(GtkWidget *widget, void *ptr) 
{
	region_t region;
	RGBPixel mask = colorMask;
	RGBPixel color = red;
	int num_faces = import_window.nfaces;

	/* draw faces, if on */
	if(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
		/* don't draw, but still need to refresh */
		mask = clearMask;
	}

	for(int i=0; i<num_faces; i++) {
		region = draw_bounding_box(import_window.layers[IMP_RES_LAYER], 1, fhandle,
					   import_window.hooks->ohandle,
					   color, mask, FACE_BBOX_FMT, i);
		/* refresh */
		gtk_widget_queue_draw_area(import_window.drawing_area,
					   region.xmin, region.ymin,
					   region.xsiz, region.ysiz);

	}

}
#endif

static void *
image_highlight_main(void *ptr) 
{
	bbox_t *		cur_bb;
	bbox_list_t		bblist;
  	char 			buf[BUFSIZ];
	RGBImage *		hl_img;
	RGBPixel mask = colorMask;
	RGBPixel color = red;
	guint			id;
	int				use_box;
	int err;
	int	i;

	err = pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	assert(!err);
	err = pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
	assert(!err);

	id = gtk_statusbar_get_context_id(
		GTK_STATUSBAR(import_window.statusbar), "histo");

	/* look at toggle button to see if we use the boxes or shading */
	use_box = 
		gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(import_window.drawbox));

	/* 
	 * go through each of the searches, if highlight is selected,
	 * then eval the regions according to the values.
	 */

	
	hl_img = import_window.layers[IMP_HIGHLIGHT_LAYER];
	rgbimg_clear(hl_img);

	GUI_THREAD_ENTER();
	gtk_widget_queue_draw_area(import_window.drawing_area, 0, 0,
			hl_img->width, hl_img->height);
	GUI_THREAD_LEAVE();

	import_window.nselections = 0;
	for (i=0; i < num_searches; i++) {
		/* if highlight isn't selected, then go to next object */
		if (snap_searches[i]->is_hl_selected() == 0) {
			continue;
		}

		TAILQ_INIT(&bblist);

  		snprintf(buf, BUFSIZ, "scanning %s ...", snap_searches[i]->get_name());
		buf[BUFSIZ - 1] = '\0';
		GUI_THREAD_ENTER();
		gtk_statusbar_push(GTK_STATUSBAR(import_window.statusbar), id, buf);
		GUI_THREAD_LEAVE();

		snap_searches[i]->region_match(import_window.img, &bblist);

  		snprintf(buf, BUFSIZ, "highlighting %s", snap_searches[i]->get_name());
		buf[BUFSIZ - 1] = '\0';
		GUI_THREAD_ENTER();
		gtk_statusbar_push(GTK_STATUSBAR(import_window.statusbar), id, buf);
		GUI_THREAD_LEAVE();


		/* for each bounding box, draw region on the overlay */
		TAILQ_FOREACH(cur_bb, &bblist, link) {
			if (use_box) { 
				image_draw_bbox_scale(hl_img, cur_bb, 1, mask, color);
			} else {
				image_fill_bbox_scale(hl_img, cur_bb, 1, hilitMask, hilit);
			}
			TAILQ_REMOVE(&bblist, cur_bb, link);
			free(cur_bb);
		}

		/* ask the windowing system to redraw the affected region */	
		GUI_THREAD_ENTER();
		gtk_widget_queue_draw_area(import_window.drawing_area, 0, 0,
			hl_img->width, hl_img->height);
		GUI_THREAD_LEAVE();

  		snprintf(buf, BUFSIZ, "done %s", snap_searches[i]->get_name());
		buf[BUFSIZ - 1] = '\0';
		GUI_THREAD_ENTER();
		gtk_statusbar_push(GTK_STATUSBAR(import_window.statusbar), id, buf);
		GUI_THREAD_LEAVE();
	}

	/* update statusbar */ snprintf(buf, BUFSIZ, "Highlight complete");
	buf[BUFSIZ - 1] = '\0';
	GUI_THREAD_ENTER();
	gtk_statusbar_push(GTK_STATUSBAR(import_window.statusbar), id, buf);
	GUI_THREAD_LEAVE();

	pthread_mutex_lock(&highlight_info.mutex);	
	highlight_info.thread_running = 0;
	// XXX ih_drop_ref(import_window.hooks, fhandle);
	pthread_mutex_unlock(&highlight_info.mutex);	

	pthread_exit(NULL);
	return NULL;
}



static void
cb_import_window(GtkWidget *window) 
{
	GUI_CALLBACK_ENTER();
	kill_highlight_thread(0);
	import_window.window = NULL;
	GUI_CALLBACK_LEAVE();
}




static void
remove_func(GtkWidget *widget, void *container) 
{
  	GUI_THREAD_CHECK(); 
  	gtk_container_remove(GTK_CONTAINER(container), widget);
}



static void
kill_highlight_thread(int run) 
{

	pthread_mutex_lock(&highlight_info.mutex);	
	if(highlight_info.thread_running) {
		int err = pthread_cancel(highlight_info.thread);
		//assert(!err);
		if(!err) {
			/* should do this in a cleanup function XXX */
			highlight_info.thread_running = 0;
			/* child should not be inside gui, since we
			 * are in a callback here, and presumable own
			 * the lock. */
			//GUI_THREAD_LEFT(); /* XXX */
			pthread_join(highlight_info.thread, NULL);
		}
	}
	highlight_info.thread_running = run;
	pthread_mutex_unlock(&highlight_info.mutex);
}

/* XXX move to more appropriate place */
static void load_import_file(const char *file);

static void
cb_have_sample_name(GtkWidget *widget, gpointer user_data)
{
	GtkWidget *file_selector = (GtkWidget *)user_data;
 	const gchar *selected_filename;

	GUI_CALLBACK_ENTER();

	selected_filename =
            gtk_file_selection_get_filename(GTK_FILE_SELECTION(file_selector));

	/* XXX somewhere else ?? */
	gtk_container_remove(GTK_CONTAINER(import_window.image_area),
                                     import_window.scroll);

	/* XXX error handling ??? */
	load_import_file(selected_filename);

	GUI_CALLBACK_LEAVE();
}


static void
cb_new_image(GtkWidget *widget, GdkEventButton *event, gpointer ptr) 
{

	GtkWidget *file_selector;
        GUI_CALLBACK_ENTER();

        /* Create the selector */
        file_selector = gtk_file_selection_new("Filter spec name");
        gtk_file_selection_show_fileop_buttons(GTK_FILE_SELECTION(file_selector));
	gtk_file_selection_complete(GTK_FILE_SELECTION(file_selector), "*.ppm");
                                                                                                  
        g_signal_connect(GTK_OBJECT (GTK_FILE_SELECTION(file_selector)->ok_button),
            "clicked", G_CALLBACK(cb_have_sample_name),
            (gpointer)file_selector);
                                                                                                  
	/*
     	 * Ensure that the dialog box is destroyed when the user 
	 * clicks a button. Use swapper here to get the right argument 
	 * to destroy (YUCK).
     	 */
        g_signal_connect_swapped(GTK_OBJECT(GTK_FILE_SELECTION(file_selector)->ok_button),
                            "clicked",
                            G_CALLBACK(gtk_widget_destroy),
                            (gpointer)file_selector);
                                                                                                  
        g_signal_connect_swapped(GTK_OBJECT(GTK_FILE_SELECTION(file_selector)->cancel_button),
                            "clicked",
                            G_CALLBACK (gtk_widget_destroy),
                            (gpointer) file_selector);
                                                                                                  
        /* Display that dialog */
        gtk_widget_show(file_selector);
        GUI_CALLBACK_LEAVE();
}

static void
cb_clear_select(GtkWidget *widget, GdkEventButton *event, gpointer ptr) 
{
	RGBImage *img;

	GUI_CALLBACK_ENTER();

	img = import_window.layers[IMP_SELECT_LAYER];

	rgbimg_clear(img);
	gtk_widget_queue_draw_area(import_window.drawing_area, 0, 0, img->width,
				   img->height);
	GUI_CALLBACK_LEAVE();

	import_window.nselections = 0;
}

static void
cb_clear_highlight_layer(GtkWidget *widget, GdkEventButton *event, gpointer ptr)
{
	RGBImage *img;

	GUI_CALLBACK_ENTER();
	
	kill_highlight_thread(0);

	img = import_window.layers[IMP_HIGHLIGHT_LAYER];

	rgbimg_clear(img);
	gtk_widget_queue_draw_area(import_window.drawing_area,
				   0, 0,
				   img->width,
				   img->height);

	GUI_CALLBACK_LEAVE();
}

static void
cb_run_highlight()
{

	GUI_CALLBACK_ENTER();

	kill_highlight_thread(1);
	int err = pthread_create(&highlight_info.thread, PATTR_DEFAULT, 
		image_highlight_main, NULL);
	assert(!err);

	GUI_CALLBACK_LEAVE();
}

static gboolean
cb_add_to_existing(GtkWidget *widget, GdkEventAny *event, gpointer data) 
{
	char buf[BUFSIZ] = "created new scene";
	GtkWidget *	active_item;
	img_search *ssearch;
	int	   	idx;
	guint	id;
  	GUI_CALLBACK_ENTER();

	active_item = gtk_menu_get_active(GTK_MENU(import_window.example_list));

	idx = (int)g_object_get_data(G_OBJECT(active_item), "user data");

	assert(idx >= 0);
	assert(idx < num_searches);

	ssearch = snap_searches[idx];

	for(int i=0; i<import_window.nselections; i++) {
		ssearch->add_patch(import_window.img, 
				 import_window.selections[i]);
  	}

	/* popup the edit window */ 
	ssearch->edit_search();

  	id = gtk_statusbar_get_context_id(GTK_STATUSBAR(import_window.statusbar),
					  "selection");
  	gtk_statusbar_push(GTK_STATUSBAR(import_window.statusbar), id, buf);
  
  	GUI_CALLBACK_LEAVE();
  	return TRUE;
}

static GtkWidget * make_highlight_table();

static void
search_popup_add(img_search *ssearch, int nsearch)
{
	GtkWidget *		item;

	/* see if the popup window exists, if not, then just return */
	if (import_window.window == NULL) {
		return;
	}
	/* Put the list of searches in the ones we can select in the popup menu */
	item = gtk_menu_item_new_with_label(ssearch->get_name());
	gtk_widget_show(item);
	/* XXX change to obj pointer */
	g_object_set_data(G_OBJECT(item), "user data", (void *)(nsearch - 1));
	gtk_menu_shell_append(GTK_MENU_SHELL(import_window.example_list), item);


}



/*
 * The callback function that takes user selected regions and creates
 * a new search with the list.
 */

static gboolean
cb_add_to_new(GtkWidget *widget, GdkEventAny *event, gpointer data) 
{
	GtkWidget *	active_item;
	GtkWidget *	dialog;
	GtkWidget *	label;
	img_search *ssearch;
	search_types_t	stype;
	int		idx;
	gint	result;
	const char *	sname;
  	GUI_CALLBACK_ENTER();

	active_item = gtk_menu_get_active(GTK_MENU(import_window.search_type));

	/* XXX can't directly get the cast to work ??? */ 
	idx = (int) g_object_get_data(G_OBJECT(active_item), "user data");
	stype = (search_types_t )idx;
   
	sname =  gtk_entry_get_text(GTK_ENTRY(import_window.search_name));
	if (strlen(sname) < 1) {
		show_popup_error("Filter name", "Please provide a name", import_window.window);
		GUI_CALLBACK_LEAVE();
		return(TRUE);
		/* XXX make sure the name already exists */
   	 }

	/* create the new search and put it in the global list */
	ssearch = create_search(stype, sname);
	assert(ssearch != NULL);


	/* put this in the list of searches */
	search_add_list(ssearch);

	/* put the patches into the newly created search */
	for(int i=0; i<import_window.nselections; i++) {
		ssearch->add_patch(import_window.img, import_window.selections[i]);
  	}
 
	/* popup the edit window */ 
	ssearch->edit_search();

	GUI_CALLBACK_LEAVE();
	return TRUE;
}



static void
clear_selection( GtkWidget *widget ) 
{
  bbox_t bbox;

  GUI_THREAD_CHECK();
  COORDS_TO_BBOX(bbox, import_window);

  image_fill_bbox_scale(import_window.layers[IMP_SELECT_LAYER], &bbox, 1, 
			colorMask, clearColor);

  /* refresh */
  gtk_widget_queue_draw_area(import_window.drawing_area,
			     bbox.min_x, bbox.min_y,
			     bbox.max_x - bbox.min_x + 1, 
			     bbox.max_y - bbox.min_y + 1);

}

static void
redraw_selections() 
{

  GUI_THREAD_CHECK();
  RGBImage *img = import_window.layers[IMP_SELECT_LAYER];

  rgbimg_clear(img);

  for(int i=0; i<import_window.nselections; i++) {
    image_fill_bbox_scale(import_window.layers[IMP_SELECT_LAYER], 
			  &import_window.selections[i], 
			  1, hilitMask, hilitRed);
    image_draw_bbox_scale(import_window.layers[IMP_SELECT_LAYER],
			  &import_window.selections[i], 
			  1, colorMask, red);
  }

  gtk_widget_queue_draw_area(import_window.drawing_area,
			     0, 0,
			     img->width,
			     img->height);
}

static void
draw_selection( GtkWidget *widget ) 
{
/*   GdkPixmap* pixmap; */
  bbox_t bbox;

  GUI_THREAD_CHECK();
  COORDS_TO_BBOX(bbox, import_window);

  image_fill_bbox_scale(import_window.layers[IMP_SELECT_LAYER], &bbox, 1, hilitMask, hilitRed);
  image_draw_bbox_scale(import_window.layers[IMP_SELECT_LAYER], &bbox, 1, colorMask, red);

  /* refresh */
  gtk_widget_queue_draw_area(import_window.drawing_area,
			     bbox.min_x, bbox.min_y,
			     bbox.max_x - bbox.min_x + 1, 
			     bbox.max_y - bbox.min_y + 1);


}

static gboolean
cb_button_press_event( GtkWidget      *widget,
		       GdkEventButton *event )
{

  GUI_CALLBACK_ENTER();

  if (event->button == 1) {
    import_window.x1 = (int)event->x;
    import_window.y1 = (int)event->y;
    import_window.x2 = (int)event->x;
    import_window.y2 = (int)event->y;
    import_window.button_down = 1;
  }

  GUI_CALLBACK_LEAVE();
  return TRUE;
}

static gboolean
cb_button_release_event(GtkWidget* widget, GdkEventButton *event)
{
	GUI_CALLBACK_ENTER();

	if (event->button != 1) {
		goto done;
	}

	if (import_window.button_down != 1) {
		goto done;
	}

	import_window.x2 = (int)event->x;
	import_window.y2 = (int)event->y;
	import_window.button_down = 0;

	draw_selection(widget);

	gtk_widget_grab_focus(import_window.select_button);


  	if (import_window.nselections >= MAX_SELECT) {
    		import_window.nselections--;	/* overwrite last one */
  	}
  	bbox_t bbox;
  	COORDS_TO_BBOX(bbox, import_window);
  	img_constrain_bbox(&bbox, import_window.img);

  	/* look for cases where box is too small */
  	if ((bbox.max_x - bbox.min_x) < MIN_DIMENSION) goto done;
  	if ((bbox.max_y - bbox.min_y) < MIN_DIMENSION) goto done;

  	import_window.selections[import_window.nselections++] = bbox;

done:
  	redraw_selections();
  	GUI_CALLBACK_LEAVE();
  	return TRUE;
}

static gboolean
cb_motion_notify_event( GtkWidget *widget,
			GdkEventMotion *event )
{
	int x, y;
  	GdkModifierType state;

  	GUI_CALLBACK_ENTER();

  	if (event->is_hint) {
    	gdk_window_get_pointer (event->window, &x, &y, &state);
  	} else {
    	x = (int)event->x;
    	y = (int)event->y;
    	state = (GdkModifierType)event->state;
  	}
  
  	if (state & GDK_BUTTON1_MASK && import_window.button_down) {
    	clear_selection(widget);
    	import_window.x2 = x;
    	import_window.y2 = y;

    	//draw_brush(widget);
    	draw_selection(widget);
 	}

  	GUI_CALLBACK_LEAVE();
  	return TRUE;
}

/*
 * Return a gtk_menu with a list of all all the existing
 * searches that use the example class.
 */

static GtkWidget *
get_example_menu(void)
{
	GtkWidget *     menu;
	GtkWidget *     item;
	img_search *	cur_search;
	int		i;
                                                                                
	menu = gtk_menu_new();
                         
	for (i=0;i<num_searches; i++) {
		cur_search = snap_searches[i];
		if (cur_search->is_example() == 0) {
			continue;
		}
		item = gtk_menu_item_new_with_label(cur_search->get_name());
		gtk_widget_show(item);
		g_object_set_data(G_OBJECT(item), "user data", (void *)i);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	}
                                       
	return(menu);
}
                                                                                
/*
 * Return a gtk_menu with a list of all the example
 * based searches.  This should be done using other state
 * instead of statically defined. XXX.
 */
static GtkWidget *
get_example_searches_menu(void)
{
        GtkWidget *     menu;
        GtkWidget *     item;
                                                                                
        menu = gtk_menu_new();
                                                                                
        item = gtk_menu_item_new_with_label("Texture Search");
        gtk_widget_show(item);
        g_object_set_data(G_OBJECT(item), "user data", (void *)TEXTURE_SEARCH);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
                                                                                
        item = gtk_menu_item_new_with_label("RGB Histogram");
        gtk_widget_show(item);
        g_object_set_data(G_OBJECT(item), "user data",
                (void *)RGB_HISTO_SEARCH);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
                                                                                
        return(menu);
}
                                                                                


static GtkWidget *
new_search_panel(void)
{
	GtkWidget *box;
	GtkWidget *hbox;
	GtkWidget *frame;
	GtkWidget *widget;

  	frame = gtk_frame_new("Create New Search");
		  
	box = gtk_vbox_new(FALSE, 10);
	gtk_container_set_border_width(GTK_CONTAINER(box), 10);
	gtk_container_add(GTK_CONTAINER(frame), box);

	/*
	 * Create a hbox that has the controls for
	 * adding a new search with examples to the existing
	 * searches.
	 */ 

	GtkWidget *button = gtk_button_new_with_label ("Create");
	import_window.select_button = button;
	g_signal_connect_after(GTK_OBJECT(button), "clicked",
	   GTK_SIGNAL_FUNC(cb_add_to_new), NULL);
	gtk_box_pack_start(GTK_BOX(box), button, TRUE, FALSE, 0);


	hbox = gtk_hbox_new(FALSE, 10);
	gtk_box_pack_start(GTK_BOX(box), hbox, TRUE, FALSE, 0);

	widget = gtk_label_new("Type");
	gtk_box_pack_start(GTK_BOX(hbox), widget, TRUE, FALSE, 0);

	import_window.search_type =  get_example_searches_menu();
	widget = gtk_option_menu_new();
	gtk_option_menu_set_menu(GTK_OPTION_MENU(widget), 
		import_window.search_type);
	gtk_box_pack_start(GTK_BOX(hbox), widget, TRUE, FALSE, 0);


	hbox = gtk_hbox_new(FALSE, 10);
	gtk_box_pack_start(GTK_BOX(box), hbox, TRUE, FALSE, 0);

	widget = gtk_label_new("Name");
	gtk_box_pack_start(GTK_BOX(hbox), widget, TRUE, FALSE, 0);

	import_window.search_name = gtk_entry_new();
	gtk_entry_set_activates_default(GTK_ENTRY(import_window.search_name),
		TRUE);
	gtk_box_pack_start(GTK_BOX(hbox), import_window.search_name, 
		TRUE, FALSE, 0);

	gtk_widget_show_all(GTK_WIDGET(frame));
	return(frame);
}

static GtkWidget *
existing_search_panel(void)
{
	GtkWidget *box;
	GtkWidget *hbox;
	GtkWidget *frame;
	GtkWidget *widget;

  	frame = gtk_frame_new("Add to Existing Search");
		  
	box = gtk_vbox_new(FALSE, 10);
	gtk_container_set_border_width(GTK_CONTAINER(box), 10);
	gtk_container_add(GTK_CONTAINER(frame), box);

	/*
	 * Create a hbox that has the controls for
	 * adding a new search with examples to the existing
	 * searches.
	 */ 

	GtkWidget *button = gtk_button_new_with_label ("Add");
	import_window.select_button = button;
	g_signal_connect_after(GTK_OBJECT(button), "clicked",
	   GTK_SIGNAL_FUNC(cb_add_to_existing), NULL);
	gtk_box_pack_start (GTK_BOX(box), button, TRUE, FALSE, 0);

	hbox = gtk_hbox_new(FALSE, 10);
	gtk_box_pack_start(GTK_BOX(box), hbox, TRUE, FALSE, 0);

	widget = gtk_label_new("Search");
	gtk_box_pack_start(GTK_BOX(hbox), widget, TRUE, FALSE, 0);

	import_window.example_list =  get_example_menu();
	import_window.opt_menu = gtk_option_menu_new();
	gtk_option_menu_set_menu(GTK_OPTION_MENU(import_window.opt_menu), 
		import_window.example_list);
	gtk_box_pack_start(GTK_BOX(hbox), import_window.opt_menu, TRUE, FALSE, 0);

	gtk_widget_show_all(GTK_WIDGET(frame));
	return(frame);
}

void
import_update_searches()
{
	if (import_window.window == NULL) {
		return;
	}

	/* add this to the option menu */
	gtk_option_menu_remove_menu(GTK_OPTION_MENU(import_window.opt_menu));
	/* XXX free old one ?? */
	import_window.example_list =  get_example_menu();
	gtk_option_menu_set_menu(GTK_OPTION_MENU(import_window.opt_menu), 
		import_window.example_list);
	gtk_widget_show_all(GTK_WIDGET(import_window.opt_menu));

	gtk_container_remove(GTK_CONTAINER(import_window.hl_frame), 
		import_window.hl_table);
	import_window.hl_table = make_highlight_table();
	gtk_container_add(GTK_CONTAINER(import_window.hl_frame), import_window.hl_table);
    gtk_widget_show_all(import_window.hl_frame);
}

#define	MAX_SEARCHES	64	/* XXX horible */
static GtkWidget *
make_highlight_table()
{
    GtkWidget *table;
    GtkWidget *widget;
    int row = 0;        /* current table row */
	int			i;

    table = gtk_table_new(MAX_SEARCHES+1, 3, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 4);
    gtk_container_set_border_width(GTK_CONTAINER(table), 10);

    widget = gtk_label_new("Predicate");
    gtk_label_set_justify(GTK_LABEL(widget), GTK_JUSTIFY_LEFT);
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, row, row+1);

    widget = gtk_label_new("Description");
    gtk_label_set_justify(GTK_LABEL(widget), GTK_JUSTIFY_LEFT);
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 1, 2, row, row+1);

    widget = gtk_label_new("Edit");
    gtk_label_set_justify(GTK_LABEL(widget), GTK_JUSTIFY_LEFT);
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 2, 3, row, row+1); 
	for (i=0; i < num_searches; i++) {
		row = i + 1;
		widget = snap_searches[i]->get_highlight_widget();
		gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, row, row+1);
		widget = snap_searches[i]->get_config_widget();
		gtk_table_attach_defaults(GTK_TABLE(table), widget, 1, 2, row, row+1);
		widget = snap_searches[i]->get_edit_widget();
		gtk_table_attach_defaults(GTK_TABLE(table), widget, 2, 3, row, row+1);
	}
    gtk_widget_show_all(table);

	return(table);
}


#define	MAX_SEARCHES	64	/* XXX horible */
static GtkWidget *
highlight_select()
{
    GtkWidget *box1;

    box1 = gtk_vbox_new (FALSE, 0);
    gtk_widget_show (box1);

    import_window.hl_frame = gtk_frame_new("Searches");

	import_window.hl_table = make_highlight_table();
	gtk_container_add(GTK_CONTAINER(import_window.hl_frame), import_window.hl_table);
    gtk_box_pack_start(GTK_BOX(box1), import_window.hl_frame, FALSE, FALSE, 10);
    gtk_widget_show_all(box1);

	return(box1);
}


static GtkWidget *
highlight_panel(void)
{
	GtkWidget	*frame;
	GtkWidget	*box;

  	frame = gtk_frame_new("Highlighting");
  	gtk_widget_show(frame);

  	/* start button area */
  	GtkWidget *box2 = gtk_vbox_new (FALSE, 10);
  	gtk_container_set_border_width (GTK_CONTAINER (box2), 10);
  	gtk_container_add(GTK_CONTAINER(frame), box2);
  	gtk_widget_show (box2);

  	GtkWidget *label = gtk_label_new("Highlight regions matching seaches");
  	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
  	gtk_box_pack_start (GTK_BOX (box2), label, FALSE, TRUE, 0);


	box = highlight_select();
  	gtk_box_pack_start(GTK_BOX(box2), box, FALSE, TRUE, 0);

	box = gtk_hbox_new(TRUE, 10);
	gtk_box_pack_start(GTK_BOX(box2), box, FALSE, TRUE, 0);

	import_window.drawbox = gtk_radio_button_new_with_label(NULL, "Outline");
    gtk_box_pack_start(GTK_BOX(box), import_window.drawbox, FALSE, TRUE, 0);
    import_window.drawhl = gtk_radio_button_new_with_label_from_widget(
        GTK_RADIO_BUTTON(import_window.drawbox), "Shade");
    gtk_box_pack_start(GTK_BOX(box), import_window.drawhl, FALSE, TRUE, 0);

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(import_window.drawhl), TRUE);
                                                                                     
  	/* Buttons to draw/clear the highlighting */
  	box = gtk_hbox_new(TRUE, 10);
  	gtk_box_pack_start (GTK_BOX(box2), box, TRUE, TRUE, 0);


  	GtkWidget *button = gtk_button_new_with_label ("Highlight");
  	g_signal_connect_after(GTK_OBJECT(button), "clicked",
			 	GTK_SIGNAL_FUNC(cb_run_highlight), NULL);
  	gtk_box_pack_start (GTK_BOX (box), button, TRUE, TRUE, 0);
  	GTK_WIDGET_SET_FLAGS (button, GTK_CAN_DEFAULT);

  	button = gtk_button_new_with_label ("Clear");
  	g_signal_connect_after(GTK_OBJECT(button), "clicked",
					 GTK_SIGNAL_FUNC(cb_clear_highlight_layer), NULL);
  	gtk_box_pack_start (GTK_BOX (box), button, TRUE, TRUE, 0);

	gtk_widget_show_all(frame);
	return(frame);
}

static void
load_import_file(const char *file)
{
	GtkWidget *eb;
	GtkWidget *image;
        char buf[MAX_NAME];


	/* open the and create RGB image */
	import_window.nselections = 0;

	import_window.img = create_rgb_image(file);
	import_window.button_down = 0;
	assert(import_window.img != NULL);


	import_window.layers[IMP_IMG_LAYER] = import_window.img;
	for(int i=IMP_IMG_LAYER+1; i<IMP_MAX_LAYERS; i++) {
		import_window.layers[i] = rgbimg_new(import_window.img); 
		rgbimg_clear(import_window.layers[i]);
	}

	/* put up the title */
	sprintf(buf, "Example Image: %s", file);
	gtk_window_set_title(GTK_WINDOW(import_window.window), buf);

	/* XXX free the old widgets ??? */

	image = import_window.drawing_area = gtk_drawing_area_new();
	GTK_WIDGET_UNSET_FLAGS(image, GTK_CAN_DEFAULT);
	gtk_drawing_area_size(GTK_DRAWING_AREA(image), 
			      import_window.img->width,
			      import_window.img->height);
	gtk_signal_connect(GTK_OBJECT(image), "expose-event",
			   GTK_SIGNAL_FUNC(expose_event), import_window.img);
	gtk_signal_connect(GTK_OBJECT(image), "realize",
			   GTK_SIGNAL_FUNC(realize_event), NULL);

	import_window.scroll = gtk_scrolled_window_new(NULL, NULL);

	eb = gtk_event_box_new();
	gtk_object_set_user_data(GTK_OBJECT(eb), NULL);
	gtk_container_add(GTK_CONTAINER(eb), image);
	gtk_widget_show(eb);

	/* additional events for selection */
	g_signal_connect(G_OBJECT (eb), "motion_notify_event",
			  G_CALLBACK (cb_motion_notify_event), NULL);
	g_signal_connect(G_OBJECT (eb), "button_press_event",
			  G_CALLBACK (cb_button_press_event), NULL);
	g_signal_connect(G_OBJECT (eb), "button_release_event",
			  G_CALLBACK (cb_button_release_event), NULL);
	gtk_widget_set_events (eb, GDK_EXPOSURE_MASK
			       | GDK_LEAVE_NOTIFY_MASK
			       | GDK_BUTTON_PRESS_MASK
			       | GDK_POINTER_MOTION_MASK
			       | GDK_POINTER_MOTION_HINT_MASK);


	gtk_scrolled_window_add_with_viewport(
		GTK_SCROLLED_WINDOW(import_window.scroll), eb);
	gtk_widget_show(image);
	gtk_widget_show(import_window.scroll);
	gtk_container_add(GTK_CONTAINER(import_window.image_area), 
		import_window.scroll);

	gtk_widget_queue_resize(import_window.window);
	gtk_widget_show(import_window.window);

	return;
}

void
open_import_window() 
{
	GtkWidget *frame;
	GtkWidget *button;
	GtkWidget *widget;
	GtkWidget *hbox;

	if (import_window.window == NULL) {
		import_window.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
		gtk_window_set_title(GTK_WINDOW(import_window.window), "Image");
		gtk_window_set_default_size(GTK_WINDOW(import_window.window), 850, 350);
		g_signal_connect(G_OBJECT(import_window.window), "destroy",
				  G_CALLBACK(cb_import_window), NULL);

		GtkWidget *box1 = gtk_vbox_new(FALSE, 0);

		import_window.statusbar = gtk_statusbar_new();
		gtk_box_pack_end(GTK_BOX(box1), import_window.statusbar, FALSE, FALSE, 0);
		gtk_widget_show(import_window.statusbar);

		GtkWidget *pane = gtk_hpaned_new();
		gtk_box_pack_start(GTK_BOX(box1), pane, TRUE, TRUE, 0);
		gtk_widget_show(pane);

		gtk_container_add(GTK_CONTAINER(import_window.window), box1);
		gtk_widget_show(box1);

		/* box to hold controls */
		box1 = gtk_vbox_new(FALSE, 10);
		gtk_container_set_border_width(GTK_CONTAINER(box1), 4);
		gtk_widget_show(box1);
		gtk_paned_pack1(GTK_PANED(pane), box1, FALSE, TRUE);

		/* XXX */
		hbox = gtk_hbox_new(FALSE, 10);
	  	gtk_box_pack_start(GTK_BOX(box1), hbox, TRUE, FALSE, 0);
	  	button = gtk_button_new_with_label("New Image");
	  	g_signal_connect_after(GTK_OBJECT(button), "clicked",
				 GTK_SIGNAL_FUNC(cb_new_image), NULL);
	  	gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, FALSE, 0);
		gtk_widget_show_all(hbox);
		

		/* 
		 * Refinement
		 */			
		{
		  frame = gtk_frame_new("Search Update");
		  gtk_box_pack_end(GTK_BOX (box1), frame, FALSE, FALSE, 0);
		  gtk_widget_show(frame);
		  
		  GtkWidget *box2 = gtk_vbox_new (FALSE, 10);
		  gtk_container_set_border_width (GTK_CONTAINER (box2), 10);
		  gtk_container_add(GTK_CONTAINER(frame), box2);
		  gtk_widget_show (box2);
		  
		  /* hbox */
		  GtkWidget *hbox = gtk_hbox_new(FALSE, 10);
		  gtk_box_pack_start (GTK_BOX(box2), hbox, TRUE, FALSE, 0);
		  gtk_widget_show(hbox);

		  /* couple of buttons */
		
		  GtkWidget *buttonbox = gtk_vbox_new(FALSE, 10);
		  gtk_box_pack_start(GTK_BOX(box2), buttonbox, TRUE, FALSE,0);
		  gtk_widget_show(buttonbox);
		
		  /*
 		   * Create a hbox that has the controls for
 		   * adding a new search with examples to the existing
		   * searches.
		   */ 

		  hbox = gtk_hbox_new(FALSE, 10);
		  gtk_box_pack_start (GTK_BOX(buttonbox), hbox, TRUE, FALSE, 0);
		  gtk_widget_show(hbox);

		  widget = new_search_panel();
		  gtk_box_pack_start(GTK_BOX(hbox), widget, TRUE, FALSE, 0);

		  widget = existing_search_panel();
		  gtk_box_pack_start(GTK_BOX(hbox), widget, TRUE, FALSE, 0);


		  /* control button to clear selected regions */
		  hbox = gtk_hbox_new(FALSE, 10);
		  gtk_box_pack_start (GTK_BOX(buttonbox), hbox, TRUE, FALSE, 0);
		  gtk_widget_show(hbox);

		  button = gtk_button_new_with_label ("Clear");
		  g_signal_connect_after(GTK_OBJECT(button), "clicked",
				 GTK_SIGNAL_FUNC(cb_clear_select), NULL);
		  gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, FALSE, 0);
		  gtk_widget_show (button);
		}

		/* Get highlighting state */
		frame = highlight_panel();
  		gtk_box_pack_end (GTK_BOX (box1), frame, FALSE, FALSE, 0);

		import_window.image_area = gtk_viewport_new(NULL, NULL);
		gtk_widget_show(import_window.image_area);
		gtk_paned_pack2(GTK_PANED(pane), import_window.image_area, TRUE, TRUE);

		load_import_file("diamond1.ppm");

	} else {
		kill_highlight_thread(0);
		//gtk_container_remove(GTK_CONTAINER(import_window.image_area), 
				     //import_window.scroll);
		gdk_window_raise(GTK_WIDGET(import_window.window)->window);
	}

}