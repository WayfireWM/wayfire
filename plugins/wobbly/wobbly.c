/*
 * Copyright © 2005 Novell, Inc.
 * Copyright © 2014 Scott Moreau
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without
 * fee, provided that the above copyright notice appear in all copies
 * and that both that copyright notice and this permission notice
 * appear in supporting documentation, and that the name of
 * Novell, Inc. not be used in advertising or publicity pertaining to
 * distribution of the software without specific, written prior permission.
 * Novell, Inc. makes no representations about the suitability of this
 * software for any purpose. It is provided "as is" without express or
 * implied warranty.
 *
 * NOVELL, INC. DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN
 * NO EVENT SHALL NOVELL, INC. BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Author: David Reveman <davidr@novell.com>
 *         Scott Moreau <oreaus@gmail.com>
 */

/*
 * Spring model implemented by Kristian Hogsberg.
 */

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>

#include "wobbly.h"

#define GRID_WIDTH  4
#define GRID_HEIGHT 4

#define MODEL_MAX_SPRINGS (GRID_WIDTH * GRID_HEIGHT * 2)

typedef struct _xy_pair {
    float x, y;
} Point, Vector;

typedef struct _Edge {
    float next, prev;

    float start;
    float end;

    float attract;
    float velocity;
} Edge;

typedef struct _Object {
    Vector	 force;
    Point	 position;
    Vector	 velocity;
    float	 theta;
    int		 immobile;
    Edge	 vertEdge;
    Edge	 horzEdge;
} Object;

typedef struct _Spring {
    Object *a;
    Object *b;
    Vector offset;
} Spring;

typedef struct _Model {
    Object	 *objects;
    int		 numObjects;
    Spring	 springs[MODEL_MAX_SPRINGS];
    int		 numSprings;
    Object	 *anchorObject;
    float	 steps;
    Point	 topLeft;
    Point	 bottomRight;
} Model;

typedef struct _WobblyWindow {
    Model        *model;
    int          wobbly;
    int	        grabbed;
    int	       velocity;
    int         grab_dx;
    int         grab_dy;
    unsigned int  state;
} WobblyWindow;

#define WobblyInitial  (1L << 0)
#define WobblyForce    (1L << 1)
#define WobblyVelocity (1L << 2)

static void objectInit(Object *object, float positionX, float positionY,
        float velocityX, float velocityY)
{
    object->force.x = 0;
    object->force.y = 0;

    object->position.x = positionX;
    object->position.y = positionY;

    object->velocity.x = velocityX;
    object->velocity.y = velocityY;

    object->theta    = 0;
    object->immobile = 0;

    object->vertEdge.next = 0.0f;
    object->horzEdge.next = 0.0f;
}

static void springInit(Spring *spring, Object *a, Object *b,
	    float offsetX, float offsetY)
{
    spring->a	     = a;
    spring->b	     = b;
    spring->offset.x = offsetX;
    spring->offset.y = offsetY;
}

static void modelCalcBounds(Model *model)
{
    int i;

    model->topLeft.x	 = SHRT_MAX;
    model->topLeft.y	 = SHRT_MAX;
    model->bottomRight.x = SHRT_MIN;
    model->bottomRight.y = SHRT_MIN;

    for (i = 0; i < model->numObjects; i++)
    {
        if (model->objects[i].position.x < model->topLeft.x)
            model->topLeft.x = model->objects[i].position.x;
        else if (model->objects[i].position.x > model->bottomRight.x)
            model->bottomRight.x = model->objects[i].position.x;

        if (model->objects[i].position.y < model->topLeft.y)
            model->topLeft.y = model->objects[i].position.y;
        else if (model->objects[i].position.y > model->bottomRight.y)
            model->bottomRight.y = model->objects[i].position.y;
    }
}

