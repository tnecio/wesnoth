#pragma once
/* Headless mock: SDL_rect.h */
#include "SDL_stdinc.h"

typedef struct SDL_Point { int x, y; } SDL_Point;
typedef struct SDL_FPoint { float x, y; } SDL_FPoint;
typedef struct SDL_Rect { int x, y, w, h; } SDL_Rect;
typedef struct SDL_FRect { float x, y, w, h; } SDL_FRect;

static inline SDL_bool SDL_RectEmpty(const SDL_Rect* r) {
    return (!r || r->w <= 0 || r->h <= 0) ? SDL_TRUE : SDL_FALSE;
}
static inline SDL_bool SDL_RectEquals(const SDL_Rect* a, const SDL_Rect* b) {
    return (a && b && a->x==b->x && a->y==b->y && a->w==b->w && a->h==b->h) ? SDL_TRUE : SDL_FALSE;
}
static inline SDL_bool SDL_PointInRect(const SDL_Point* p, const SDL_Rect* r) {
    return (p && r && p->x >= r->x && p->x < (r->x+r->w) && p->y >= r->y && p->y < (r->y+r->h)) ? SDL_TRUE : SDL_FALSE;
}
static inline SDL_bool SDL_HasIntersection(const SDL_Rect* a, const SDL_Rect* b) {
    if(!a || !b || SDL_RectEmpty(a) || SDL_RectEmpty(b)) return SDL_FALSE;
    return (a->x < b->x+b->w && a->x+a->w > b->x && a->y < b->y+b->h && a->y+a->h > b->y) ? SDL_TRUE : SDL_FALSE;
}
static inline SDL_bool SDL_IntersectRect(const SDL_Rect* a, const SDL_Rect* b, SDL_Rect* result) {
    if(!SDL_HasIntersection(a, b)) { if(result) { result->x=result->y=result->w=result->h=0; } return SDL_FALSE; }
    int x1 = a->x > b->x ? a->x : b->x;
    int y1 = a->y > b->y ? a->y : b->y;
    int x2 = (a->x+a->w) < (b->x+b->w) ? (a->x+a->w) : (b->x+b->w);
    int y2 = (a->y+a->h) < (b->y+b->h) ? (a->y+a->h) : (b->y+b->h);
    if(result) { result->x=x1; result->y=y1; result->w=x2-x1; result->h=y2-y1; }
    return SDL_TRUE;
}
static inline void SDL_UnionRect(const SDL_Rect* a, const SDL_Rect* b, SDL_Rect* result) {
    if(!result) return;
    int x1 = a->x < b->x ? a->x : b->x;
    int y1 = a->y < b->y ? a->y : b->y;
    int x2 = (a->x+a->w) > (b->x+b->w) ? (a->x+a->w) : (b->x+b->w);
    int y2 = (a->y+a->h) > (b->y+b->h) ? (a->y+a->h) : (b->y+b->h);
    result->x=x1; result->y=y1; result->w=x2-x1; result->h=y2-y1;
}
