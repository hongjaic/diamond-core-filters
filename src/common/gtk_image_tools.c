
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include <opencv/cv.h>
#include <gtk/gtk.h>

#include "rgb.h"
#include "gtk_image_tools.h"

/*
 * build a pixbuf from an rgbimage.
 */
GdkPixbuf      *
rgbimage_to_pixbuf(RGBImage * img)
{
    GdkPixbuf      *pbuf;

    /*
     * NB pixbuf refers to data 
     */
    pbuf = gdk_pixbuf_new_from_data((const guchar *) &img->data[0],
                                    GDK_COLORSPACE_RGB, 1, 8,
                                    img->columns, img->rows,
                                    (img->columns * sizeof(RGBPixel)),
                                    NULL, NULL);
    if (pbuf == NULL) {
        printf("failed to allocate pbuf\n");
        exit(1);
    }
    return pbuf;
}


/*
 * Build GTK image from an RGB image.
 */
GtkWidget      *
rgbimage_to_gtkimage(RGBImage * img)
{
    GdkPixbuf      *pbuf;
    GtkWidget      *image;

    pbuf = gdk_pixbuf_new_from_data((const guchar *) &img->data[0],
                                    GDK_COLORSPACE_RGB, 1, 8,
                                    img->columns, img->rows,
                                    (img->columns * sizeof(RGBPixel)),
                                    NULL, NULL);
    if (pbuf == NULL) {
        printf("failed to allocate pbuf\n");
        exit(1);
    }

    image = gtk_image_new_from_pixbuf(pbuf);
    assert(image);
    return image;
}