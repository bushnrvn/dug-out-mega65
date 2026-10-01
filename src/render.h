#ifndef RENDER_H
#define RENDER_H
void render_init(void);     /* video mode, palettes, tile graphics, Doug's sprite */
void render_frame(void);    /* draw the whole game state and show it */
void render_banner(const char *a, const char *b);   /* a banner over the field: one or two lines (b may be 0) */
void render_banner_off(void);
#endif