static void modelAddSpring(Model *model, Object *a, Object *b,
		float offsetX, float offsetY)
{
    Spring *spring;

    spring = &model->springs[model->numSprings];
    model->numSprings++;

    springInit (spring, a, b, offsetX, offsetY);
}

static void modelSetMiddleAnchor(Model *model, int x, int y,
        int width, int height)
{
    float gx, gy;

    gx = ((GRID_WIDTH  - 1) / 2 * width)  / (float) (GRID_WIDTH  - 1);
    gy = ((GRID_HEIGHT - 1) / 2 * height) / (float) (GRID_HEIGHT - 1);

    if (model->anchorObject)
        model->anchorObject->immobile = 0;

    model->anchorObject =
        &model->objects[GRID_WIDTH * ((GRID_HEIGHT-1)/2) + (GRID_WIDTH-1)/ 2];
    model->anchorObject->position.x = x + gx;
    model->anchorObject->position.y = y + gy;

    model->anchorObject->immobile = 1;
}

static void modelSetTopAnchor(Model *model, int x, int y,
        int width)
{
    float gx;

    gx = ((GRID_WIDTH  - 1) / 2 * width)  / (float) (GRID_WIDTH  - 1);

    if (model->anchorObject)
	model->anchorObject->immobile = 0;

    model->anchorObject = &model->objects[(GRID_WIDTH-1)/ 2];
    model->anchorObject->position.x = x + gx;
    model->anchorObject->position.y = y;

    model->anchorObject->immobile = 1;
}

static void modelInitObjects(Model *model, int x, int y, int width, int height)
{
    int	  gridX, gridY, i = 0;
    float gw, gh;

    gw = GRID_WIDTH  - 1;
    gh = GRID_HEIGHT - 1;

    for (gridY = 0; gridY < GRID_HEIGHT; gridY++)
    {
        for (gridX = 0; gridX < GRID_WIDTH; gridX++)
        {
            objectInit (&model->objects[i],
                    x + (gridX * width) / gw,
                    y + (gridY * height) / gh,
                    0, 0);
            i++;
        }
    }

    if (!model->anchorObject)
        modelSetMiddleAnchor (model, x, y, width, height);
}

static void modelInitSprings(Model *model, int width, int height)
{
    int   gridX, gridY, i = 0;
    float hpad, vpad;

    model->numSprings = 0;

    hpad = ((float) width) / (GRID_WIDTH  - 1);
    vpad = ((float) height) / (GRID_HEIGHT - 1);

    for (gridY = 0; gridY < GRID_HEIGHT; gridY++)
    {
        for (gridX = 0; gridX < GRID_WIDTH; gridX++)
        {
            if (gridX > 0)
            {
                modelAddSpring (model, &model->objects[i - 1],
                        &model->objects[i], hpad, 0);
            }

            if (gridY > 0)
            {
                modelAddSpring (model, &model->objects[i - GRID_WIDTH],
                        &model->objects[i], 0, vpad);
            }

            i++;
        }
    }
}

static Model * createModel(int x, int y, int width, int height)
{
    Model *model;

    model = malloc(sizeof(Model));
    if (!model)
        return 0;

    model->numObjects = GRID_WIDTH * GRID_HEIGHT;
    model->objects = malloc (sizeof (Object) * model->numObjects);
    if (!model->objects)
    {
        free (model);
        return 0;
    }

    model->anchorObject = 0;
    model->numSprings = 0;
    model->steps = 0;

    modelInitObjects (model, x, y, width, height);
    modelInitSprings (model, width, height);
    modelCalcBounds (model);

    return model;
}

static void objectApplyForce(Object *object, float fx, float fy)
{
    object->force.x += fx;
    object->force.y += fy;
}

