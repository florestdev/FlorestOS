#pragma once
#include "types.h"

void mouse_init(void);
void mouse_disable(void); /* stop streaming + flush */
void mouse_poll(void);

int  mouse_x(void);
int  mouse_y(void);
bool mouse_left_pressed(void);
bool mouse_left_down(void);
void mouse_draw_cursor(void);
void mouse_hide_cursor(void);
