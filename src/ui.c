#include "ui.h"

#define ICON_COLOR         BLACK
#define BUTTON_COLOR       WHITE
#define BUTTON_SHADE_COLOR BLACK
#define BUTTON_RIM_COLOR   GRAY

Vector2 GetMousePositionDPI(void)
{
    return Vector2Multiply(GetMousePosition(), GetWindowScaleDPI());
}

void box_merge(Rectangle *a, Rectangle b)
{
    if (b.x < a->x) {
        a->x = b.x;
    }
    if (b.y < a->y) {
        a->y = b.y;
    }
    if (a->x + a->width < b.x + b.width) {
        a->width = b.x + b.width;
    }
    if (a->y + a->height < b.y + b.height) {
        a->height = b.y + b.height;
    }
}

Rectangle box_pad(Rectangle a, float pad)
{
    a.x      -= pad;
    a.y      -= pad;
    a.width  += pad*2;
    a.height += pad*2;
    return a;
}

void play_icon(Rectangle box)
{
    float icon_padding_left   = 0.3*box.width;
    float icon_padding_right  = 0.2*box.width;
    float icon_padding_top    = 0.2*box.width;
    float icon_padding_bottom = 0.2*box.width;
    Vector2 v1 = {box.x + icon_padding_left, box.y + icon_padding_top};
    Vector2 v2 = {box.x + icon_padding_left, box.y + box.height - icon_padding_bottom};
    Vector2 v3 = {box.x + box.width - icon_padding_right, box.y + box.height*0.5};
    DrawTriangle(v1, v2, v3, ICON_COLOR);
}

void pause_icon(Rectangle box)
{
    float icon_padding = 0.2*box.width;
    Rectangle pause_icon_box = {
        .x      = box.x + icon_padding,
        .y      = box.y + icon_padding,
        .width  = box.width - icon_padding - icon_padding,
        .height = box.height - icon_padding - icon_padding,
    };
    DrawRectangleRec((Rectangle) {
        .x      = pause_icon_box.x,
        .y      = pause_icon_box.y,
        .width  = pause_icon_box.width/3.,
        .height = pause_icon_box.height,
    }, ICON_COLOR);
    DrawRectangleRec((Rectangle) {
        .x      = pause_icon_box.x + pause_icon_box.width*2./3.,
        .y      = pause_icon_box.y,
        .width  = pause_icon_box.width/3.,
        .height = pause_icon_box.height,
    }, ICON_COLOR);
}

void reset_icon(Rectangle box)
{
    float radius = 0.35*fminf(box.width, box.height);

    Vector2 center = {
        .x = box.x + box.width*0.5,
        .y = box.y + box.height*0.5,
    };

    float gap_angle = 50;
    DrawCircleSector(center, radius, gap_angle*0.5, 360 - gap_angle*0.5, 20, ICON_COLOR);
    DrawCircleV(center, radius*0.5, BUTTON_COLOR);

    // Arrow
    {
        Vector2 v = {1, 0};
        v = Vector2Rotate(v, -0.5*gap_angle*DEG2RAD);

        Vector2 c = center;
        c = Vector2Add(c, Vector2Scale(v, radius*0.75));

        float size = 0.65*radius;
        v = Vector2Rotate(v, 85*DEG2RAD);
        Vector2 v1 = Vector2Add(c, Vector2Scale(v, size));
        Vector2 v2 = Vector2Add(c, Vector2Scale(Vector2Rotate(v, -120*DEG2RAD), size));
        Vector2 v3 = Vector2Add(c, Vector2Scale(Vector2Rotate(v, 120*DEG2RAD), size));
        DrawTriangle(v1, v2, v3, ICON_COLOR);
    }
}

bool button(bool *down, Rectangle button_box, void (*icon)(Rectangle box), Camera2D camera)
{
    bool hover = CheckCollisionPointRec(GetScreenToWorld2D(GetMousePositionDPI(), camera), button_box);
    float unit = fminf(button_box.width, button_box.height);
    float thicc = 0.1*unit;
    Vector2 v1 = {button_box.x, button_box.y};
    Vector2 v2 = {button_box.x, button_box.y + button_box.height};
    Vector2 v3 = {button_box.x + button_box.width, button_box.y};
    Vector2 v4 = {button_box.x + button_box.width, button_box.y + button_box.height};
    if (*down) {
        DrawTriangle(v1, v2, v3, BUTTON_SHADE_COLOR);
        DrawTriangle(v3, v2, v4, BUTTON_RIM_COLOR);
    } else {
        DrawTriangle(v1, v2, v3, BUTTON_RIM_COLOR);
        DrawTriangle(v3, v2, v4, BUTTON_SHADE_COLOR);
    }
    button_box = box_pad(button_box, -thicc);
    DrawRectangleRec(button_box, BUTTON_COLOR);
    if (*down) {
        button_box.x += 0.02*unit;
        button_box.y += 0.02*unit;
        icon(button_box);
    } else {
        icon(button_box);
    }
    if (!*down) {
        if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            *down = true;
        }
    } else {
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            *down = false;
            return hover;
        }
    }
    return false;
}
