#ifndef GRAPHICS_H
#define GRAPHICS_H

void graphics_init();

void graphics_clear();
void graphics_showLabel(int slot, uint16_t color, String str);
void graphics_println(String str);


#endif