static void springExertForces(Spring *spring, float k)
{
    Vector da, db;
    Vector a, b;

    a = spring->a->position;
    b = spring->b->position;

    da.x = 0.5f * (b.x - a.x - spring->offset.x);
    da.y = 0.5f * (b.y - a.y - spring->offset.y);

    db.x = 0.5f * (a.x - b.x + spring->offset.x);
    db.y = 0.5f * (a.y - b.y + spring->offset.y);

    objectApplyForce (spring->a, k * da.x, k * da.y);
    objectApplyForce (spring->b, k * db.x, k * db.y);
}

static float modelStepObject(Object *object, float friction, float *force)
{
    object->theta += 0.05f;

    if (object->immobile)
    {
        object->velocity.x = 0.0f;
        object->velocity.y = 0.0f;
        object->force.x = 0.0f;
        object->force.y = 0.0f;

        *force = 0.0f;
        return 0.0f;
    }
    else
    {
        object->force.x -= friction * object->velocity.x;
        object->force.y -= friction * object->velocity.y;

        object->velocity.x += object->force.x / WOBBLY_MASS;
        object->velocity.y += object->force.y / WOBBLY_MASS;

        object->position.x += object->velocity.x;
        object->position.y += object->velocity.y;

        *force = fabs(object->force.x) + fabs(object->force.y);

        object->force.x = 0.0f;
        object->force.y = 0.0f;

        return fabs(object->velocity.x) + fabs(object->velocity.y);
    }
}

static int modelStep(Model *model, float friction, float k, float time)
{
    int   i, j, steps, wobbly = 0;
    float velocitySum = 0.0f;
    float force, forceSum = 0.0f;

    model->steps += time / 15.0f;
    steps = floor (model->steps);
    model->steps -= steps;

    if (!steps)
        return 1;

    for (j = 0; j < steps; j++)
    {
        for (i = 0; i < model->numSprings; i++)
            springExertForces (&model->springs[i], k);

        for (i = 0; i < model->numObjects; i++)
        {
            velocitySum += modelStepObject(&model->objects[i], friction, &force);
            forceSum += force;
        }
    }

    modelCalcBounds (model);

    if (velocitySum > 0.5f)
        wobbly |= WobblyVelocity;
    if (forceSum > 20.0f)
        wobbly |= WobblyForce;

    return wobbly;
}

static void bezierPatchEvaluate (Model *model, float u, float v,
        float *patchX, float *patchY)
{
    float coeffsU[4], coeffsV[4];
    float x, y;
    int   i, j;

    coeffsU[0] = (1 - u) * (1 - u) * (1 - u);
    coeffsU[1] = 3 * u * (1 - u) * (1 - u);
    coeffsU[2] = 3 * u * u * (1 - u);
    coeffsU[3] = u * u * u;

    coeffsV[0] = (1 - v) * (1 - v) * (1 - v);
    coeffsV[1] = 3 * v * (1 - v) * (1 - v);
    coeffsV[2] = 3 * v * v * (1 - v);
    coeffsV[3] = v * v * v;

    x = y = 0.0f;

    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 4; j++)
        {
            x += coeffsU[i] * coeffsV[j] *
                model->objects[j * GRID_WIDTH + i].position.x;
            y += coeffsU[i] * coeffsV[j] *
                model->objects[j * GRID_HEIGHT + i].position.y;
        }
    }

    *patchX = x;
    *patchY = y;
}

static int wobblyEnsureModel(struct wobbly_surface *surface)
{
    WobblyWindow *ww = surface->ww;

    if (!ww->model)
    {
        ww->model = createModel(surface->x, surface->y,
                surface->width, surface->height);
        if (!ww->model)
            return 0;
    }

    return 1;
}

static float objectDistance(Object *object, float x, float y)
{
    float dx, dy;
    dx = object->position.x - x;
    dy = object->position.y - y;

    return sqrt(dx * dx + dy * dy);
}

static Object *modelFindNearestObject(Model *model, float x, float y)
{
    Object *object = &model->objects[0];
    float  distance, minDistance = 0.0;
    int    i;

    for (i = 0; i < model->numObjects; i++)
    {
        distance = objectDistance(&model->objects[i], x, y);
        if (i == 0 || distance < minDistance)
        {
            minDistance = distance;
            object = &model->objects[i];
        }
    }

    return object;
}

