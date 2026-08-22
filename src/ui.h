#ifndef UI_H_
#define UI_H_

Vector2 GetMousePositionDPI(void);
Rectangle box_pad(Rectangle a, float pad);
void box_merge(Rectangle *a, Rectangle b);
void play_icon(Rectangle box);
void pause_icon(Rectangle box);
void reset_icon(Rectangle box);
bool button(bool *down, Rectangle button_box, void (*icon)(Rectangle button_box), Camera2D camera);

#endif // UI_H_
