#ifndef VKWIDGET_H
#define VKWIDGET_H

#include "vke.h"
#include "vkvg.h"

class vkWidget
{
public:
	int32_t top;
	int32_t left;
	uint32_t width;
	uint32_t height;

	vkvg_color_t background = {0.2,0.2,0.2,0.5};

	vkWidget() {

	}

	void onDraw (VkvgContext ctx) {
		vkvg_save(ctx);
		vkvg_rectangle(ctx, (float)left, (float)top, (float)width, (float)height);
		vkvg_clip(ctx);
		vkvg_set_source_rgba(ctx, background.r, background.g, background.b, background.a);
		vkvg_paint(ctx);
		vkvg_restore(ctx);
	}
};

#endif // VKWIDGET_H