static void modelAdjustCorners(Model *model, int x, int y,
        int width, int height, int make_immobile)
{
    Object *o;
    o = &model->objects[0];
    o->position.x = x;
    o->position.y = y;
    o->immobile = make_immobile;

    o = &model->objects[GRID_WIDTH - 1];
    o->position.x = x + width;
    o->position.y = y;
    o->immobile = make_immobile;

    o = &model->objects[GRID_WIDTH * (GRID_HEIGHT - 1)];
    o->position.x = x;
    o->position.y = y + height;
    o->immobile = make_immobile;

    o = &model->objects[model->numObjects - 1];
    o->position.x = x + width;
    o->position.y = y + height;
    o->immobile = make_immobile;

    if (!model->anchorObject)
        model->anchorObject = &model->objects[0];
}

static int modelRemoveEdgeAnchors(Model *model)
{
    int result = 0;
    Object *o;

    o = &model->objects[0];
    if (o != model->anchorObject)
    {
        result |= o->immobile;
        o->immobile = 0;
    }

    o = &model->objects[GRID_WIDTH - 1];
    if (o != model->anchorObject)
    {
        result |= o->immobile;
        o->immobile = 0;
    }

    o = &model->objects[GRID_WIDTH * (GRID_HEIGHT - 1)];
    if (o != model->anchorObject)
    {
        result |= o->immobile;
        o->immobile = 0;
    }

    o = &model->objects[model->numObjects - 1];
    if (o != model->anchorObject)
    {
        result |= o->immobile;
        o->immobile = 0;
    }

    return result;
}

void wobbly_prepare_paint(struct wobbly_surface *surface, int msSinceLastPaint)
{
    WobblyWindow *ww = surface->ww;
    float  friction, springK;

    friction = wobbly_settings_get_friction();
    springK  = wobbly_settings_get_spring_k();

    if (ww->wobbly)
    {
        if (ww->wobbly & (WobblyInitial | WobblyVelocity | WobblyForce))
        {
            ww->wobbly = modelStep(ww->model, friction, springK,
                    (ww->wobbly & WobblyVelocity) ?
                    msSinceLastPaint : 16);

            if (ww->wobbly) {
                modelCalcBounds(ww->model);
            } else {
                surface->x = ww->model->topLeft.x;
                surface->y = ww->model->topLeft.y;
                surface->synced = 1;
            }
        }
    }
}

void wobbly_done_paint(struct wobbly_surface *surface)
{
    WobblyWindow *ww = (WobblyWindow*)surface->ww;
    if (ww->wobbly)
    {
        surface->x = ww->model->topLeft.x;
        surface->y = ww->model->topLeft.y;
    }
}

void wobbly_add_geometry(struct wobbly_surface *surface)
{
    WobblyWindow *ww = surface->ww;

    float    width, height;
    float    deformedX, deformedY;
    int      x, y, iw, ih;
    float    cell_w, cell_h;
    GLfloat  *v, *uv;

    if (ww->wobbly)
    {
        width  = surface->width;
        height = surface->height;

        cell_w = width / surface->x_cells;
        cell_h = height / surface->y_cells;

        iw = surface->x_cells + 1;
        ih = surface->y_cells + 1;

        v = realloc(surface->v, sizeof(GLfloat) * 2 * iw * ih);
        uv = realloc(surface->uv, sizeof(GLfloat) * 2 * iw * ih);

        surface->v = v;
        surface->uv = uv;

        for (y = 0; y < ih; y++)
        {
            for (x = 0; x < iw; x++)
            {
                bezierPatchEvaluate(ww->model,
                        (x * cell_w) / width, (y * cell_h) / height,
                        &deformedX, &deformedY);

                *v++ = deformedX;
                *v++ = deformedY;

                *uv++ = (x * cell_w) / width;
                *uv++ = 1.0 - ((y * cell_h) / height);
            }
        }
    }
}

void wobbly_resize(struct wobbly_surface *surface, int width, int height)
{
    WobblyWindow *ww = surface->ww;
    width = width > 1 ? width : 1;
    height = height > 1 ? height : 1;

    surface->synced = 0;
    ww->wobbly |= WobblyInitial;

    if (ww->model)
        modelInitSprings(ww->model, width, height);

    ww->grab_dx = (ww->grab_dx * width) / surface->width;
    ww->grab_dy = (ww->grab_dy * height) / surface->height;

    surface->width = width;
    surface->height = height;
}

void wobbly_move_notify(struct wobbly_surface *surface, int x, int y)
{
    WobblyWindow *ww = surface->ww;
    if (ww->grabbed)
    {
        ww->model->anchorObject->position.x = x + ww->grab_dx;
        ww->model->anchorObject->position.y = y + ww->grab_dy;

        ww->wobbly |= WobblyInitial;
        surface->synced = 0;
    }
}

void wobbly_slight_wobble(struct wobbly_surface *surface)
{
    WobblyWindow *ww = surface->ww;
    if (wobblyEnsureModel(surface))
    {
        Object *centerObj;
        Spring *s;
        int	   i;

        centerObj = modelFindNearestObject(ww->model,
            surface->x + surface->width / 2, surface->y + surface->height / 2);

        for (i = 0; i < ww->model->numSprings; i++)
        {
            s = &ww->model->springs[i];

            if (s->a == centerObj)
            {
                s->b->velocity.x -= s->offset.x * 0.05f;
                s->b->velocity.y -= s->offset.y * 0.05f;
            }
            else if (s->b == centerObj)
            {
                s->a->velocity.x += s->offset.x * 0.05f;
                s->a->velocity.y += s->offset.y * 0.05f;
            }
        }

        ww->wobbly |= WobblyInitial;
    }
}

void wobbly_set_top_anchor(struct wobbly_surface *surface, int x, int y, int w, int h)
{
    (void)h;
    WobblyWindow *ww = surface->ww;
    if (wobblyEnsureModel(surface))
    {
        modelSetTopAnchor(ww->model, x, y, w);
    }
}

void wobbly_grab_notify(struct wobbly_surface *surface, int x, int y)
{
    WobblyWindow *ww = surface->ww;

    if (wobblyEnsureModel(surface))
    {
        Spring *s;
        int	   i;

        if (ww->model->anchorObject)
            ww->model->anchorObject->immobile = 0;

        ww->model->anchorObject = modelFindNearestObject(ww->model, x, y);
        ww->model->anchorObject->immobile = 1;
        ww->grab_dx = ww->model->anchorObject->position.x - x;
        ww->grab_dy = ww->model->anchorObject->position.y - y;

        ww->grabbed = 1;
        for (i = 0; i < ww->model->numSprings; i++)
        {
            s = &ww->model->springs[i];

            if (s->a == ww->model->anchorObject)
            {
                s->b->velocity.x -= s->offset.x * 0.05f;
                s->b->velocity.y -= s->offset.y * 0.05f;
            }
            else if (s->b == ww->model->anchorObject)
            {
                s->a->velocity.x += s->offset.x * 0.05f;
                s->a->velocity.y += s->offset.y * 0.05f;
            }
        }

        ww->wobbly |= WobblyInitial;
    }
}

void wobbly_ungrab_notify(struct wobbly_surface *surface)
{
    WobblyWindow *ww = surface->ww;
    if (ww->grabbed)
    {
        if (ww->model)
        {
            if (ww->model->anchorObject)
                ww->model->anchorObject->immobile = 0;

            ww->model->anchorObject = NULL;

            ww->wobbly |= WobblyInitial;
        }

        surface->synced = 0;
        ww->grabbed = 0;
    }
}

int wobbly_init(struct wobbly_surface *surface)
{
    WobblyWindow *ww;
    ww = malloc(sizeof (WobblyWindow));
    if (!ww)
        return 0;

    ww->model   = 0;
    ww->wobbly  = 0;
    ww->grabbed = 0;
    ww->state   = 0;

    surface->ww = ww;
    if(!wobblyEnsureModel(surface))
    {
        free(ww);
        return 0;
    }

    return 1;
}

void wobbly_fini(struct wobbly_surface *surface)
{
    WobblyWindow *ww = surface->ww;

    if (ww->model)
    {
        free(ww->model->objects);
        free(ww->model);
        free(surface->v);
    }

    free (ww);
}

void wobbly_force_geometry(struct wobbly_surface *surface,
        int x, int y, int w, int h)
{
    WobblyWindow *ww = surface->ww;

    if (wobblyEnsureModel(surface))
    {
		if (!ww->grabbed && ww->model->anchorObject)
		{
		    ww->model->anchorObject->immobile = 0;
		    ww->model->anchorObject = NULL;
		}

        surface->x = x;
        surface->y = y;
        surface->width = w > 0 ? w : 1;
        surface->height = h > 0 ? h : 1;
        surface->synced = 0;

	    modelInitSprings(ww->model, w, h);
		modelAdjustCorners(ww->model, x, y, w, h, 1);

	    ww->wobbly |= WobblyInitial;
    }
}

void wobbly_unenforce_geometry(struct wobbly_surface *surface)
{
    WobblyWindow *ww = surface->ww;

    if (wobblyEnsureModel(surface))
    {
        if (modelRemoveEdgeAnchors(ww->model))
        {
            if (!ww->model->anchorObject || !ww->model->anchorObject->immobile)
            {
                modelSetMiddleAnchor(ww->model, surface->x, surface->y,
                    surface->width, surface->height);
            }
            modelInitSprings(ww->model, surface->width, surface->height);
        }

        ww->wobbly |= WobblyInitial;
    }
}

void wobbly_translate(struct wobbly_surface *surface, int dx, int dy)
{
    WobblyWindow *ww = surface->ww;
    if (wobblyEnsureModel(surface))
    {
        for (int i = 0; i < ww->model->numObjects; i++)
        {
            ww->model->objects[i].position.x += dx;
            ww->model->objects[i].position.y += dy;
        }

        ww->model->topLeft.x += dx;
        ww->model->topLeft.y += dy;
        ww->model->bottomRight.x += dx;
        ww->model->bottomRight.y += dy;
    }
}

static void scale(float origin, float *x, double scale)
{
    *x = (*x - origin) * scale + origin;
}

void wobbly_scale(struct wobbly_surface *surface, double dx, double dy)
{
    WobblyWindow *ww = surface->ww;
    if (wobblyEnsureModel(surface))
    {
        for (int i = 0; i < ww->model->numObjects; i++)
        {
            scale(surface->x, &ww->model->objects[i].position.x, dx);
            scale(surface->y, &ww->model->objects[i].position.y, dy);
        }

        scale(surface->x, &ww->model->topLeft.x, dx);
        scale(surface->y, &ww->model->topLeft.y, dy);
        scale(surface->x, &ww->model->bottomRight.x, dx);
        scale(surface->y, &ww->model->bottomRight.y, dy);
    }
}

struct wobbly_rect wobbly_boundingbox(struct wobbly_surface *surface)
{
    WobblyWindow *ww = surface->ww;
    struct wobbly_rect result;
    memset(&result, 0, sizeof(result));
    if (ww->model)
    {
        result.tlx = ww->model->topLeft.x;
        result.tly = ww->model->topLeft.y;
        result.brx = ww->model->bottomRight.x;
        result.bry = ww->model->bottomRight.y;
    }

    return result;
}
