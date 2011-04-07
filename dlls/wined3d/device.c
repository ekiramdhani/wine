/*
 * IWineD3DDevice implementation
 *
 * Copyright 2002 Lionel Ulmer
 * Copyright 2002-2005 Jason Edmeades
 * Copyright 2003-2004 Raphael Junqueira
 * Copyright 2004 Christian Costa
 * Copyright 2005 Oliver Stieber
 * Copyright 2006-2008 Stefan Dösinger for CodeWeavers
 * Copyright 2006-2008 Henri Verbeet
 * Copyright 2007 Andrew Riedi
 * Copyright 2009-2011 Henri Verbeet for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "config.h"
#include <stdio.h>
#ifdef HAVE_FLOAT_H
# include <float.h>
#endif
#include "wined3d_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d);

/* Define the default light parameters as specified by MSDN */
const WINED3DLIGHT WINED3D_default_light = {

    WINED3DLIGHT_DIRECTIONAL,   /* Type */
    { 1.0f, 1.0f, 1.0f, 0.0f }, /* Diffuse r,g,b,a */
    { 0.0f, 0.0f, 0.0f, 0.0f }, /* Specular r,g,b,a */
    { 0.0f, 0.0f, 0.0f, 0.0f }, /* Ambient r,g,b,a, */
    { 0.0f, 0.0f, 0.0f },       /* Position x,y,z */
    { 0.0f, 0.0f, 1.0f },       /* Direction x,y,z */
    0.0f,                       /* Range */
    0.0f,                       /* Falloff */
    0.0f, 0.0f, 0.0f,           /* Attenuation 0,1,2 */
    0.0f,                       /* Theta */
    0.0f                        /* Phi */
};

/**********************************************************
 * Global variable / Constants follow
 **********************************************************/
const float identity[] =
{
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
};  /* When needed for comparisons */

/* Note that except for WINED3DPT_POINTLIST and WINED3DPT_LINELIST these
 * actually have the same values in GL and D3D. */
static GLenum gl_primitive_type_from_d3d(WINED3DPRIMITIVETYPE primitive_type)
{
    switch(primitive_type)
    {
        case WINED3DPT_POINTLIST:
            return GL_POINTS;

        case WINED3DPT_LINELIST:
            return GL_LINES;

        case WINED3DPT_LINESTRIP:
            return GL_LINE_STRIP;

        case WINED3DPT_TRIANGLELIST:
            return GL_TRIANGLES;

        case WINED3DPT_TRIANGLESTRIP:
            return GL_TRIANGLE_STRIP;

        case WINED3DPT_TRIANGLEFAN:
            return GL_TRIANGLE_FAN;

        case WINED3DPT_LINELIST_ADJ:
            return GL_LINES_ADJACENCY_ARB;

        case WINED3DPT_LINESTRIP_ADJ:
            return GL_LINE_STRIP_ADJACENCY_ARB;

        case WINED3DPT_TRIANGLELIST_ADJ:
            return GL_TRIANGLES_ADJACENCY_ARB;

        case WINED3DPT_TRIANGLESTRIP_ADJ:
            return GL_TRIANGLE_STRIP_ADJACENCY_ARB;

        default:
            FIXME("Unhandled primitive type %s\n", debug_d3dprimitivetype(primitive_type));
            return GL_NONE;
    }
}

static WINED3DPRIMITIVETYPE d3d_primitive_type_from_gl(GLenum primitive_type)
{
    switch(primitive_type)
    {
        case GL_POINTS:
            return WINED3DPT_POINTLIST;

        case GL_LINES:
            return WINED3DPT_LINELIST;

        case GL_LINE_STRIP:
            return WINED3DPT_LINESTRIP;

        case GL_TRIANGLES:
            return WINED3DPT_TRIANGLELIST;

        case GL_TRIANGLE_STRIP:
            return WINED3DPT_TRIANGLESTRIP;

        case GL_TRIANGLE_FAN:
            return WINED3DPT_TRIANGLEFAN;

        case GL_LINES_ADJACENCY_ARB:
            return WINED3DPT_LINELIST_ADJ;

        case GL_LINE_STRIP_ADJACENCY_ARB:
            return WINED3DPT_LINESTRIP_ADJ;

        case GL_TRIANGLES_ADJACENCY_ARB:
            return WINED3DPT_TRIANGLELIST_ADJ;

        case GL_TRIANGLE_STRIP_ADJACENCY_ARB:
            return WINED3DPT_TRIANGLESTRIP_ADJ;

        default:
            FIXME("Unhandled primitive type %s\n", debug_d3dprimitivetype(primitive_type));
            return WINED3DPT_UNDEFINED;
    }
}

static BOOL fixed_get_input(BYTE usage, BYTE usage_idx, unsigned int *regnum)
{
    if ((usage == WINED3DDECLUSAGE_POSITION || usage == WINED3DDECLUSAGE_POSITIONT) && !usage_idx)
        *regnum = WINED3D_FFP_POSITION;
    else if (usage == WINED3DDECLUSAGE_BLENDWEIGHT && !usage_idx)
        *regnum = WINED3D_FFP_BLENDWEIGHT;
    else if (usage == WINED3DDECLUSAGE_BLENDINDICES && !usage_idx)
        *regnum = WINED3D_FFP_BLENDINDICES;
    else if (usage == WINED3DDECLUSAGE_NORMAL && !usage_idx)
        *regnum = WINED3D_FFP_NORMAL;
    else if (usage == WINED3DDECLUSAGE_PSIZE && !usage_idx)
        *regnum = WINED3D_FFP_PSIZE;
    else if (usage == WINED3DDECLUSAGE_COLOR && !usage_idx)
        *regnum = WINED3D_FFP_DIFFUSE;
    else if (usage == WINED3DDECLUSAGE_COLOR && usage_idx == 1)
        *regnum = WINED3D_FFP_SPECULAR;
    else if (usage == WINED3DDECLUSAGE_TEXCOORD && usage_idx < WINED3DDP_MAXTEXCOORD)
        *regnum = WINED3D_FFP_TEXCOORD0 + usage_idx;
    else
    {
        FIXME("Unsupported input stream [usage=%s, usage_idx=%u]\n", debug_d3ddeclusage(usage), usage_idx);
        *regnum = ~0U;
        return FALSE;
    }

    return TRUE;
}

/* Context activation is done by the caller. */
void device_stream_info_from_declaration(IWineD3DDeviceImpl *This,
        BOOL use_vshader, struct wined3d_stream_info *stream_info, BOOL *fixup)
{
    /* We need to deal with frequency data! */
    struct wined3d_vertex_declaration *declaration = This->stateBlock->state.vertex_declaration;
    unsigned int i;

    stream_info->use_map = 0;
    stream_info->swizzle_map = 0;

    /* Check for transformed vertices, disable vertex shader if present. */
    stream_info->position_transformed = declaration->position_transformed;
    if (declaration->position_transformed) use_vshader = FALSE;

    /* Translate the declaration into strided data. */
    for (i = 0; i < declaration->element_count; ++i)
    {
        const struct wined3d_vertex_declaration_element *element = &declaration->elements[i];
        struct wined3d_buffer *buffer = This->stateBlock->state.streams[element->input_slot].buffer;
        GLuint buffer_object = 0;
        const BYTE *data = NULL;
        BOOL stride_used;
        unsigned int idx;
        DWORD stride;

        TRACE("%p Element %p (%u of %u)\n", declaration->elements,
                element, i + 1, declaration->element_count);

        if (!buffer) continue;

        stride = This->stateBlock->state.streams[element->input_slot].stride;
        if (This->stateBlock->state.user_stream)
        {
            TRACE("Stream %u is UP, %p\n", element->input_slot, buffer);
            buffer_object = 0;
            data = (BYTE *)buffer;
        }
        else
        {
            TRACE("Stream %u isn't UP, %p\n", element->input_slot, buffer);
            data = buffer_get_memory(buffer, &This->adapter->gl_info, &buffer_object);

            /* Can't use vbo's if the base vertex index is negative. OpenGL doesn't accept negative offsets
             * (or rather offsets bigger than the vbo, because the pointer is unsigned), so use system memory
             * sources. In most sane cases the pointer - offset will still be > 0, otherwise it will wrap
             * around to some big value. Hope that with the indices, the driver wraps it back internally. If
             * not, drawStridedSlow is needed, including a vertex buffer path. */
            if (This->stateBlock->state.load_base_vertex_index < 0)
            {
                WARN("load_base_vertex_index is < 0 (%d), not using VBOs.\n",
                        This->stateBlock->state.load_base_vertex_index);
                buffer_object = 0;
                data = buffer_get_sysmem(buffer, &This->adapter->gl_info);
                if ((UINT_PTR)data < -This->stateBlock->state.load_base_vertex_index * stride)
                {
                    FIXME("System memory vertex data load offset is negative!\n");
                }
            }

            if (fixup)
            {
                if (buffer_object) *fixup = TRUE;
                else if (*fixup && !use_vshader
                        && (element->usage == WINED3DDECLUSAGE_COLOR
                        || element->usage == WINED3DDECLUSAGE_POSITIONT))
                {
                    static BOOL warned = FALSE;
                    if (!warned)
                    {
                        /* This may be bad with the fixed function pipeline. */
                        FIXME("Missing vbo streams with unfixed colors or transformed position, expect problems\n");
                        warned = TRUE;
                    }
                }
            }
        }
        data += element->offset;

        TRACE("offset %u input_slot %u usage_idx %d\n", element->offset, element->input_slot, element->usage_idx);

        if (use_vshader)
        {
            if (element->output_slot == ~0U)
            {
                /* TODO: Assuming vertexdeclarations are usually used with the
                 * same or a similar shader, it might be worth it to store the
                 * last used output slot and try that one first. */
                stride_used = vshader_get_input(This->stateBlock->state.vertex_shader,
                        element->usage, element->usage_idx, &idx);
            }
            else
            {
                idx = element->output_slot;
                stride_used = TRUE;
            }
        }
        else
        {
            if (!element->ffp_valid)
            {
                WARN("Skipping unsupported fixed function element of format %s and usage %s\n",
                        debug_d3dformat(element->format->id), debug_d3ddeclusage(element->usage));
                stride_used = FALSE;
            }
            else
            {
                stride_used = fixed_get_input(element->usage, element->usage_idx, &idx);
            }
        }

        if (stride_used)
        {
            TRACE("Load %s array %u [usage %s, usage_idx %u, "
                    "input_slot %u, offset %u, stride %u, format %s, buffer_object %u]\n",
                    use_vshader ? "shader": "fixed function", idx,
                    debug_d3ddeclusage(element->usage), element->usage_idx, element->input_slot,
                    element->offset, stride, debug_d3dformat(element->format->id), buffer_object);

            stream_info->elements[idx].format = element->format;
            stream_info->elements[idx].stride = stride;
            stream_info->elements[idx].data = data;
            stream_info->elements[idx].stream_idx = element->input_slot;
            stream_info->elements[idx].buffer_object = buffer_object;

            if (!This->adapter->gl_info.supported[ARB_VERTEX_ARRAY_BGRA]
                    && element->format->id == WINED3DFMT_B8G8R8A8_UNORM)
            {
                stream_info->swizzle_map |= 1 << idx;
            }
            stream_info->use_map |= 1 << idx;
        }
    }

    This->num_buffer_queries = 0;
    if (!This->stateBlock->state.user_stream)
    {
        WORD map = stream_info->use_map;

        /* PreLoad all the vertex buffers. */
        for (i = 0; map; map >>= 1, ++i)
        {
            struct wined3d_stream_info_element *element;
            struct wined3d_buffer *buffer;

            if (!(map & 1)) continue;

            element = &stream_info->elements[i];
            buffer = This->stateBlock->state.streams[element->stream_idx].buffer;
            wined3d_buffer_preload(buffer);

            /* If PreLoad dropped the buffer object, update the stream info. */
            if (buffer->buffer_object != element->buffer_object)
            {
                element->buffer_object = 0;
                element->data = buffer_get_sysmem(buffer, &This->adapter->gl_info) + (ptrdiff_t)element->data;
            }

            if (buffer->query)
                This->buffer_queries[This->num_buffer_queries++] = buffer->query;
        }
    }
}

static void stream_info_element_from_strided(const struct wined3d_gl_info *gl_info,
        const struct WineDirect3DStridedData *strided, struct wined3d_stream_info_element *e)
{
    e->format = wined3d_get_format(gl_info, strided->format);
    e->stride = strided->dwStride;
    e->data = strided->lpData;
    e->stream_idx = 0;
    e->buffer_object = 0;
}

static void device_stream_info_from_strided(const struct wined3d_gl_info *gl_info,
        const struct WineDirect3DVertexStridedData *strided, struct wined3d_stream_info *stream_info)
{
    unsigned int i;

    memset(stream_info, 0, sizeof(*stream_info));

    if (strided->position.lpData)
        stream_info_element_from_strided(gl_info, &strided->position, &stream_info->elements[WINED3D_FFP_POSITION]);
    if (strided->normal.lpData)
        stream_info_element_from_strided(gl_info, &strided->normal, &stream_info->elements[WINED3D_FFP_NORMAL]);
    if (strided->diffuse.lpData)
        stream_info_element_from_strided(gl_info, &strided->diffuse, &stream_info->elements[WINED3D_FFP_DIFFUSE]);
    if (strided->specular.lpData)
        stream_info_element_from_strided(gl_info, &strided->specular, &stream_info->elements[WINED3D_FFP_SPECULAR]);

    for (i = 0; i < WINED3DDP_MAXTEXCOORD; ++i)
    {
        if (strided->texCoords[i].lpData)
            stream_info_element_from_strided(gl_info, &strided->texCoords[i],
                    &stream_info->elements[WINED3D_FFP_TEXCOORD0 + i]);
    }

    stream_info->position_transformed = strided->position_transformed;

    for (i = 0; i < sizeof(stream_info->elements) / sizeof(*stream_info->elements); ++i)
    {
        if (!stream_info->elements[i].format) continue;

        if (!gl_info->supported[ARB_VERTEX_ARRAY_BGRA]
                && stream_info->elements[i].format->id == WINED3DFMT_B8G8R8A8_UNORM)
        {
            stream_info->swizzle_map |= 1 << i;
        }
        stream_info->use_map |= 1 << i;
    }
}

static void device_trace_strided_stream_info(const struct wined3d_stream_info *stream_info)
{
    TRACE("Strided Data:\n");
    TRACE_STRIDED(stream_info, WINED3D_FFP_POSITION);
    TRACE_STRIDED(stream_info, WINED3D_FFP_BLENDWEIGHT);
    TRACE_STRIDED(stream_info, WINED3D_FFP_BLENDINDICES);
    TRACE_STRIDED(stream_info, WINED3D_FFP_NORMAL);
    TRACE_STRIDED(stream_info, WINED3D_FFP_PSIZE);
    TRACE_STRIDED(stream_info, WINED3D_FFP_DIFFUSE);
    TRACE_STRIDED(stream_info, WINED3D_FFP_SPECULAR);
    TRACE_STRIDED(stream_info, WINED3D_FFP_TEXCOORD0);
    TRACE_STRIDED(stream_info, WINED3D_FFP_TEXCOORD1);
    TRACE_STRIDED(stream_info, WINED3D_FFP_TEXCOORD2);
    TRACE_STRIDED(stream_info, WINED3D_FFP_TEXCOORD3);
    TRACE_STRIDED(stream_info, WINED3D_FFP_TEXCOORD4);
    TRACE_STRIDED(stream_info, WINED3D_FFP_TEXCOORD5);
    TRACE_STRIDED(stream_info, WINED3D_FFP_TEXCOORD6);
    TRACE_STRIDED(stream_info, WINED3D_FFP_TEXCOORD7);
}

/* Context activation is done by the caller. */
void device_update_stream_info(IWineD3DDeviceImpl *device, const struct wined3d_gl_info *gl_info)
{
    struct wined3d_stream_info *stream_info = &device->strided_streams;
    const struct wined3d_state *state = &device->stateBlock->state;
    BOOL fixup = FALSE;

    if (device->up_strided)
    {
        /* Note: this is a ddraw fixed-function code path. */
        TRACE("=============================== Strided Input ================================\n");
        device_stream_info_from_strided(gl_info, device->up_strided, stream_info);
        if (TRACE_ON(d3d)) device_trace_strided_stream_info(stream_info);
    }
    else
    {
        TRACE("============================= Vertex Declaration =============================\n");
        device_stream_info_from_declaration(device, !!state->vertex_shader, stream_info, &fixup);
    }

    if (state->vertex_shader && !stream_info->position_transformed)
    {
        if (state->vertex_declaration->half_float_conv_needed && !fixup)
        {
            TRACE("Using drawStridedSlow with vertex shaders for FLOAT16 conversion.\n");
            device->useDrawStridedSlow = TRUE;
        }
        else
        {
            device->useDrawStridedSlow = FALSE;
        }
    }
    else
    {
        WORD slow_mask = (1 << WINED3D_FFP_PSIZE);
        slow_mask |= -!gl_info->supported[ARB_VERTEX_ARRAY_BGRA]
                & ((1 << WINED3D_FFP_DIFFUSE) | (1 << WINED3D_FFP_SPECULAR));

        if ((stream_info->position_transformed || (stream_info->use_map & slow_mask)) && !fixup)
        {
            device->useDrawStridedSlow = TRUE;
        }
        else
        {
            device->useDrawStridedSlow = FALSE;
        }
    }
}

static void device_preload_texture(const struct wined3d_state *state, unsigned int idx)
{
    struct wined3d_texture *texture;
    enum WINED3DSRGB srgb;

    if (!(texture = state->textures[idx])) return;
    srgb = state->sampler_states[idx][WINED3DSAMP_SRGBTEXTURE] ? SRGB_SRGB : SRGB_RGB;
    texture->texture_ops->texture_preload(texture, srgb);
}

void device_preload_textures(IWineD3DDeviceImpl *device)
{
    const struct wined3d_state *state = &device->stateBlock->state;
    unsigned int i;

    if (use_vs(state))
    {
        for (i = 0; i < MAX_VERTEX_SAMPLERS; ++i)
        {
            if (state->vertex_shader->reg_maps.sampler_type[i])
                device_preload_texture(state, MAX_FRAGMENT_SAMPLERS + i);
        }
    }

    if (use_ps(state))
    {
        for (i = 0; i < MAX_FRAGMENT_SAMPLERS; ++i)
        {
            if (state->pixel_shader->reg_maps.sampler_type[i])
                device_preload_texture(state, i);
        }
    }
    else
    {
        WORD ffu_map = device->fixed_function_usage_map;

        for (i = 0; ffu_map; ffu_map >>= 1, ++i)
        {
            if (ffu_map & 1)
                device_preload_texture(state, i);
        }
    }
}

BOOL device_context_add(IWineD3DDeviceImpl *device, struct wined3d_context *context)
{
    struct wined3d_context **new_array;

    TRACE("Adding context %p.\n", context);

    if (!device->contexts) new_array = HeapAlloc(GetProcessHeap(), 0, sizeof(*new_array));
    else new_array = HeapReAlloc(GetProcessHeap(), 0, device->contexts,
            sizeof(*new_array) * (device->context_count + 1));

    if (!new_array)
    {
        ERR("Failed to grow the context array.\n");
        return FALSE;
    }

    new_array[device->context_count++] = context;
    device->contexts = new_array;
    return TRUE;
}

void device_context_remove(IWineD3DDeviceImpl *device, struct wined3d_context *context)
{
    struct wined3d_context **new_array;
    BOOL found = FALSE;
    UINT i;

    TRACE("Removing context %p.\n", context);

    for (i = 0; i < device->context_count; ++i)
    {
        if (device->contexts[i] == context)
        {
            found = TRUE;
            break;
        }
    }

    if (!found)
    {
        ERR("Context %p doesn't exist in context array.\n", context);
        return;
    }

    if (!--device->context_count)
    {
        HeapFree(GetProcessHeap(), 0, device->contexts);
        device->contexts = NULL;
        return;
    }

    memmove(&device->contexts[i], &device->contexts[i + 1], (device->context_count - i) * sizeof(*device->contexts));
    new_array = HeapReAlloc(GetProcessHeap(), 0, device->contexts, device->context_count * sizeof(*device->contexts));
    if (!new_array)
    {
        ERR("Failed to shrink context array. Oh well.\n");
        return;
    }

    device->contexts = new_array;
}

void device_get_draw_rect(IWineD3DDeviceImpl *device, RECT *rect)
{
    struct wined3d_stateblock *stateblock = device->stateBlock;
    WINED3DVIEWPORT *vp = &stateblock->state.viewport;

    SetRect(rect, vp->X, vp->Y, vp->X + vp->Width, vp->Y + vp->Height);

    if (stateblock->state.render_states[WINED3DRS_SCISSORTESTENABLE])
    {
        IntersectRect(rect, rect, &stateblock->state.scissor_rect);
    }
}

/* Do not call while under the GL lock. */
void device_switch_onscreen_ds(IWineD3DDeviceImpl *device,
        struct wined3d_context *context, IWineD3DSurfaceImpl *depth_stencil)
{
    if (device->onscreen_depth_stencil)
    {
        surface_load_ds_location(device->onscreen_depth_stencil, context, SFLAG_DS_OFFSCREEN);
        surface_modify_ds_location(device->onscreen_depth_stencil, SFLAG_DS_OFFSCREEN,
                device->onscreen_depth_stencil->ds_current_size.cx,
                device->onscreen_depth_stencil->ds_current_size.cy);
        IWineD3DSurface_Release((IWineD3DSurface *)device->onscreen_depth_stencil);
    }
    device->onscreen_depth_stencil = depth_stencil;
    IWineD3DSurface_AddRef((IWineD3DSurface *)device->onscreen_depth_stencil);
}

static BOOL is_full_clear(IWineD3DSurfaceImpl *target, const RECT *draw_rect, const RECT *clear_rect)
{
    /* partial draw rect */
    if (draw_rect->left || draw_rect->top
            || draw_rect->right < target->resource.width
            || draw_rect->bottom < target->resource.height)
        return FALSE;

    /* partial clear rect */
    if (clear_rect && (clear_rect->left > 0 || clear_rect->top > 0
            || clear_rect->right < target->resource.width
            || clear_rect->bottom < target->resource.height))
        return FALSE;

    return TRUE;
}

static void prepare_ds_clear(IWineD3DSurfaceImpl *ds, struct wined3d_context *context,
        DWORD location, const RECT *draw_rect, UINT rect_count, const RECT *clear_rect)
{
    RECT current_rect, r;

    if (ds->flags & location)
        SetRect(&current_rect, 0, 0,
                ds->ds_current_size.cx,
                ds->ds_current_size.cy);
    else
        SetRectEmpty(&current_rect);

    IntersectRect(&r, draw_rect, &current_rect);
    if (EqualRect(&r, draw_rect))
    {
        /* current_rect ⊇ draw_rect, modify only. */
        surface_modify_ds_location(ds, location, ds->ds_current_size.cx, ds->ds_current_size.cy);
        return;
    }

    if (EqualRect(&r, &current_rect))
    {
        /* draw_rect ⊇ current_rect, test if we're doing a full clear. */

        if (!clear_rect)
        {
            /* Full clear, modify only. */
            surface_modify_ds_location(ds, location, draw_rect->right, draw_rect->bottom);
            return;
        }

        IntersectRect(&r, draw_rect, clear_rect);
        if (EqualRect(&r, draw_rect))
        {
            /* clear_rect ⊇ draw_rect, modify only. */
            surface_modify_ds_location(ds, location, draw_rect->right, draw_rect->bottom);
            return;
        }
    }

    /* Full load. */
    surface_load_ds_location(ds, context, location);
    surface_modify_ds_location(ds, location, ds->ds_current_size.cx, ds->ds_current_size.cy);
}

/* Do not call while under the GL lock. */
HRESULT device_clear_render_targets(IWineD3DDeviceImpl *device, UINT rt_count, IWineD3DSurfaceImpl **rts,
        IWineD3DSurfaceImpl *depth_stencil, UINT rect_count, const RECT *rects, const RECT *draw_rect,
        DWORD flags, const WINED3DCOLORVALUE *color, float depth, DWORD stencil)
{
    const RECT *clear_rect = (rect_count > 0 && rects) ? (const RECT *)rects : NULL;
    IWineD3DSurfaceImpl *target = rt_count ? rts[0] : NULL;
    UINT drawable_width, drawable_height;
    struct wined3d_context *context;
    GLbitfield clear_mask = 0;
    BOOL render_offscreen;
    unsigned int i;

    /* When we're clearing parts of the drawable, make sure that the target surface is well up to date in the
     * drawable. After the clear we'll mark the drawable up to date, so we have to make sure that this is true
     * for the cleared parts, and the untouched parts.
     *
     * If we're clearing the whole target there is no need to copy it into the drawable, it will be overwritten
     * anyway. If we're not clearing the color buffer we don't have to copy either since we're not going to set
     * the drawable up to date. We have to check all settings that limit the clear area though. Do not bother
     * checking all this if the dest surface is in the drawable anyway. */
    if (flags & WINED3DCLEAR_TARGET && !is_full_clear(target, draw_rect, clear_rect))
    {
        for (i = 0; i < rt_count; ++i)
        {
            if (rts[i]) surface_load_location(rts[i], SFLAG_INDRAWABLE, NULL);
        }
    }

    context = context_acquire(device, target);
    if (!context->valid)
    {
        context_release(context);
        WARN("Invalid context, skipping clear.\n");
        return WINED3D_OK;
    }

    if (!context_apply_clear_state(context, device, rt_count, rts, depth_stencil))
    {
        context_release(context);
        WARN("Failed to apply clear state, skipping clear.\n");
        return WINED3D_OK;
    }

    if (target)
    {
        render_offscreen = context->render_offscreen;
        target->get_drawable_size(context, &drawable_width, &drawable_height);
    }
    else
    {
        render_offscreen = FALSE;
        drawable_width = depth_stencil->pow2Width;
        drawable_height = depth_stencil->pow2Height;
    }

    ENTER_GL();

    /* Only set the values up once, as they are not changing. */
    if (flags & WINED3DCLEAR_STENCIL)
    {
        if (context->gl_info->supported[EXT_STENCIL_TWO_SIDE])
        {
            glDisable(GL_STENCIL_TEST_TWO_SIDE_EXT);
            IWineD3DDeviceImpl_MarkStateDirty(device, STATE_RENDER(WINED3DRS_TWOSIDEDSTENCILMODE));
        }
        glStencilMask(~0U);
        IWineD3DDeviceImpl_MarkStateDirty(device, STATE_RENDER(WINED3DRS_STENCILWRITEMASK));
        glClearStencil(stencil);
        checkGLcall("glClearStencil");
        clear_mask = clear_mask | GL_STENCIL_BUFFER_BIT;
    }

    if (flags & WINED3DCLEAR_ZBUFFER)
    {
        DWORD location = render_offscreen ? SFLAG_DS_OFFSCREEN : SFLAG_DS_ONSCREEN;

        if (location == SFLAG_DS_ONSCREEN && depth_stencil != device->onscreen_depth_stencil)
        {
            LEAVE_GL();
            device_switch_onscreen_ds(device, context, depth_stencil);
            ENTER_GL();
        }
        prepare_ds_clear(depth_stencil, context, location, draw_rect, rect_count, clear_rect);
        surface_modify_location(depth_stencil, SFLAG_INDRAWABLE, TRUE);

        glDepthMask(GL_TRUE);
        IWineD3DDeviceImpl_MarkStateDirty(device, STATE_RENDER(WINED3DRS_ZWRITEENABLE));
        glClearDepth(depth);
        checkGLcall("glClearDepth");
        clear_mask = clear_mask | GL_DEPTH_BUFFER_BIT;
    }

    if (flags & WINED3DCLEAR_TARGET)
    {
        for (i = 0; i < rt_count; ++i)
        {
            if (rts[i]) surface_modify_location(rts[i], SFLAG_INDRAWABLE, TRUE);
        }

        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        IWineD3DDeviceImpl_MarkStateDirty(device, STATE_RENDER(WINED3DRS_COLORWRITEENABLE));
        IWineD3DDeviceImpl_MarkStateDirty(device, STATE_RENDER(WINED3DRS_COLORWRITEENABLE1));
        IWineD3DDeviceImpl_MarkStateDirty(device, STATE_RENDER(WINED3DRS_COLORWRITEENABLE2));
        IWineD3DDeviceImpl_MarkStateDirty(device, STATE_RENDER(WINED3DRS_COLORWRITEENABLE3));
        glClearColor(color->r, color->g, color->b, color->a);
        checkGLcall("glClearColor");
        clear_mask = clear_mask | GL_COLOR_BUFFER_BIT;
    }

    if (!clear_rect)
    {
        if (render_offscreen)
        {
            glScissor(draw_rect->left, draw_rect->top,
                    draw_rect->right - draw_rect->left, draw_rect->bottom - draw_rect->top);
        }
        else
        {
            glScissor(draw_rect->left, drawable_height - draw_rect->bottom,
                        draw_rect->right - draw_rect->left, draw_rect->bottom - draw_rect->top);
        }
        checkGLcall("glScissor");
        glClear(clear_mask);
        checkGLcall("glClear");
    }
    else
    {
        RECT current_rect;

        /* Now process each rect in turn. */
        for (i = 0; i < rect_count; ++i)
        {
            /* Note that GL uses lower left, width/height. */
            IntersectRect(&current_rect, draw_rect, &clear_rect[i]);

            TRACE("clear_rect[%u] %s, current_rect %s.\n", i,
                    wine_dbgstr_rect(&clear_rect[i]),
                    wine_dbgstr_rect(&current_rect));

            /* Tests show that rectangles where x1 > x2 or y1 > y2 are ignored silently.
             * The rectangle is not cleared, no error is returned, but further rectanlges are
             * still cleared if they are valid. */
            if (current_rect.left > current_rect.right || current_rect.top > current_rect.bottom)
            {
                TRACE("Rectangle with negative dimensions, ignoring.\n");
                continue;
            }

            if (render_offscreen)
            {
                glScissor(current_rect.left, current_rect.top,
                        current_rect.right - current_rect.left, current_rect.bottom - current_rect.top);
            }
            else
            {
                glScissor(current_rect.left, drawable_height - current_rect.bottom,
                          current_rect.right - current_rect.left, current_rect.bottom - current_rect.top);
            }
            checkGLcall("glScissor");

            glClear(clear_mask);
            checkGLcall("glClear");
        }
    }

    LEAVE_GL();

    if (wined3d_settings.strict_draw_ordering || (flags & WINED3DCLEAR_TARGET
            && target->container.type == WINED3D_CONTAINER_SWAPCHAIN
            && target->container.u.swapchain->front_buffer == target))
        wglFlush(); /* Flush to ensure ordering across contexts. */

    context_release(context);

    return WINED3D_OK;
}


/**********************************************************
 * IUnknown parts follows
 **********************************************************/

static HRESULT WINAPI IWineD3DDeviceImpl_QueryInterface(IWineD3DDevice *iface, REFIID riid, void **object)
{
    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_IWineD3DDevice)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        IUnknown_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI IWineD3DDeviceImpl_AddRef(IWineD3DDevice *iface) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    ULONG refCount = InterlockedIncrement(&This->ref);

    TRACE("(%p) : AddRef increasing from %d\n", This, refCount - 1);
    return refCount;
}

static ULONG WINAPI IWineD3DDeviceImpl_Release(IWineD3DDevice *iface) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    ULONG refCount = InterlockedDecrement(&This->ref);

    TRACE("(%p) : Releasing from %d\n", This, refCount + 1);

    if (!refCount) {
        UINT i;

        for (i = 0; i < sizeof(This->multistate_funcs)/sizeof(This->multistate_funcs[0]); ++i) {
            HeapFree(GetProcessHeap(), 0, This->multistate_funcs[i]);
            This->multistate_funcs[i] = NULL;
        }

        /* TODO: Clean up all the surfaces and textures! */
        /* NOTE: You must release the parent if the object was created via a callback
        ** ***************************/

        if (!list_empty(&This->resources))
        {
            struct wined3d_resource *resource;
            FIXME("(%p) Device released with resources still bound, acceptable but unexpected\n", This);

            LIST_FOR_EACH_ENTRY(resource, &This->resources, struct wined3d_resource, resource_list_entry)
            {
                FIXME("Leftover resource %p with type %s (%#x).\n",
                        resource, debug_d3dresourcetype(resource->resourceType), resource->resourceType);
            }
        }

        if(This->contexts) ERR("Context array not freed!\n");
        if (This->hardwareCursor) DestroyCursor(This->hardwareCursor);
        This->haveHardwareCursor = FALSE;

        wined3d_decref(This->wined3d);
        This->wined3d = NULL;
        HeapFree(GetProcessHeap(), 0, This);
        TRACE("Freed device  %p\n", This);
        This = NULL;
    }
    return refCount;
}

static HRESULT WINAPI IWineD3DDeviceImpl_CreateBuffer(IWineD3DDevice *iface, struct wined3d_buffer_desc *desc,
        const void *data, void *parent, const struct wined3d_parent_ops *parent_ops, struct wined3d_buffer **buffer)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    struct wined3d_buffer *object;
    HRESULT hr;

    TRACE("iface %p, desc %p, data %p, parent %p, buffer %p\n", iface, desc, data, parent, buffer);

    object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object));
    if (!object)
    {
        ERR("Failed to allocate memory\n");
        return E_OUTOFMEMORY;
    }

    FIXME("Ignoring access flags (pool)\n");

    hr = buffer_init(object, This, desc->byte_width, desc->usage, WINED3DFMT_UNKNOWN,
            WINED3DPOOL_MANAGED, GL_ARRAY_BUFFER_ARB, data, parent, parent_ops);
    if (FAILED(hr))
    {
        WARN("Failed to initialize buffer, hr %#x.\n", hr);
        HeapFree(GetProcessHeap(), 0, object);
        return hr;
    }
    object->desc = *desc;

    TRACE("Created buffer %p.\n", object);

    *buffer = object;

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_CreateVertexBuffer(IWineD3DDevice *iface, UINT Size, DWORD Usage,
        WINED3DPOOL Pool, void *parent, const struct wined3d_parent_ops *parent_ops, struct wined3d_buffer **buffer)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    struct wined3d_buffer *object;
    HRESULT hr;

    TRACE("iface %p, size %u, usage %#x, pool %#x, parent %p, parent_ops %p, buffer %p.\n",
            iface, Size, Usage, Pool, parent, parent_ops, buffer);

    if (Pool == WINED3DPOOL_SCRATCH)
    {
        /* The d3d9 testsuit shows that this is not allowed. It doesn't make much sense
         * anyway, SCRATCH vertex buffers aren't usable anywhere
         */
        WARN("Vertex buffer in D3DPOOL_SCRATCH requested, returning WINED3DERR_INVALIDCALL\n");
        *buffer = NULL;
        return WINED3DERR_INVALIDCALL;
    }

    object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object));
    if (!object)
    {
        ERR("Out of memory\n");
        *buffer = NULL;
        return WINED3DERR_OUTOFVIDEOMEMORY;
    }

    hr = buffer_init(object, This, Size, Usage, WINED3DFMT_VERTEXDATA,
            Pool, GL_ARRAY_BUFFER_ARB, NULL, parent, parent_ops);
    if (FAILED(hr))
    {
        WARN("Failed to initialize buffer, hr %#x.\n", hr);
        HeapFree(GetProcessHeap(), 0, object);
        return hr;
    }

    TRACE("Created buffer %p.\n", object);
    *buffer = object;

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_CreateIndexBuffer(IWineD3DDevice *iface, UINT Length, DWORD Usage,
        WINED3DPOOL Pool, void *parent, const struct wined3d_parent_ops *parent_ops, struct wined3d_buffer **buffer)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    struct wined3d_buffer *object;
    HRESULT hr;

    TRACE("iface %p, size %u, usage %#x, pool %#x, parent %p, parent_ops %p, buffer %p.\n",
            iface, Length, Usage, Pool, parent, parent_ops, buffer);

    /* Allocate the storage for the device */
    object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object));
    if (!object)
    {
        ERR("Out of memory\n");
        *buffer = NULL;
        return WINED3DERR_OUTOFVIDEOMEMORY;
    }

    hr = buffer_init(object, This, Length, Usage | WINED3DUSAGE_STATICDECL,
            WINED3DFMT_UNKNOWN, Pool, GL_ELEMENT_ARRAY_BUFFER_ARB, NULL,
            parent, parent_ops);
    if (FAILED(hr))
    {
        WARN("Failed to initialize buffer, hr %#x\n", hr);
        HeapFree(GetProcessHeap(), 0, object);
        return hr;
    }

    TRACE("Created buffer %p.\n", object);

    *buffer = object;

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_CreateStateBlock(IWineD3DDevice *iface,
        WINED3DSTATEBLOCKTYPE type, struct wined3d_stateblock **stateblock)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    struct wined3d_stateblock *object;
    HRESULT hr;

    object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object));
    if(!object)
    {
        ERR("Failed to allocate stateblock memory.\n");
        return E_OUTOFMEMORY;
    }

    hr = stateblock_init(object, This, type);
    if (FAILED(hr))
    {
        WARN("Failed to initialize stateblock, hr %#x.\n", hr);
        HeapFree(GetProcessHeap(), 0, object);
        return hr;
    }

    TRACE("Created stateblock %p.\n", object);
    *stateblock = object;

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_CreateSurface(IWineD3DDevice *iface, UINT Width, UINT Height,
        enum wined3d_format_id Format, BOOL Lockable, BOOL Discard, UINT Level, DWORD Usage, WINED3DPOOL Pool,
        WINED3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, WINED3DSURFTYPE Impl,
        void *parent, const struct wined3d_parent_ops *parent_ops, IWineD3DSurface **surface)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DSurfaceImpl *object;
    HRESULT hr;

    TRACE("iface %p, width %u, height %u, format %s (%#x), lockable %#x, discard %#x, level %u\n",
            iface, Width, Height, debug_d3dformat(Format), Format, Lockable, Discard, Level);
    TRACE("surface %p, usage %s (%#x), pool %s (%#x), multisample_type %#x, multisample_quality %u\n",
            surface, debug_d3dusage(Usage), Usage, debug_d3dpool(Pool), Pool, MultiSample, MultisampleQuality);
    TRACE("surface_type %#x, parent %p, parent_ops %p.\n", Impl, parent, parent_ops);

    if (Impl == SURFACE_OPENGL && !This->adapter)
    {
        ERR("OpenGL surfaces are not available without OpenGL.\n");
        return WINED3DERR_NOTAVAILABLE;
    }

    object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object));
    if (!object)
    {
        ERR("Failed to allocate surface memory.\n");
        return WINED3DERR_OUTOFVIDEOMEMORY;
    }

    hr = surface_init(object, Impl, This->surface_alignment, Width, Height, Level, Lockable,
            Discard, MultiSample, MultisampleQuality, This, Usage, Format, Pool, parent, parent_ops);
    if (FAILED(hr))
    {
        WARN("Failed to initialize surface, returning %#x.\n", hr);
        HeapFree(GetProcessHeap(), 0, object);
        return hr;
    }

    TRACE("(%p) : Created surface %p\n", This, object);

    *surface = (IWineD3DSurface *)object;

    return hr;
}

static HRESULT WINAPI IWineD3DDeviceImpl_CreateRendertargetView(IWineD3DDevice *iface,
        struct wined3d_resource *resource, void *parent, struct wined3d_rendertarget_view **rendertarget_view)
{
    struct wined3d_rendertarget_view *object;

    TRACE("iface %p, resource %p, parent %p, rendertarget_view %p.\n",
            iface, resource, parent, rendertarget_view);

    object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object));
    if (!object)
    {
        ERR("Failed to allocate memory\n");
        return E_OUTOFMEMORY;
    }

    wined3d_rendertarget_view_init(object, resource, parent);

    TRACE("Created render target view %p.\n", object);
    *rendertarget_view = object;

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_CreateTexture(IWineD3DDevice *iface,
        UINT Width, UINT Height, UINT Levels, DWORD Usage, enum wined3d_format_id Format, WINED3DPOOL Pool,
        void *parent, const struct wined3d_parent_ops *parent_ops, struct wined3d_texture **texture)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    struct wined3d_texture *object;
    HRESULT hr;

    TRACE("(%p) : Width %d, Height %d, Levels %d, Usage %#x\n", This, Width, Height, Levels, Usage);
    TRACE("Format %#x (%s), Pool %#x, texture %p, parent %p\n",
            Format, debug_d3dformat(Format), Pool, texture, parent);

    object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object));
    if (!object)
    {
        ERR("Out of memory\n");
        *texture = NULL;
        return WINED3DERR_OUTOFVIDEOMEMORY;
    }

    hr = texture_init(object, Width, Height, Levels, This, Usage, Format, Pool, parent, parent_ops);
    if (FAILED(hr))
    {
        WARN("Failed to initialize texture, returning %#x\n", hr);
        HeapFree(GetProcessHeap(), 0, object);
        *texture = NULL;
        return hr;
    }

    *texture = object;

    TRACE("(%p) : Created texture %p\n", This, object);

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_CreateVolumeTexture(IWineD3DDevice *iface,
        UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage, enum wined3d_format_id Format, WINED3DPOOL Pool,
        void *parent, const struct wined3d_parent_ops *parent_ops, struct wined3d_texture **texture)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    struct wined3d_texture *object;
    HRESULT hr;

    TRACE("(%p) : W(%u) H(%u) D(%u), Lvl(%u) Usage(%#x), Fmt(%u,%s), Pool(%s)\n", This, Width, Height,
          Depth, Levels, Usage, Format, debug_d3dformat(Format), debug_d3dpool(Pool));

    object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object));
    if (!object)
    {
        ERR("Out of memory\n");
        *texture = NULL;
        return WINED3DERR_OUTOFVIDEOMEMORY;
    }

    hr = volumetexture_init(object, Width, Height, Depth, Levels, This, Usage, Format, Pool, parent, parent_ops);
    if (FAILED(hr))
    {
        WARN("Failed to initialize volumetexture, returning %#x\n", hr);
        HeapFree(GetProcessHeap(), 0, object);
        *texture = NULL;
        return hr;
    }

    TRACE("(%p) : Created volume texture %p.\n", This, object);
    *texture = object;

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_CreateVolume(IWineD3DDevice *iface, UINT Width, UINT Height,
        UINT Depth, DWORD Usage, enum wined3d_format_id Format, WINED3DPOOL Pool, void *parent,
        const struct wined3d_parent_ops *parent_ops, IWineD3DVolume **ppVolume)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DVolumeImpl *object;
    HRESULT hr;

    TRACE("(%p) : W(%d) H(%d) D(%d), Usage(%d), Fmt(%u,%s), Pool(%s)\n", This, Width, Height,
          Depth, Usage, Format, debug_d3dformat(Format), debug_d3dpool(Pool));

    object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object));
    if (!object)
    {
        ERR("Out of memory\n");
        *ppVolume = NULL;
        return WINED3DERR_OUTOFVIDEOMEMORY;
    }

    hr = volume_init(object, This, Width, Height, Depth, Usage, Format, Pool, parent, parent_ops);
    if (FAILED(hr))
    {
        WARN("Failed to initialize volume, returning %#x.\n", hr);
        HeapFree(GetProcessHeap(), 0, object);
        return hr;
    }

    TRACE("(%p) : Created volume %p.\n", This, object);
    *ppVolume = (IWineD3DVolume *)object;

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_CreateCubeTexture(IWineD3DDevice *iface, UINT EdgeLength, UINT Levels,
        DWORD Usage, enum wined3d_format_id Format, WINED3DPOOL Pool, void *parent,
        const struct wined3d_parent_ops *parent_ops, struct wined3d_texture **texture)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    struct wined3d_texture *object;
    HRESULT hr;

    object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object));
    if (!object)
    {
        ERR("Out of memory\n");
        *texture = NULL;
        return WINED3DERR_OUTOFVIDEOMEMORY;
    }

    hr = cubetexture_init(object, EdgeLength, Levels, This, Usage, Format, Pool, parent, parent_ops);
    if (FAILED(hr))
    {
        WARN("Failed to initialize cubetexture, returning %#x\n", hr);
        HeapFree(GetProcessHeap(), 0, object);
        *texture = NULL;
        return hr;
    }

    TRACE("(%p) : Created Cube Texture %p\n", This, object);
    *texture = object;

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_CreateQuery(IWineD3DDevice *iface,
        WINED3DQUERYTYPE type, struct wined3d_query **query)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    struct wined3d_query *object;
    HRESULT hr;

    TRACE("iface %p, type %#x, query %p.\n", iface, type, query);

    object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object));
    if (!object)
    {
        ERR("Failed to allocate query memory.\n");
        return E_OUTOFMEMORY;
    }

    hr = query_init(object, This, type);
    if (FAILED(hr))
    {
        WARN("Failed to initialize query, hr %#x.\n", hr);
        HeapFree(GetProcessHeap(), 0, object);
        return hr;
    }

    TRACE("Created query %p.\n", object);
    *query = object;

    return WINED3D_OK;
}

/* Do not call while under the GL lock. */
static HRESULT WINAPI IWineD3DDeviceImpl_CreateSwapChain(IWineD3DDevice *iface,
        WINED3DPRESENT_PARAMETERS *present_parameters, WINED3DSURFTYPE surface_type,
        void *parent, const struct wined3d_parent_ops *parent_ops, IWineD3DSwapChain **swapchain)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DSwapChainImpl *object;
    HRESULT hr;

    TRACE("iface %p, present_parameters %p, swapchain %p, parent %p, surface_type %#x.\n",
            iface, present_parameters, swapchain, parent, surface_type);

    object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object));
    if (!object)
    {
        ERR("Failed to allocate swapchain memory.\n");
        return E_OUTOFMEMORY;
    }

    hr = swapchain_init(object, surface_type, This, present_parameters, parent, parent_ops);
    if (FAILED(hr))
    {
        WARN("Failed to initialize swapchain, hr %#x.\n", hr);
        HeapFree(GetProcessHeap(), 0, object);
        return hr;
    }

    TRACE("Created swapchain %p.\n", object);
    *swapchain = (IWineD3DSwapChain *)object;

    return WINED3D_OK;
}

static UINT WINAPI IWineD3DDeviceImpl_GetNumberOfSwapChains(IWineD3DDevice *iface)
{
    IWineD3DDeviceImpl *device = (IWineD3DDeviceImpl *)iface;

    TRACE("iface %p.\n", iface);

    return device->swapchain_count;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetSwapChain(IWineD3DDevice *iface,
        UINT iSwapChain, IWineD3DSwapChain **pSwapChain)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) : swapchain %d\n", This, iSwapChain);

    if (iSwapChain < This->swapchain_count)
    {
        *pSwapChain = (IWineD3DSwapChain *)This->swapchains[iSwapChain];
        IWineD3DSwapChain_AddRef(*pSwapChain);
        TRACE("(%p) returning %p\n", This, *pSwapChain);
        return WINED3D_OK;
    } else {
        TRACE("Swapchain out of range\n");
        *pSwapChain = NULL;
        return WINED3DERR_INVALIDCALL;
    }
}

static HRESULT WINAPI IWineD3DDeviceImpl_CreateVertexDeclaration(IWineD3DDevice *iface,
        const WINED3DVERTEXELEMENT *elements, UINT element_count, void *parent,
        const struct wined3d_parent_ops *parent_ops, struct wined3d_vertex_declaration **declaration)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    struct wined3d_vertex_declaration *object;
    HRESULT hr;

    TRACE("iface %p, elements %p, element_count %u, parent %p, parent_ops %p, declaration %p.\n",
            iface, elements, element_count, parent, parent_ops, declaration);

    object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object));
    if(!object)
    {
        ERR("Failed to allocate vertex declaration memory.\n");
        return E_OUTOFMEMORY;
    }

    hr = vertexdeclaration_init(object, This, elements, element_count, parent, parent_ops);
    if (FAILED(hr))
    {
        WARN("Failed to initialize vertex declaration, hr %#x.\n", hr);
        HeapFree(GetProcessHeap(), 0, object);
        return hr;
    }

    TRACE("Created vertex declaration %p.\n", object);
    *declaration = object;

    return WINED3D_OK;
}

struct wined3d_fvf_convert_state
{
    const struct wined3d_gl_info *gl_info;
    WINED3DVERTEXELEMENT *elements;
    UINT offset;
    UINT idx;
};

static void append_decl_element(struct wined3d_fvf_convert_state *state,
        enum wined3d_format_id format_id, WINED3DDECLUSAGE usage, UINT usage_idx)
{
    WINED3DVERTEXELEMENT *elements = state->elements;
    const struct wined3d_format *format;
    UINT offset = state->offset;
    UINT idx = state->idx;

    elements[idx].format = format_id;
    elements[idx].input_slot = 0;
    elements[idx].offset = offset;
    elements[idx].output_slot = 0;
    elements[idx].method = WINED3DDECLMETHOD_DEFAULT;
    elements[idx].usage = usage;
    elements[idx].usage_idx = usage_idx;

    format = wined3d_get_format(state->gl_info, format_id);
    state->offset += format->component_count * format->component_size;
    ++state->idx;
}

static unsigned int ConvertFvfToDeclaration(IWineD3DDeviceImpl *This, /* For the GL info, which has the type table */
        DWORD fvf, WINED3DVERTEXELEMENT **ppVertexElements)
{
    const struct wined3d_gl_info *gl_info = &This->adapter->gl_info;
    BOOL has_pos = !!(fvf & WINED3DFVF_POSITION_MASK);
    BOOL has_blend = (fvf & WINED3DFVF_XYZB5) > WINED3DFVF_XYZRHW;
    BOOL has_blend_idx = has_blend &&
       (((fvf & WINED3DFVF_XYZB5) == WINED3DFVF_XYZB5) ||
        (fvf & WINED3DFVF_LASTBETA_D3DCOLOR) ||
        (fvf & WINED3DFVF_LASTBETA_UBYTE4));
    BOOL has_normal = !!(fvf & WINED3DFVF_NORMAL);
    BOOL has_psize = !!(fvf & WINED3DFVF_PSIZE);
    BOOL has_diffuse = !!(fvf & WINED3DFVF_DIFFUSE);
    BOOL has_specular = !!(fvf & WINED3DFVF_SPECULAR);

    DWORD num_textures = (fvf & WINED3DFVF_TEXCOUNT_MASK) >> WINED3DFVF_TEXCOUNT_SHIFT;
    DWORD texcoords = (fvf & 0xFFFF0000) >> 16;
    struct wined3d_fvf_convert_state state;
    unsigned int size;
    unsigned int idx;
    DWORD num_blends = 1 + (((fvf & WINED3DFVF_XYZB5) - WINED3DFVF_XYZB1) >> 1);
    if (has_blend_idx) num_blends--;

    /* Compute declaration size */
    size = has_pos + (has_blend && num_blends > 0) + has_blend_idx + has_normal +
           has_psize + has_diffuse + has_specular + num_textures;

    state.gl_info = gl_info;
    state.elements = HeapAlloc(GetProcessHeap(), 0, size * sizeof(*state.elements));
    if (!state.elements) return ~0U;
    state.offset = 0;
    state.idx = 0;

    if (has_pos)
    {
        if (!has_blend && (fvf & WINED3DFVF_XYZRHW))
            append_decl_element(&state, WINED3DFMT_R32G32B32A32_FLOAT, WINED3DDECLUSAGE_POSITIONT, 0);
        else if ((fvf & WINED3DFVF_XYZW) == WINED3DFVF_XYZW)
            append_decl_element(&state, WINED3DFMT_R32G32B32A32_FLOAT, WINED3DDECLUSAGE_POSITION, 0);
        else
            append_decl_element(&state, WINED3DFMT_R32G32B32_FLOAT, WINED3DDECLUSAGE_POSITION, 0);
    }

    if (has_blend && (num_blends > 0))
    {
        if ((fvf & WINED3DFVF_XYZB5) == WINED3DFVF_XYZB2 && (fvf & WINED3DFVF_LASTBETA_D3DCOLOR))
            append_decl_element(&state, WINED3DFMT_B8G8R8A8_UNORM, WINED3DDECLUSAGE_BLENDWEIGHT, 0);
        else
        {
            switch (num_blends)
            {
                case 1:
                    append_decl_element(&state, WINED3DFMT_R32_FLOAT, WINED3DDECLUSAGE_BLENDWEIGHT, 0);
                    break;
                case 2:
                    append_decl_element(&state, WINED3DFMT_R32G32_FLOAT, WINED3DDECLUSAGE_BLENDWEIGHT, 0);
                    break;
                case 3:
                    append_decl_element(&state, WINED3DFMT_R32G32B32_FLOAT, WINED3DDECLUSAGE_BLENDWEIGHT, 0);
                    break;
                case 4:
                    append_decl_element(&state, WINED3DFMT_R32G32B32A32_FLOAT, WINED3DDECLUSAGE_BLENDWEIGHT, 0);
                    break;
                default:
                    ERR("Unexpected amount of blend values: %u\n", num_blends);
            }
        }
    }

    if (has_blend_idx)
    {
        if ((fvf & WINED3DFVF_LASTBETA_UBYTE4)
                || ((fvf & WINED3DFVF_XYZB5) == WINED3DFVF_XYZB2 && (fvf & WINED3DFVF_LASTBETA_D3DCOLOR)))
            append_decl_element(&state, WINED3DFMT_R8G8B8A8_UINT, WINED3DDECLUSAGE_BLENDINDICES, 0);
        else if (fvf & WINED3DFVF_LASTBETA_D3DCOLOR)
            append_decl_element(&state, WINED3DFMT_B8G8R8A8_UNORM, WINED3DDECLUSAGE_BLENDINDICES, 0);
        else
            append_decl_element(&state, WINED3DFMT_R32_FLOAT, WINED3DDECLUSAGE_BLENDINDICES, 0);
    }

    if (has_normal) append_decl_element(&state, WINED3DFMT_R32G32B32_FLOAT, WINED3DDECLUSAGE_NORMAL, 0);
    if (has_psize) append_decl_element(&state, WINED3DFMT_R32_FLOAT, WINED3DDECLUSAGE_PSIZE, 0);
    if (has_diffuse) append_decl_element(&state, WINED3DFMT_B8G8R8A8_UNORM, WINED3DDECLUSAGE_COLOR, 0);
    if (has_specular) append_decl_element(&state, WINED3DFMT_B8G8R8A8_UNORM, WINED3DDECLUSAGE_COLOR, 1);

    for (idx = 0; idx < num_textures; ++idx)
    {
        switch ((texcoords >> (idx * 2)) & 0x03)
        {
            case WINED3DFVF_TEXTUREFORMAT1:
                append_decl_element(&state, WINED3DFMT_R32_FLOAT, WINED3DDECLUSAGE_TEXCOORD, idx);
                break;
            case WINED3DFVF_TEXTUREFORMAT2:
                append_decl_element(&state, WINED3DFMT_R32G32_FLOAT, WINED3DDECLUSAGE_TEXCOORD, idx);
                break;
            case WINED3DFVF_TEXTUREFORMAT3:
                append_decl_element(&state, WINED3DFMT_R32G32B32_FLOAT, WINED3DDECLUSAGE_TEXCOORD, idx);
                break;
            case WINED3DFVF_TEXTUREFORMAT4:
                append_decl_element(&state, WINED3DFMT_R32G32B32A32_FLOAT, WINED3DDECLUSAGE_TEXCOORD, idx);
                break;
        }
    }

    *ppVertexElements = state.elements;
    return size;
}

static HRESULT WINAPI IWineD3DDeviceImpl_CreateVertexDeclarationFromFVF(IWineD3DDevice *iface,
        DWORD fvf, void *parent, const struct wined3d_parent_ops *parent_ops,
        struct wined3d_vertex_declaration **declaration)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *) iface;
    WINED3DVERTEXELEMENT *elements;
    unsigned int size;
    DWORD hr;

    TRACE("iface %p, fvf %#x, parent %p, parent_ops %p, declaration %p.\n",
            iface, fvf, parent, parent_ops, declaration);

    size = ConvertFvfToDeclaration(This, fvf, &elements);
    if (size == ~0U) return E_OUTOFMEMORY;

    hr = IWineD3DDeviceImpl_CreateVertexDeclaration(iface, elements, size, parent, parent_ops, declaration);
    HeapFree(GetProcessHeap(), 0, elements);
    return hr;
}

static HRESULT WINAPI IWineD3DDeviceImpl_CreateVertexShader(IWineD3DDevice *iface,
        const DWORD *pFunction, const struct wined3d_shader_signature *output_signature,
        void *parent, const struct wined3d_parent_ops *parent_ops,
        struct wined3d_shader **shader)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    struct wined3d_shader *object;
    HRESULT hr;

    if (This->vs_selected_mode == SHADER_NONE)
        return WINED3DERR_INVALIDCALL;

    object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object));
    if (!object)
    {
        ERR("Failed to allocate shader memory.\n");
        return E_OUTOFMEMORY;
    }

    hr = vertexshader_init(object, This, pFunction, output_signature, parent, parent_ops);
    if (FAILED(hr))
    {
        WARN("Failed to initialize vertex shader, hr %#x.\n", hr);
        HeapFree(GetProcessHeap(), 0, object);
        return hr;
    }

    TRACE("Created vertex shader %p.\n", object);
    *shader = object;

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_CreateGeometryShader(IWineD3DDevice *iface,
        const DWORD *byte_code, const struct wined3d_shader_signature *output_signature,
        void *parent, const struct wined3d_parent_ops *parent_ops,
        struct wined3d_shader **shader)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    struct wined3d_shader *object;
    HRESULT hr;

    object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object));
    if (!object)
    {
        ERR("Failed to allocate shader memory.\n");
        return E_OUTOFMEMORY;
    }

    hr = geometryshader_init(object, This, byte_code, output_signature, parent, parent_ops);
    if (FAILED(hr))
    {
        WARN("Failed to initialize geometry shader, hr %#x.\n", hr);
        HeapFree(GetProcessHeap(), 0, object);
        return hr;
    }

    TRACE("Created geometry shader %p.\n", object);
    *shader = object;

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_CreatePixelShader(IWineD3DDevice *iface,
        const DWORD *pFunction, const struct wined3d_shader_signature *output_signature,
        void *parent, const struct wined3d_parent_ops *parent_ops,
        struct wined3d_shader **shader)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    struct wined3d_shader *object;
    HRESULT hr;

    if (This->ps_selected_mode == SHADER_NONE)
        return WINED3DERR_INVALIDCALL;

    object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object));
    if (!object)
    {
        ERR("Failed to allocate shader memory.\n");
        return E_OUTOFMEMORY;
    }

    hr = pixelshader_init(object, This, pFunction, output_signature, parent, parent_ops);
    if (FAILED(hr))
    {
        WARN("Failed to initialize pixel shader, hr %#x.\n", hr);
        HeapFree(GetProcessHeap(), 0, object);
        return hr;
    }

    TRACE("Created pixel shader %p.\n", object);
    *shader = object;

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_CreatePalette(IWineD3DDevice *iface, DWORD flags,
        const PALETTEENTRY *entries, void *parent, struct wined3d_palette **palette)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *) iface;
    struct wined3d_palette *object;
    HRESULT hr;

    TRACE("iface %p, flags %#x, entries %p, palette %p, parent %p.\n",
            iface, flags, entries, palette, parent);

    object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object));
    if (!object)
    {
        ERR("Failed to allocate palette memory.\n");
        return E_OUTOFMEMORY;
    }

    hr = wined3d_palette_init(object, This, flags, entries, parent);
    if (FAILED(hr))
    {
        WARN("Failed to initialize palette, hr %#x.\n", hr);
        HeapFree(GetProcessHeap(), 0, object);
        return hr;
    }

    TRACE("Created palette %p.\n", object);
    *palette = object;

    return WINED3D_OK;
}

static void IWineD3DDeviceImpl_LoadLogo(IWineD3DDeviceImpl *This, const char *filename) {
    HBITMAP hbm;
    BITMAP bm;
    HRESULT hr;
    HDC dcb = NULL, dcs = NULL;
    WINEDDCOLORKEY colorkey;

    hbm = LoadImageA(NULL, filename, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
    if(hbm)
    {
        GetObjectA(hbm, sizeof(BITMAP), &bm);
        dcb = CreateCompatibleDC(NULL);
        if(!dcb) goto out;
        SelectObject(dcb, hbm);
    }
    else
    {
        /* Create a 32x32 white surface to indicate that wined3d is used, but the specified image
         * couldn't be loaded
         */
        memset(&bm, 0, sizeof(bm));
        bm.bmWidth = 32;
        bm.bmHeight = 32;
    }

    hr = IWineD3DDevice_CreateSurface((IWineD3DDevice *)This, bm.bmWidth, bm.bmHeight, WINED3DFMT_B5G6R5_UNORM, TRUE,
            FALSE, 0, 0, WINED3DPOOL_DEFAULT, WINED3DMULTISAMPLE_NONE, 0, SURFACE_OPENGL, NULL,
            &wined3d_null_parent_ops, &This->logo_surface);
    if (FAILED(hr))
    {
        ERR("Wine logo requested, but failed to create surface, hr %#x.\n", hr);
        goto out;
    }

    if(dcb) {
        hr = IWineD3DSurface_GetDC(This->logo_surface, &dcs);
        if(FAILED(hr)) goto out;
        BitBlt(dcs, 0, 0, bm.bmWidth, bm.bmHeight, dcb, 0, 0, SRCCOPY);
        IWineD3DSurface_ReleaseDC(This->logo_surface, dcs);

        colorkey.dwColorSpaceLowValue = 0;
        colorkey.dwColorSpaceHighValue = 0;
        IWineD3DSurface_SetColorKey(This->logo_surface, WINEDDCKEY_SRCBLT, &colorkey);
    }
    else
    {
        const WINED3DCOLORVALUE c = {1.0f, 1.0f, 1.0f, 1.0f};
        /* Fill the surface with a white color to show that wined3d is there */
        IWineD3DDevice_ColorFill((IWineD3DDevice *)This, This->logo_surface, NULL, &c);
    }

out:
    if (dcb) DeleteDC(dcb);
    if (hbm) DeleteObject(hbm);
}

/* Context activation is done by the caller. */
static void create_dummy_textures(IWineD3DDeviceImpl *This)
{
    const struct wined3d_gl_info *gl_info = &This->adapter->gl_info;
    unsigned int i;
    /* Under DirectX you can have texture stage operations even if no texture is
    bound, whereas opengl will only do texture operations when a valid texture is
    bound. We emulate this by creating dummy textures and binding them to each
    texture stage, but disable all stages by default. Hence if a stage is enabled
    then the default texture will kick in until replaced by a SetTexture call     */
    ENTER_GL();

    if (gl_info->supported[APPLE_CLIENT_STORAGE])
    {
        /* The dummy texture does not have client storage backing */
        glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_FALSE);
        checkGLcall("glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_FALSE)");
    }

    for (i = 0; i < gl_info->limits.textures; ++i)
    {
        GLubyte white = 255;

        /* Make appropriate texture active */
        GL_EXTCALL(glActiveTextureARB(GL_TEXTURE0_ARB + i));
        checkGLcall("glActiveTextureARB");

        /* Generate an opengl texture name */
        glGenTextures(1, &This->dummyTextureName[i]);
        checkGLcall("glGenTextures");
        TRACE("Dummy Texture %d given name %d\n", i, This->dummyTextureName[i]);

        /* Generate a dummy 2d texture (not using 1d because they cause many
        * DRI drivers fall back to sw) */
        glBindTexture(GL_TEXTURE_2D, This->dummyTextureName[i]);
        checkGLcall("glBindTexture");

        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 1, 1, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, &white);
        checkGLcall("glTexImage2D");
    }

    if (gl_info->supported[APPLE_CLIENT_STORAGE])
    {
        /* Reenable because if supported it is enabled by default */
        glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_TRUE);
        checkGLcall("glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_TRUE)");
    }

    LEAVE_GL();
}

/* Context activation is done by the caller. */
static void destroy_dummy_textures(IWineD3DDeviceImpl *device, const struct wined3d_gl_info *gl_info)
{
    ENTER_GL();
    glDeleteTextures(gl_info->limits.textures, device->dummyTextureName);
    checkGLcall("glDeleteTextures(gl_info->limits.textures, device->dummyTextureName)");
    LEAVE_GL();

    memset(device->dummyTextureName, 0, gl_info->limits.textures * sizeof(*device->dummyTextureName));
}

static LONG fullscreen_style(LONG style)
{
    /* Make sure the window is managed, otherwise we won't get keyboard input. */
    style |= WS_POPUP | WS_SYSMENU;
    style &= ~(WS_CAPTION | WS_THICKFRAME);

    return style;
}

static LONG fullscreen_exstyle(LONG exstyle)
{
    /* Filter out window decorations. */
    exstyle &= ~(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE);

    return exstyle;
}

static void WINAPI IWineD3DDeviceImpl_SetupFullscreenWindow(IWineD3DDevice *iface, HWND window, UINT w, UINT h)
{
    IWineD3DDeviceImpl *device = (IWineD3DDeviceImpl *)iface;
    BOOL filter_messages;
    LONG style, exstyle;

    TRACE("Setting up window %p for fullscreen mode.\n", window);

    if (device->style || device->exStyle)
    {
        ERR("Changing the window style for window %p, but another style (%08x, %08x) is already stored.\n",
                window, device->style, device->exStyle);
    }

    device->style = GetWindowLongW(window, GWL_STYLE);
    device->exStyle = GetWindowLongW(window, GWL_EXSTYLE);

    style = fullscreen_style(device->style);
    exstyle = fullscreen_exstyle(device->exStyle);

    TRACE("Old style was %08x, %08x, setting to %08x, %08x.\n",
            device->style, device->exStyle, style, exstyle);

    filter_messages = device->filter_messages;
    device->filter_messages = TRUE;

    SetWindowLongW(window, GWL_STYLE, style);
    SetWindowLongW(window, GWL_EXSTYLE, exstyle);
    SetWindowPos(window, HWND_TOP, 0, 0, w, h, SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE);

    device->filter_messages = filter_messages;
}

static void WINAPI IWineD3DDeviceImpl_RestoreFullscreenWindow(IWineD3DDevice *iface, HWND window)
{
    IWineD3DDeviceImpl *device = (IWineD3DDeviceImpl *)iface;
    BOOL filter_messages;
    LONG style, exstyle;

    if (!device->style && !device->exStyle) return;

    TRACE("Restoring window style of window %p to %08x, %08x.\n",
            window, device->style, device->exStyle);

    style = GetWindowLongW(window, GWL_STYLE);
    exstyle = GetWindowLongW(window, GWL_EXSTYLE);

    filter_messages = device->filter_messages;
    device->filter_messages = TRUE;

    /* Only restore the style if the application didn't modify it during the
     * fullscreen phase. Some applications change it before calling Reset()
     * when switching between windowed and fullscreen modes (HL2), some
     * depend on the original style (Eve Online). */
    if (style == fullscreen_style(device->style) && exstyle == fullscreen_exstyle(device->exStyle))
    {
        SetWindowLongW(window, GWL_STYLE, device->style);
        SetWindowLongW(window, GWL_EXSTYLE, device->exStyle);
    }
    SetWindowPos(window, 0, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    device->filter_messages = filter_messages;

    /* Delete the old values. */
    device->style = 0;
    device->exStyle = 0;
}

static HRESULT WINAPI IWineD3DDeviceImpl_AcquireFocusWindow(IWineD3DDevice *iface, HWND window)
{
    IWineD3DDeviceImpl *device = (IWineD3DDeviceImpl *)iface;

    TRACE("iface %p, window %p.\n", iface, window);

    if (!wined3d_register_window(window, device))
    {
        ERR("Failed to register window %p.\n", window);
        return E_FAIL;
    }

    device->focus_window = window;
    SetWindowPos(window, 0, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);

    return WINED3D_OK;
}

static void WINAPI IWineD3DDeviceImpl_ReleaseFocusWindow(IWineD3DDevice *iface)
{
    IWineD3DDeviceImpl *device = (IWineD3DDeviceImpl *)iface;

    TRACE("iface %p.\n", iface);

    if (device->focus_window) wined3d_unregister_window(device->focus_window);
    device->focus_window = NULL;
}

static HRESULT WINAPI IWineD3DDeviceImpl_Init3D(IWineD3DDevice *iface,
        WINED3DPRESENT_PARAMETERS *pPresentationParameters)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *) iface;
    const struct wined3d_gl_info *gl_info = &This->adapter->gl_info;
    IWineD3DSwapChainImpl *swapchain = NULL;
    struct wined3d_context *context;
    HRESULT hr;
    DWORD state;
    unsigned int i;

    TRACE("(%p)->(%p)\n", This, pPresentationParameters);

    if(This->d3d_initialized) return WINED3DERR_INVALIDCALL;
    if(!This->adapter->opengl) return WINED3DERR_INVALIDCALL;

    TRACE("(%p) : Creating stateblock\n", This);
    hr = IWineD3DDevice_CreateStateBlock(iface, WINED3DSBT_INIT, &This->stateBlock);
    if (FAILED(hr))
    {
        WARN("Failed to create stateblock\n");
        goto err_out;
    }
    TRACE("(%p) : Created stateblock (%p)\n", This, This->stateBlock);
    This->updateStateBlock = This->stateBlock;
    wined3d_stateblock_incref(This->updateStateBlock);

    This->render_targets = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*This->render_targets) * gl_info->limits.buffers);

    This->palette_count = 1;
    This->palettes = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(PALETTEENTRY*));
    if (!This->palettes || !This->render_targets)
    {
        ERR("Out of memory!\n");
        hr = E_OUTOFMEMORY;
        goto err_out;
    }
    This->palettes[0] = HeapAlloc(GetProcessHeap(), 0, sizeof(PALETTEENTRY) * 256);
    if(!This->palettes[0]) {
        ERR("Out of memory!\n");
        hr = E_OUTOFMEMORY;
        goto err_out;
    }
    for (i = 0; i < 256; ++i) {
        This->palettes[0][i].peRed   = 0xFF;
        This->palettes[0][i].peGreen = 0xFF;
        This->palettes[0][i].peBlue  = 0xFF;
        This->palettes[0][i].peFlags = 0xFF;
    }
    This->currentPalette = 0;

    /* Initialize the texture unit mapping to a 1:1 mapping */
    for (state = 0; state < MAX_COMBINED_SAMPLERS; ++state)
    {
        if (state < gl_info->limits.fragment_samplers)
        {
            This->texUnitMap[state] = state;
            This->rev_tex_unit_map[state] = state;
        } else {
            This->texUnitMap[state] = WINED3D_UNMAPPED_STAGE;
            This->rev_tex_unit_map[state] = WINED3D_UNMAPPED_STAGE;
        }
    }

    /* Setup the implicit swapchain. This also initializes a context. */
    TRACE("Creating implicit swapchain\n");
    hr = IWineD3DDeviceParent_CreateSwapChain(This->device_parent,
            pPresentationParameters, (IWineD3DSwapChain **)&swapchain);
    if (FAILED(hr))
    {
        WARN("Failed to create implicit swapchain\n");
        goto err_out;
    }

    This->swapchain_count = 1;
    This->swapchains = HeapAlloc(GetProcessHeap(), 0, This->swapchain_count * sizeof(*This->swapchains));
    if (!This->swapchains)
    {
        ERR("Out of memory!\n");
        goto err_out;
    }
    This->swapchains[0] = swapchain;

    if (swapchain->back_buffers && swapchain->back_buffers[0])
    {
        TRACE("Setting rendertarget to %p.\n", swapchain->back_buffers);
        This->render_targets[0] = swapchain->back_buffers[0];
    }
    else
    {
        TRACE("Setting rendertarget to %p.\n", swapchain->front_buffer);
        This->render_targets[0] = swapchain->front_buffer;
    }
    IWineD3DSurface_AddRef((IWineD3DSurface *)This->render_targets[0]);

    /* Depth Stencil support */
    This->depth_stencil = This->auto_depth_stencil;
    if (This->depth_stencil)
        IWineD3DSurface_AddRef((IWineD3DSurface *)This->depth_stencil);

    hr = This->shader_backend->shader_alloc_private(This);
    if(FAILED(hr)) {
        TRACE("Shader private data couldn't be allocated\n");
        goto err_out;
    }
    hr = This->frag_pipe->alloc_private(This);
    if(FAILED(hr)) {
        TRACE("Fragment pipeline private data couldn't be allocated\n");
        goto err_out;
    }
    hr = This->blitter->alloc_private(This);
    if(FAILED(hr)) {
        TRACE("Blitter private data couldn't be allocated\n");
        goto err_out;
    }

    /* Set up some starting GL setup */

    /* Setup all the devices defaults */
    stateblock_init_default_state(This->stateBlock);

    context = context_acquire(This, swapchain->front_buffer);

    create_dummy_textures(This);

    ENTER_GL();

    /* Initialize the current view state */
    This->view_ident = 1;
    This->contexts[0]->last_was_rhw = 0;
    glGetIntegerv(GL_MAX_LIGHTS, &This->maxConcurrentLights);
    checkGLcall("glGetIntegerv(GL_MAX_LIGHTS, &This->maxConcurrentLights)");

    switch(wined3d_settings.offscreen_rendering_mode) {
        case ORM_FBO:
            This->offscreenBuffer = GL_COLOR_ATTACHMENT0;
            break;

        case ORM_BACKBUFFER:
        {
            if (context_get_current()->aux_buffers > 0)
            {
                TRACE("Using auxilliary buffer for offscreen rendering\n");
                This->offscreenBuffer = GL_AUX0;
            } else {
                TRACE("Using back buffer for offscreen rendering\n");
                This->offscreenBuffer = GL_BACK;
            }
        }
    }

    TRACE("(%p) All defaults now set up, leaving Init3D with %p\n", This, This);
    LEAVE_GL();

    context_release(context);

    /* Clear the screen */
    IWineD3DDevice_Clear((IWineD3DDevice *) This, 0, NULL,
                          WINED3DCLEAR_TARGET | pPresentationParameters->EnableAutoDepthStencil ? WINED3DCLEAR_ZBUFFER | WINED3DCLEAR_STENCIL : 0,
                          0x00, 1.0f, 0);

    This->d3d_initialized = TRUE;

    if(wined3d_settings.logo) {
        IWineD3DDeviceImpl_LoadLogo(This, wined3d_settings.logo);
    }
    This->highest_dirty_ps_const = 0;
    This->highest_dirty_vs_const = 0;
    return WINED3D_OK;

err_out:
    HeapFree(GetProcessHeap(), 0, This->render_targets);
    HeapFree(GetProcessHeap(), 0, This->swapchains);
    This->swapchain_count = 0;
    if (This->palettes)
    {
        HeapFree(GetProcessHeap(), 0, This->palettes[0]);
        HeapFree(GetProcessHeap(), 0, This->palettes);
    }
    This->palette_count = 0;
    if (swapchain)
    {
        IWineD3DSwapChain_Release( (IWineD3DSwapChain *) swapchain);
    }
    if (This->stateBlock)
    {
        wined3d_stateblock_decref(This->stateBlock);
        This->stateBlock = NULL;
    }
    if (This->blit_priv) {
        This->blitter->free_private(This);
    }
    if (This->fragment_priv) {
        This->frag_pipe->free_private(This);
    }
    if (This->shader_priv) {
        This->shader_backend->shader_free_private(This);
    }
    return hr;
}

static HRESULT WINAPI IWineD3DDeviceImpl_InitGDI(IWineD3DDevice *iface,
        WINED3DPRESENT_PARAMETERS *pPresentationParameters)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *) iface;
    IWineD3DSwapChainImpl *swapchain = NULL;
    HRESULT hr;

    /* Setup the implicit swapchain */
    TRACE("Creating implicit swapchain\n");
    hr = IWineD3DDeviceParent_CreateSwapChain(This->device_parent,
            pPresentationParameters, (IWineD3DSwapChain **)&swapchain);
    if (FAILED(hr))
    {
        WARN("Failed to create implicit swapchain\n");
        goto err_out;
    }

    This->swapchain_count = 1;
    This->swapchains = HeapAlloc(GetProcessHeap(), 0, This->swapchain_count * sizeof(*This->swapchains));
    if (!This->swapchains)
    {
        ERR("Out of memory!\n");
        goto err_out;
    }
    This->swapchains[0] = swapchain;
    return WINED3D_OK;

err_out:
    IWineD3DSwapChain_Release((IWineD3DSwapChain *) swapchain);
    return hr;
}

static HRESULT WINAPI device_unload_resource(struct wined3d_resource *resource, void *data)
{
    TRACE("Unloading resource %p.\n", resource);

    resource->resource_ops->resource_unload(resource);

    return S_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_Uninit3D(IWineD3DDevice *iface,
        D3DCB_DESTROYSWAPCHAINFN D3DCB_DestroySwapChain)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *) iface;
    const struct wined3d_gl_info *gl_info;
    struct IWineD3DSurfaceImpl *surface;
    struct wined3d_context *context;
    int sampler;
    UINT i;
    TRACE("(%p)\n", This);

    if(!This->d3d_initialized) return WINED3DERR_INVALIDCALL;

    /* Force making the context current again, to verify it is still valid
     * (workaround for broken drivers) */
    context_set_current(NULL);
    /* I don't think that the interface guarantees that the device is destroyed from the same thread
     * it was created. Thus make sure a context is active for the glDelete* calls
     */
    context = context_acquire(This, NULL);
    gl_info = context->gl_info;

    if(This->logo_surface) IWineD3DSurface_Release(This->logo_surface);

    /* Unload resources */
    IWineD3DDevice_EnumResources(iface, device_unload_resource, NULL);

    TRACE("Deleting high order patches\n");
    for(i = 0; i < PATCHMAP_SIZE; i++) {
        struct list *e1, *e2;
        struct WineD3DRectPatch *patch;
        LIST_FOR_EACH_SAFE(e1, e2, &This->patches[i]) {
            patch = LIST_ENTRY(e1, struct WineD3DRectPatch, entry);
            IWineD3DDevice_DeletePatch(iface, patch->Handle);
        }
    }

    /* Delete the mouse cursor texture */
    if(This->cursorTexture) {
        ENTER_GL();
        glDeleteTextures(1, &This->cursorTexture);
        LEAVE_GL();
        This->cursorTexture = 0;
    }

    for (sampler = 0; sampler < MAX_FRAGMENT_SAMPLERS; ++sampler) {
        IWineD3DDevice_SetTexture(iface, sampler, NULL);
    }
    for (sampler = 0; sampler < MAX_VERTEX_SAMPLERS; ++sampler) {
        IWineD3DDevice_SetTexture(iface, WINED3DVERTEXTEXTURESAMPLER0 + sampler, NULL);
    }

    /* Destroy the depth blt resources, they will be invalid after the reset. Also free shader
     * private data, it might contain opengl pointers
     */
    if(This->depth_blt_texture) {
        ENTER_GL();
        glDeleteTextures(1, &This->depth_blt_texture);
        LEAVE_GL();
        This->depth_blt_texture = 0;
    }
    if (This->depth_blt_rb) {
        ENTER_GL();
        gl_info->fbo_ops.glDeleteRenderbuffers(1, &This->depth_blt_rb);
        LEAVE_GL();
        This->depth_blt_rb = 0;
        This->depth_blt_rb_w = 0;
        This->depth_blt_rb_h = 0;
    }

    /* Release the update stateblock */
    if (wined3d_stateblock_decref(This->updateStateBlock))
    {
        if (This->updateStateBlock != This->stateBlock)
            FIXME("Something's still holding the update stateblock.\n");
    }
    This->updateStateBlock = NULL;

    {
        struct wined3d_stateblock *stateblock = This->stateBlock;
        This->stateBlock = NULL;

        /* Release the stateblock */
        if (wined3d_stateblock_decref(stateblock))
            FIXME("Something's still holding the stateblock.\n");
    }

    /* Destroy the shader backend. Note that this has to happen after all shaders are destroyed. */
    This->blitter->free_private(This);
    This->frag_pipe->free_private(This);
    This->shader_backend->shader_free_private(This);

    /* Release the buffers (with sanity checks)*/
    if (This->onscreen_depth_stencil)
    {
        surface = This->onscreen_depth_stencil;
        This->onscreen_depth_stencil = NULL;
        IWineD3DSurface_Release((IWineD3DSurface *)surface);
    }

    if (This->depth_stencil)
    {
        surface = This->depth_stencil;

        TRACE("Releasing depth/stencil buffer %p.\n", surface);

        This->depth_stencil = NULL;
        if (IWineD3DSurface_Release((IWineD3DSurface *)surface)
                && surface != This->auto_depth_stencil)
        {
            ERR("Something is still holding a reference to depth/stencil buffer %p.\n", surface);
        }
    }

    if (This->auto_depth_stencil)
    {
        surface = This->auto_depth_stencil;
        This->auto_depth_stencil = NULL;
        if (IWineD3DSurface_Release((IWineD3DSurface *)surface))
        {
            FIXME("(%p) Something's still holding the auto depth stencil buffer\n", This);
        }
    }

    for (i = 1; i < gl_info->limits.buffers; ++i)
    {
        IWineD3DDevice_SetRenderTarget(iface, i, NULL, FALSE);
    }

    surface = This->render_targets[0];
    TRACE("Setting rendertarget 0 to NULL\n");
    This->render_targets[0] = NULL;
    TRACE("Releasing the render target at %p\n", surface);
    IWineD3DSurface_Release((IWineD3DSurface *)surface);

    context_release(context);

    for (i = 0; i < This->swapchain_count; ++i)
    {
        TRACE("Releasing the implicit swapchain %u.\n", i);
        if (D3DCB_DestroySwapChain((IWineD3DSwapChain *)This->swapchains[i]) > 0)
        {
            FIXME("(%p) Something's still holding the implicit swapchain\n", This);
        }
    }

    HeapFree(GetProcessHeap(), 0, This->swapchains);
    This->swapchains = NULL;
    This->swapchain_count = 0;

    for (i = 0; i < This->palette_count; ++i)
        HeapFree(GetProcessHeap(), 0, This->palettes[i]);
    HeapFree(GetProcessHeap(), 0, This->palettes);
    This->palettes = NULL;
    This->palette_count = 0;

    HeapFree(GetProcessHeap(), 0, This->render_targets);
    This->render_targets = NULL;

    This->d3d_initialized = FALSE;

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_UninitGDI(IWineD3DDevice *iface, D3DCB_DESTROYSWAPCHAINFN D3DCB_DestroySwapChain) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *) iface;
    unsigned int i;

    for (i = 0; i < This->swapchain_count; ++i)
    {
        TRACE("Releasing the implicit swapchain %u.\n", i);
        if (D3DCB_DestroySwapChain((IWineD3DSwapChain *)This->swapchains[i]) > 0)
        {
            FIXME("(%p) Something's still holding the implicit swapchain\n", This);
        }
    }

    HeapFree(GetProcessHeap(), 0, This->swapchains);
    This->swapchains = NULL;
    This->swapchain_count = 0;
    return WINED3D_OK;
}

/* Enables thread safety in the wined3d device and its resources. Called by DirectDraw
 * from SetCooperativeLevel if DDSCL_MULTITHREADED is specified, and by d3d8/9 from
 * CreateDevice if D3DCREATE_MULTITHREADED is passed.
 *
 * There is no way to deactivate thread safety once it is enabled.
 */
static void WINAPI IWineD3DDeviceImpl_SetMultithreaded(IWineD3DDevice *iface) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *) iface;

    /*For now just store the flag(needed in case of ddraw) */
    This->createParms.BehaviorFlags |= WINED3DCREATE_MULTITHREADED;
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetDisplayMode(IWineD3DDevice *iface, UINT iSwapChain,
        const WINED3DDISPLAYMODE* pMode) {
    DEVMODEW devmode;
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    const struct wined3d_format *format = wined3d_get_format(&This->adapter->gl_info, pMode->Format);
    LONG ret;
    RECT clip_rc;

    TRACE("(%p)->(%d,%p) Mode=%dx%dx@%d, %s\n", This, iSwapChain, pMode, pMode->Width, pMode->Height, pMode->RefreshRate, debug_d3dformat(pMode->Format));

    /* Resize the screen even without a window:
     * The app could have unset it with SetCooperativeLevel, but not called
     * RestoreDisplayMode first. Then the release will call RestoreDisplayMode,
     * but we don't have any hwnd
     */

    memset(&devmode, 0, sizeof(devmode));
    devmode.dmSize = sizeof(devmode);
    devmode.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
    devmode.dmBitsPerPel = format->byte_count * CHAR_BIT;
    devmode.dmPelsWidth  = pMode->Width;
    devmode.dmPelsHeight = pMode->Height;

    devmode.dmDisplayFrequency = pMode->RefreshRate;
    if (pMode->RefreshRate)
        devmode.dmFields |= DM_DISPLAYFREQUENCY;

    /* Only change the mode if necessary */
    if (This->ddraw_width == pMode->Width && This->ddraw_height == pMode->Height
            && This->ddraw_format == pMode->Format && !pMode->RefreshRate)
        return WINED3D_OK;

    ret = ChangeDisplaySettingsExW(NULL, &devmode, NULL, CDS_FULLSCREEN, NULL);
    if (ret != DISP_CHANGE_SUCCESSFUL)
    {
        if (devmode.dmDisplayFrequency)
        {
            WARN("ChangeDisplaySettingsExW failed, trying without the refresh rate\n");
            devmode.dmFields &= ~DM_DISPLAYFREQUENCY;
            devmode.dmDisplayFrequency = 0;
            ret = ChangeDisplaySettingsExW(NULL, &devmode, NULL, CDS_FULLSCREEN, NULL) != DISP_CHANGE_SUCCESSFUL;
        }
        if(ret != DISP_CHANGE_SUCCESSFUL) {
            return WINED3DERR_NOTAVAILABLE;
        }
    }

    /* Store the new values */
    This->ddraw_width = pMode->Width;
    This->ddraw_height = pMode->Height;
    This->ddraw_format = pMode->Format;

    /* And finally clip mouse to our screen */
    SetRect(&clip_rc, 0, 0, pMode->Width, pMode->Height);
    ClipCursor(&clip_rc);

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetDirect3D(IWineD3DDevice *iface, struct wined3d **wined3d)
{
    IWineD3DDeviceImpl *device = (IWineD3DDeviceImpl *)iface;

    TRACE("iface %p, wined3d %p.\n", iface, wined3d);

    *wined3d = device->wined3d;
    wined3d_incref(*wined3d);

    TRACE("Returning %p.\n", *wined3d);

    return WINED3D_OK;
}

static UINT WINAPI IWineD3DDeviceImpl_GetAvailableTextureMem(IWineD3DDevice *iface) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    TRACE("(%p) : simulating %dMB, returning %dMB left\n",  This,
         (This->adapter->TextureRam/(1024*1024)),
         ((This->adapter->TextureRam - This->adapter->UsedTextureRam) / (1024*1024)));
    /* return simulated texture memory left */
    return (This->adapter->TextureRam - This->adapter->UsedTextureRam);
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetStreamSource(IWineD3DDevice *iface, UINT StreamNumber,
        struct wined3d_buffer *buffer, UINT OffsetInBytes, UINT Stride)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    struct wined3d_stream_state *stream;
    struct wined3d_buffer *prev_buffer;

    TRACE("iface %p, stream_idx %u, buffer %p, offset %u, stride %u.\n",
            iface, StreamNumber, buffer, OffsetInBytes, Stride);

    if (StreamNumber >= MAX_STREAMS) {
        WARN("Stream out of range %d\n", StreamNumber);
        return WINED3DERR_INVALIDCALL;
    } else if(OffsetInBytes & 0x3) {
        WARN("OffsetInBytes is not 4 byte aligned: %d\n", OffsetInBytes);
        return WINED3DERR_INVALIDCALL;
    }

    stream = &This->updateStateBlock->state.streams[StreamNumber];
    prev_buffer = stream->buffer;

    This->updateStateBlock->changed.streamSource |= 1 << StreamNumber;

    if (prev_buffer == buffer
            && stream->stride == Stride
            && stream->offset == OffsetInBytes)
    {
       TRACE("Application is setting the old values over, nothing to do\n");
       return WINED3D_OK;
    }

    stream->buffer = buffer;
    if (buffer)
    {
        stream->stride = Stride;
        stream->offset = OffsetInBytes;
    }

    /* Handle recording of state blocks */
    if (This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        if (buffer)
            wined3d_buffer_incref(buffer);
        if (prev_buffer)
            wined3d_buffer_decref(prev_buffer);
        return WINED3D_OK;
    }

    if (buffer)
    {
        InterlockedIncrement(&buffer->bind_count);
        wined3d_buffer_incref(buffer);
    }
    if (prev_buffer)
    {
        InterlockedDecrement(&prev_buffer->bind_count);
        wined3d_buffer_decref(prev_buffer);
    }

    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_STREAMSRC);

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetStreamSource(IWineD3DDevice *iface,
        UINT StreamNumber, struct wined3d_buffer **buffer, UINT *pOffset, UINT *pStride)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    struct wined3d_stream_state *stream;

    TRACE("iface %p, stream_idx %u, buffer %p, offset %p, stride %p.\n",
            iface, StreamNumber, buffer, pOffset, pStride);

    if (StreamNumber >= MAX_STREAMS)
    {
        WARN("Stream out of range %d\n", StreamNumber);
        return WINED3DERR_INVALIDCALL;
    }

    stream = &This->stateBlock->state.streams[StreamNumber];
    *buffer = stream->buffer;
    *pStride = stream->stride;
    if (pOffset) *pOffset = stream->offset;

    if (*buffer)
        wined3d_buffer_incref(*buffer);

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetStreamSourceFreq(IWineD3DDevice *iface,  UINT StreamNumber, UINT Divider) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    struct wined3d_stream_state *stream;
    UINT old_flags, oldFreq;

    TRACE("iface %p, stream_idx %u, divider %#x.\n", iface, StreamNumber, Divider);

    /* Verify input at least in d3d9 this is invalid. */
    if ((Divider & WINED3DSTREAMSOURCE_INSTANCEDATA) && (Divider & WINED3DSTREAMSOURCE_INDEXEDDATA))
    {
        WARN("INSTANCEDATA and INDEXEDDATA were set, returning D3DERR_INVALIDCALL\n");
        return WINED3DERR_INVALIDCALL;
    }
    if ((Divider & WINED3DSTREAMSOURCE_INSTANCEDATA) && !StreamNumber)
    {
        WARN("INSTANCEDATA used on stream 0, returning D3DERR_INVALIDCALL\n");
        return WINED3DERR_INVALIDCALL;
    }
    if (!Divider)
    {
        WARN("Divider is 0, returning D3DERR_INVALIDCALL\n");
        return WINED3DERR_INVALIDCALL;
    }

    stream = &This->updateStateBlock->state.streams[StreamNumber];
    old_flags = stream->flags;
    oldFreq = stream->frequency;

    stream->flags = Divider & (WINED3DSTREAMSOURCE_INSTANCEDATA | WINED3DSTREAMSOURCE_INDEXEDDATA);
    stream->frequency = Divider & 0x7FFFFF;

    This->updateStateBlock->changed.streamFreq |= 1 << StreamNumber;

    if (stream->frequency != oldFreq || stream->flags != old_flags)
        IWineD3DDeviceImpl_MarkStateDirty(This, STATE_STREAMSRC);

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetStreamSourceFreq(IWineD3DDevice *iface,  UINT StreamNumber, UINT* Divider) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    struct wined3d_stream_state *stream;

    TRACE("iface %p, stream_idx %u, divider %p.\n", iface, StreamNumber, Divider);

    stream = &This->updateStateBlock->state.streams[StreamNumber];
    *Divider = stream->flags | stream->frequency;

    TRACE("Returning %#x.\n", *Divider);

    return WINED3D_OK;
}

/*****
 * Get / Set & Multiply Transform
 *****/
static HRESULT  WINAPI  IWineD3DDeviceImpl_SetTransform(IWineD3DDevice *iface, WINED3DTRANSFORMSTATETYPE d3dts, CONST WINED3DMATRIX* lpmatrix) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    /* Most of this routine, comments included copied from ddraw tree initially: */
    TRACE("(%p) : Transform State=%s\n", This, debug_d3dtstype(d3dts));

    /* Handle recording of state blocks */
    if (This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        This->updateStateBlock->changed.transform[d3dts >> 5] |= 1 << (d3dts & 0x1f);
        This->updateStateBlock->state.transforms[d3dts] = *lpmatrix;
        return WINED3D_OK;
    }

    /*
     * If the new matrix is the same as the current one,
     * we cut off any further processing. this seems to be a reasonable
     * optimization because as was noticed, some apps (warcraft3 for example)
     * tend towards setting the same matrix repeatedly for some reason.
     *
     * From here on we assume that the new matrix is different, wherever it matters.
     */
    if (!memcmp(&This->stateBlock->state.transforms[d3dts].u.m[0][0], lpmatrix, sizeof(*lpmatrix)))
    {
        TRACE("The app is setting the same matrix over again\n");
        return WINED3D_OK;
    }
    else
    {
        conv_mat(lpmatrix, &This->stateBlock->state.transforms[d3dts].u.m[0][0]);
    }

    /*
       ScreenCoord = ProjectionMat * ViewMat * WorldMat * ObjectCoord
       where ViewMat = Camera space, WorldMat = world space.

       In OpenGL, camera and world space is combined into GL_MODELVIEW
       matrix.  The Projection matrix stay projection matrix.
     */

    /* Capture the times we can just ignore the change for now */
    if (d3dts == WINED3DTS_VIEW) { /* handle the VIEW matrix */
        This->view_ident = !memcmp(lpmatrix, identity, 16 * sizeof(float));
        /* Handled by the state manager */
    }

    if (d3dts < WINED3DTS_WORLDMATRIX(This->adapter->gl_info.limits.blends))
        IWineD3DDeviceImpl_MarkStateDirty(This, STATE_TRANSFORM(d3dts));

    return WINED3D_OK;

}

static HRESULT WINAPI IWineD3DDeviceImpl_GetTransform(IWineD3DDevice *iface,
        WINED3DTRANSFORMSTATETYPE state, WINED3DMATRIX *matrix)
{
    IWineD3DDeviceImpl *device = (IWineD3DDeviceImpl *)iface;

    TRACE("iface %p, state %s, matrix %p.\n", iface, debug_d3dtstype(state), matrix);

    *matrix = device->stateBlock->state.transforms[state];

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_MultiplyTransform(IWineD3DDevice *iface, WINED3DTRANSFORMSTATETYPE State, CONST WINED3DMATRIX* pMatrix) {
    const WINED3DMATRIX *mat = NULL;
    WINED3DMATRIX temp;

    /* Note: Using 'updateStateBlock' rather than 'stateblock' in the code
     * below means it will be recorded in a state block change, but it
     * works regardless where it is recorded.
     * If this is found to be wrong, change to StateBlock.
     */
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) : For state %s\n", This, debug_d3dtstype(State));

    if (State <= HIGHEST_TRANSFORMSTATE)
    {
        mat = &This->updateStateBlock->state.transforms[State];
    }
    else
    {
        FIXME("Unhandled transform state!!\n");
    }

    multiply_matrix(&temp, mat, pMatrix);

    /* Apply change via set transform - will reapply to eg. lights this way */
    return IWineD3DDeviceImpl_SetTransform(iface, State, &temp);
}

/*****
 * Get / Set Light
 *****/
/* Note lights are real special cases. Although the device caps state only eg. 8 are supported,
   you can reference any indexes you want as long as that number max are enabled at any
   one point in time! Therefore since the indexes can be anything, we need a hashmap of them.
   However, this causes stateblock problems. When capturing the state block, I duplicate the hashmap,
   but when recording, just build a chain pretty much of commands to be replayed.                  */

static HRESULT WINAPI IWineD3DDeviceImpl_SetLight(IWineD3DDevice *iface, DWORD Index, CONST WINED3DLIGHT* pLight) {
    float rho;
    struct wined3d_light_info *object = NULL;
    UINT Hi = LIGHTMAP_HASHFUNC(Index);
    struct list *e;

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) : Idx(%d), pLight(%p). Hash index is %d\n", This, Index, pLight, Hi);

    /* Check the parameter range. Need for speed most wanted sets junk lights which confuse
     * the gl driver.
     */
    if(!pLight) {
        WARN("Light pointer = NULL, returning WINED3DERR_INVALIDCALL\n");
        return WINED3DERR_INVALIDCALL;
    }

    switch(pLight->Type) {
        case WINED3DLIGHT_POINT:
        case WINED3DLIGHT_SPOT:
        case WINED3DLIGHT_PARALLELPOINT:
        case WINED3DLIGHT_GLSPOT:
            /* Incorrect attenuation values can cause the gl driver to crash. Happens with Need for speed
             * most wanted
             */
            if (pLight->Attenuation0 < 0.0f || pLight->Attenuation1 < 0.0f || pLight->Attenuation2 < 0.0f)
            {
                WARN("Attenuation is negative, returning WINED3DERR_INVALIDCALL\n");
                return WINED3DERR_INVALIDCALL;
            }
            break;

        case WINED3DLIGHT_DIRECTIONAL:
            /* Ignores attenuation */
            break;

        default:
        WARN("Light type out of range, returning WINED3DERR_INVALIDCALL\n");
        return WINED3DERR_INVALIDCALL;
    }

    LIST_FOR_EACH(e, &This->updateStateBlock->state.light_map[Hi])
    {
        object = LIST_ENTRY(e, struct wined3d_light_info, entry);
        if(object->OriginalIndex == Index) break;
        object = NULL;
    }

    if(!object) {
        TRACE("Adding new light\n");
        object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object));
        if(!object) {
            ERR("Out of memory error when allocating a light\n");
            return E_OUTOFMEMORY;
        }
        list_add_head(&This->updateStateBlock->state.light_map[Hi], &object->entry);
        object->glIndex = -1;
        object->OriginalIndex = Index;
    }

    /* Initialize the object */
    TRACE("Light %d setting to type %d, Diffuse(%f,%f,%f,%f), Specular(%f,%f,%f,%f), Ambient(%f,%f,%f,%f)\n", Index, pLight->Type,
          pLight->Diffuse.r, pLight->Diffuse.g, pLight->Diffuse.b, pLight->Diffuse.a,
          pLight->Specular.r, pLight->Specular.g, pLight->Specular.b, pLight->Specular.a,
          pLight->Ambient.r, pLight->Ambient.g, pLight->Ambient.b, pLight->Ambient.a);
    TRACE("... Pos(%f,%f,%f), Dirn(%f,%f,%f)\n", pLight->Position.x, pLight->Position.y, pLight->Position.z,
          pLight->Direction.x, pLight->Direction.y, pLight->Direction.z);
    TRACE("... Range(%f), Falloff(%f), Theta(%f), Phi(%f)\n", pLight->Range, pLight->Falloff, pLight->Theta, pLight->Phi);

    /* Save away the information */
    object->OriginalParms = *pLight;

    switch (pLight->Type) {
    case WINED3DLIGHT_POINT:
        /* Position */
        object->lightPosn[0] = pLight->Position.x;
        object->lightPosn[1] = pLight->Position.y;
        object->lightPosn[2] = pLight->Position.z;
        object->lightPosn[3] = 1.0f;
        object->cutoff = 180.0f;
        /* FIXME: Range */
        break;

    case WINED3DLIGHT_DIRECTIONAL:
        /* Direction */
        object->lightPosn[0] = -pLight->Direction.x;
        object->lightPosn[1] = -pLight->Direction.y;
        object->lightPosn[2] = -pLight->Direction.z;
        object->lightPosn[3] = 0.0f;
        object->exponent     = 0.0f;
        object->cutoff       = 180.0f;
        break;

    case WINED3DLIGHT_SPOT:
        /* Position */
        object->lightPosn[0] = pLight->Position.x;
        object->lightPosn[1] = pLight->Position.y;
        object->lightPosn[2] = pLight->Position.z;
        object->lightPosn[3] = 1.0f;

        /* Direction */
        object->lightDirn[0] = pLight->Direction.x;
        object->lightDirn[1] = pLight->Direction.y;
        object->lightDirn[2] = pLight->Direction.z;
        object->lightDirn[3] = 1.0f;

        /*
         * opengl-ish and d3d-ish spot lights use too different models for the
         * light "intensity" as a function of the angle towards the main light direction,
         * so we only can approximate very roughly.
         * however spot lights are rather rarely used in games (if ever used at all).
         * furthermore if still used, probably nobody pays attention to such details.
         */
        if (!pLight->Falloff)
        {
            /* Falloff = 0 is easy, because d3d's and opengl's spot light equations have the
             * falloff resp. exponent parameter as an exponent, so the spot light lighting
             * will always be 1.0 for both of them, and we don't have to care for the
             * rest of the rather complex calculation
             */
            object->exponent = 0.0f;
        } else {
            rho = pLight->Theta + (pLight->Phi - pLight->Theta)/(2*pLight->Falloff);
            if (rho < 0.0001f) rho = 0.0001f;
            object->exponent = -0.3f/logf(cosf(rho/2));
        }
        if (object->exponent > 128.0f)
        {
            object->exponent = 128.0f;
        }
        object->cutoff = (float) (pLight->Phi*90/M_PI);

        /* FIXME: Range */
        break;

    default:
        FIXME("Unrecognized light type %d\n", pLight->Type);
    }

    /* Update the live definitions if the light is currently assigned a glIndex */
    if (object->glIndex != -1 && !This->isRecordingState) {
        IWineD3DDeviceImpl_MarkStateDirty(This, STATE_ACTIVELIGHT(object->glIndex));
    }
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetLight(IWineD3DDevice *iface, DWORD Index, WINED3DLIGHT *pLight)
{
    struct wined3d_light_info *lightInfo = NULL;
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    DWORD Hi = LIGHTMAP_HASHFUNC(Index);
    struct list *e;
    TRACE("(%p) : Idx(%d), pLight(%p)\n", This, Index, pLight);

    LIST_FOR_EACH(e, &This->stateBlock->state.light_map[Hi])
    {
        lightInfo = LIST_ENTRY(e, struct wined3d_light_info, entry);
        if(lightInfo->OriginalIndex == Index) break;
        lightInfo = NULL;
    }

    if (!lightInfo)
    {
        TRACE("Light information requested but light not defined\n");
        return WINED3DERR_INVALIDCALL;
    }

    *pLight = lightInfo->OriginalParms;
    return WINED3D_OK;
}

/*****
 * Get / Set Light Enable
 *   (Note for consistency, renamed d3dx function by adding the 'set' prefix)
 *****/
static HRESULT WINAPI IWineD3DDeviceImpl_SetLightEnable(IWineD3DDevice *iface, DWORD Index, BOOL Enable)
{
    struct wined3d_light_info *lightInfo = NULL;
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    UINT Hi = LIGHTMAP_HASHFUNC(Index);
    struct list *e;
    TRACE("(%p) : Idx(%d), enable? %d\n", This, Index, Enable);

    LIST_FOR_EACH(e, &This->updateStateBlock->state.light_map[Hi])
    {
        lightInfo = LIST_ENTRY(e, struct wined3d_light_info, entry);
        if(lightInfo->OriginalIndex == Index) break;
        lightInfo = NULL;
    }
    TRACE("Found light: %p\n", lightInfo);

    /* Special case - enabling an undefined light creates one with a strict set of parms! */
    if (!lightInfo)
    {
        TRACE("Light enabled requested but light not defined, so defining one!\n");
        IWineD3DDeviceImpl_SetLight(iface, Index, &WINED3D_default_light);

        /* Search for it again! Should be fairly quick as near head of list */
        LIST_FOR_EACH(e, &This->updateStateBlock->state.light_map[Hi])
        {
            lightInfo = LIST_ENTRY(e, struct wined3d_light_info, entry);
            if(lightInfo->OriginalIndex == Index) break;
            lightInfo = NULL;
        }
        if (!lightInfo)
        {
            FIXME("Adding default lights has failed dismally\n");
            return WINED3DERR_INVALIDCALL;
        }
    }

    if(!Enable) {
        if(lightInfo->glIndex != -1) {
            if(!This->isRecordingState) {
                IWineD3DDeviceImpl_MarkStateDirty(This, STATE_ACTIVELIGHT(lightInfo->glIndex));
            }

            This->updateStateBlock->state.lights[lightInfo->glIndex] = NULL;
            lightInfo->glIndex = -1;
        } else {
            TRACE("Light already disabled, nothing to do\n");
        }
        lightInfo->enabled = FALSE;
    } else {
        lightInfo->enabled = TRUE;
        if (lightInfo->glIndex != -1) {
            /* nop */
            TRACE("Nothing to do as light was enabled\n");
        } else {
            int i;
            /* Find a free gl light */
            for (i = 0; i < This->maxConcurrentLights; ++i)
            {
                if (!This->updateStateBlock->state.lights[i])
                {
                    This->updateStateBlock->state.lights[i] = lightInfo;
                    lightInfo->glIndex = i;
                    break;
                }
            }
            if(lightInfo->glIndex == -1) {
                /* Our tests show that Windows returns D3D_OK in this situation, even with
                 * D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE devices. This
                 * is consistent among ddraw, d3d8 and d3d9. GetLightEnable returns TRUE
                 * as well for those lights.
                 *
                 * TODO: Test how this affects rendering
                 */
                WARN("Too many concurrently active lights\n");
                return WINED3D_OK;
            }

            /* i == lightInfo->glIndex */
            if(!This->isRecordingState) {
                IWineD3DDeviceImpl_MarkStateDirty(This, STATE_ACTIVELIGHT(i));
            }
        }
    }

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetLightEnable(IWineD3DDevice *iface, DWORD Index,BOOL* pEnable)
{
    struct wined3d_light_info *lightInfo = NULL;
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    struct list *e;
    UINT Hi = LIGHTMAP_HASHFUNC(Index);
    TRACE("(%p) : for idx(%d)\n", This, Index);

    LIST_FOR_EACH(e, &This->stateBlock->state.light_map[Hi])
    {
        lightInfo = LIST_ENTRY(e, struct wined3d_light_info, entry);
        if(lightInfo->OriginalIndex == Index) break;
        lightInfo = NULL;
    }

    if (!lightInfo)
    {
        TRACE("Light enabled state requested but light not defined\n");
        return WINED3DERR_INVALIDCALL;
    }
    /* true is 128 according to SetLightEnable */
    *pEnable = lightInfo->enabled ? 128 : 0;
    return WINED3D_OK;
}

/*****
 * Get / Set Clip Planes
 *****/
static HRESULT WINAPI IWineD3DDeviceImpl_SetClipPlane(IWineD3DDevice *iface, DWORD Index, CONST float *pPlane) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) : for idx %d, %p\n", This, Index, pPlane);

    /* Validate Index */
    if (Index >= This->adapter->gl_info.limits.clipplanes)
    {
        TRACE("Application has requested clipplane this device doesn't support\n");
        return WINED3DERR_INVALIDCALL;
    }

    This->updateStateBlock->changed.clipplane |= 1 << Index;

    if (This->updateStateBlock->state.clip_planes[Index][0] == pPlane[0]
            && This->updateStateBlock->state.clip_planes[Index][1] == pPlane[1]
            && This->updateStateBlock->state.clip_planes[Index][2] == pPlane[2]
            && This->updateStateBlock->state.clip_planes[Index][3] == pPlane[3])
    {
        TRACE("Application is setting old values over, nothing to do\n");
        return WINED3D_OK;
    }

    This->updateStateBlock->state.clip_planes[Index][0] = pPlane[0];
    This->updateStateBlock->state.clip_planes[Index][1] = pPlane[1];
    This->updateStateBlock->state.clip_planes[Index][2] = pPlane[2];
    This->updateStateBlock->state.clip_planes[Index][3] = pPlane[3];

    /* Handle recording of state blocks */
    if (This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        return WINED3D_OK;
    }

    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_CLIPPLANE(Index));

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetClipPlane(IWineD3DDevice *iface, DWORD Index, float *pPlane) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) : for idx %d\n", This, Index);

    /* Validate Index */
    if (Index >= This->adapter->gl_info.limits.clipplanes)
    {
        TRACE("Application has requested clipplane this device doesn't support\n");
        return WINED3DERR_INVALIDCALL;
    }

    pPlane[0] = (float)This->stateBlock->state.clip_planes[Index][0];
    pPlane[1] = (float)This->stateBlock->state.clip_planes[Index][1];
    pPlane[2] = (float)This->stateBlock->state.clip_planes[Index][2];
    pPlane[3] = (float)This->stateBlock->state.clip_planes[Index][3];
    return WINED3D_OK;
}

/*****
 * Get / Set Clip Plane Status
 *   WARNING: This code relies on the fact that D3DCLIPSTATUS8 == D3DCLIPSTATUS9
 *****/
static HRESULT  WINAPI  IWineD3DDeviceImpl_SetClipStatus(IWineD3DDevice *iface, CONST WINED3DCLIPSTATUS* pClipStatus) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    FIXME("(%p) : stub\n", This);

    if (!pClipStatus)
        return WINED3DERR_INVALIDCALL;

    This->updateStateBlock->state.clip_status.ClipUnion = pClipStatus->ClipUnion;
    This->updateStateBlock->state.clip_status.ClipIntersection = pClipStatus->ClipIntersection;
    return WINED3D_OK;
}

static HRESULT  WINAPI  IWineD3DDeviceImpl_GetClipStatus(IWineD3DDevice *iface, WINED3DCLIPSTATUS* pClipStatus) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    FIXME("(%p) : stub\n", This);

    if (!pClipStatus)
        return WINED3DERR_INVALIDCALL;

    pClipStatus->ClipUnion = This->updateStateBlock->state.clip_status.ClipUnion;
    pClipStatus->ClipIntersection = This->updateStateBlock->state.clip_status.ClipIntersection;
    return WINED3D_OK;
}

/*****
 * Get / Set Material
 *****/
static HRESULT WINAPI IWineD3DDeviceImpl_SetMaterial(IWineD3DDevice *iface, CONST WINED3DMATERIAL* pMaterial) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    This->updateStateBlock->changed.material = TRUE;
    This->updateStateBlock->state.material = *pMaterial;

    /* Handle recording of state blocks */
    if (This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        return WINED3D_OK;
    }

    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_MATERIAL);
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetMaterial(IWineD3DDevice *iface, WINED3DMATERIAL* pMaterial) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    *pMaterial = This->updateStateBlock->state.material;
    TRACE("(%p) : Diffuse (%f,%f,%f,%f)\n", This, pMaterial->Diffuse.r, pMaterial->Diffuse.g,
        pMaterial->Diffuse.b, pMaterial->Diffuse.a);
    TRACE("(%p) : Ambient (%f,%f,%f,%f)\n", This, pMaterial->Ambient.r, pMaterial->Ambient.g,
        pMaterial->Ambient.b, pMaterial->Ambient.a);
    TRACE("(%p) : Specular (%f,%f,%f,%f)\n", This, pMaterial->Specular.r, pMaterial->Specular.g,
        pMaterial->Specular.b, pMaterial->Specular.a);
    TRACE("(%p) : Emissive (%f,%f,%f,%f)\n", This, pMaterial->Emissive.r, pMaterial->Emissive.g,
        pMaterial->Emissive.b, pMaterial->Emissive.a);
    TRACE("(%p) : Power (%f)\n", This, pMaterial->Power);

    return WINED3D_OK;
}

/*****
 * Get / Set Indices
 *****/
static HRESULT WINAPI IWineD3DDeviceImpl_SetIndexBuffer(IWineD3DDevice *iface,
        struct wined3d_buffer *buffer, enum wined3d_format_id fmt)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    struct wined3d_buffer *prev_buffer;

    TRACE("iface %p, buffer %p, format %s.\n",
            iface, buffer, debug_d3dformat(fmt));

    prev_buffer = This->updateStateBlock->state.index_buffer;

    This->updateStateBlock->changed.indices = TRUE;
    This->updateStateBlock->state.index_buffer = buffer;
    This->updateStateBlock->state.index_format = fmt;

    /* Handle recording of state blocks */
    if (This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        if (buffer)
            wined3d_buffer_incref(buffer);
        if (prev_buffer)
            wined3d_buffer_decref(prev_buffer);
        return WINED3D_OK;
    }

    if (prev_buffer != buffer)
    {
        IWineD3DDeviceImpl_MarkStateDirty(This, STATE_INDEXBUFFER);
        if (buffer)
        {
            InterlockedIncrement(&buffer->bind_count);
            wined3d_buffer_incref(buffer);
        }
        if (prev_buffer)
        {
            InterlockedDecrement(&prev_buffer->bind_count);
            wined3d_buffer_decref(prev_buffer);
        }
    }

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetIndexBuffer(IWineD3DDevice *iface, struct wined3d_buffer **buffer)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    TRACE("iface %p, buffer %p.\n", iface, buffer);

    *buffer = This->stateBlock->state.index_buffer;

    if (*buffer)
        wined3d_buffer_incref(*buffer);

    TRACE("Returning %p.\n", *buffer);

    return WINED3D_OK;
}

/* Method to offer d3d9 a simple way to set the base vertex index without messing with the index buffer */
static HRESULT WINAPI IWineD3DDeviceImpl_SetBaseVertexIndex(IWineD3DDevice *iface, INT BaseIndex) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p)->(%d)\n", This, BaseIndex);

    if (This->updateStateBlock->state.base_vertex_index == BaseIndex)
    {
        TRACE("Application is setting the old value over, nothing to do\n");
        return WINED3D_OK;
    }

    This->updateStateBlock->state.base_vertex_index = BaseIndex;

    if (This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        return WINED3D_OK;
    }
    /* The base vertex index affects the stream sources */
    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_STREAMSRC);
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetBaseVertexIndex(IWineD3DDevice *iface, INT* base_index) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) : base_index %p\n", This, base_index);

    *base_index = This->stateBlock->state.base_vertex_index;

    TRACE("Returning %u\n", *base_index);

    return WINED3D_OK;
}

/*****
 * Get / Set Viewports
 *****/
static HRESULT WINAPI IWineD3DDeviceImpl_SetViewport(IWineD3DDevice *iface, CONST WINED3DVIEWPORT* pViewport) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    TRACE("(%p)\n", This);
    This->updateStateBlock->changed.viewport = TRUE;
    This->updateStateBlock->state.viewport = *pViewport;

    /* Handle recording of state blocks */
    if (This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        return WINED3D_OK;
    }

    TRACE("(%p) : x=%d, y=%d, wid=%d, hei=%d, minz=%f, maxz=%f\n", This,
          pViewport->X, pViewport->Y, pViewport->Width, pViewport->Height, pViewport->MinZ, pViewport->MaxZ);

    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_VIEWPORT);
    return WINED3D_OK;

}

static HRESULT WINAPI IWineD3DDeviceImpl_GetViewport(IWineD3DDevice *iface, WINED3DVIEWPORT* pViewport) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p)\n", This);
    *pViewport = This->stateBlock->state.viewport;
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetRenderState(IWineD3DDevice *iface,
        WINED3DRENDERSTATETYPE State, DWORD Value)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    DWORD oldValue = This->stateBlock->state.render_states[State];

    TRACE("iface %p, state %s (%#x), value %#x.\n", iface, debug_d3drenderstate(State), State, Value);

    This->updateStateBlock->changed.renderState[State >> 5] |= 1 << (State & 0x1f);
    This->updateStateBlock->state.render_states[State] = Value;

    /* Handle recording of state blocks */
    if (This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        return WINED3D_OK;
    }

    /* Compared here and not before the assignment to allow proper stateblock recording */
    if(Value == oldValue) {
        TRACE("Application is setting the old value over, nothing to do\n");
    } else {
        IWineD3DDeviceImpl_MarkStateDirty(This, STATE_RENDER(State));
    }

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetRenderState(IWineD3DDevice *iface,
        WINED3DRENDERSTATETYPE State, DWORD *pValue)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    TRACE("iface %p, state %s (%#x), value %p.\n", iface, debug_d3drenderstate(State), State, pValue);

    *pValue = This->stateBlock->state.render_states[State];
    return WINED3D_OK;
}

/*****
 * Get / Set Sampler States
 * TODO: Verify against dx9 definitions
 *****/

static HRESULT WINAPI IWineD3DDeviceImpl_SetSamplerState(IWineD3DDevice *iface, DWORD Sampler, WINED3DSAMPLERSTATETYPE Type, DWORD Value) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    DWORD oldValue;

    TRACE("(%p) : Sampler %#x, Type %s (%#x), Value %#x\n",
            This, Sampler, debug_d3dsamplerstate(Type), Type, Value);

    if (Sampler >= WINED3DVERTEXTEXTURESAMPLER0 && Sampler <= WINED3DVERTEXTEXTURESAMPLER3) {
        Sampler -= (WINED3DVERTEXTEXTURESAMPLER0 - MAX_FRAGMENT_SAMPLERS);
    }

    if (Sampler >= sizeof(This->stateBlock->state.sampler_states) / sizeof(*This->stateBlock->state.sampler_states))
    {
        ERR("Current Sampler overflows sampleState0 array (sampler %d)\n", Sampler);
        return WINED3D_OK; /* Windows accepts overflowing this array ... we do not. */
    }

    oldValue = This->stateBlock->state.sampler_states[Sampler][Type];
    This->updateStateBlock->state.sampler_states[Sampler][Type] = Value;
    This->updateStateBlock->changed.samplerState[Sampler] |= 1 << Type;

    /* Handle recording of state blocks */
    if (This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        return WINED3D_OK;
    }

    if(oldValue == Value) {
        TRACE("Application is setting the old value over, nothing to do\n");
        return WINED3D_OK;
    }

    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_SAMPLER(Sampler));

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetSamplerState(IWineD3DDevice *iface, DWORD Sampler, WINED3DSAMPLERSTATETYPE Type, DWORD* Value) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    TRACE("(%p) : Sampler %#x, Type %s (%#x)\n",
            This, Sampler, debug_d3dsamplerstate(Type), Type);

    if (Sampler >= WINED3DVERTEXTEXTURESAMPLER0 && Sampler <= WINED3DVERTEXTEXTURESAMPLER3) {
        Sampler -= (WINED3DVERTEXTEXTURESAMPLER0 - MAX_FRAGMENT_SAMPLERS);
    }

    if (Sampler >= sizeof(This->stateBlock->state.sampler_states) / sizeof(*This->stateBlock->state.sampler_states))
    {
        ERR("Current Sampler overflows sampleState0 array (sampler %d)\n", Sampler);
        return WINED3D_OK; /* Windows accepts overflowing this array ... we do not. */
    }
    *Value = This->stateBlock->state.sampler_states[Sampler][Type];
    TRACE("(%p) : Returning %#x\n", This, *Value);

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetScissorRect(IWineD3DDevice *iface, CONST RECT* pRect) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    This->updateStateBlock->changed.scissorRect = TRUE;
    if (EqualRect(&This->updateStateBlock->state.scissor_rect, pRect))
    {
        TRACE("App is setting the old scissor rectangle over, nothing to do.\n");
        return WINED3D_OK;
    }
    CopyRect(&This->updateStateBlock->state.scissor_rect, pRect);

    if(This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        return WINED3D_OK;
    }

    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_SCISSORRECT);

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetScissorRect(IWineD3DDevice *iface, RECT* pRect) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    *pRect = This->updateStateBlock->state.scissor_rect;
    TRACE("(%p)Returning a Scissor Rect of %d:%d-%d:%d\n", This, pRect->left, pRect->top, pRect->right, pRect->bottom);
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetVertexDeclaration(IWineD3DDevice *iface,
        struct wined3d_vertex_declaration *pDecl)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *) iface;
    struct wined3d_vertex_declaration *oldDecl = This->updateStateBlock->state.vertex_declaration;

    TRACE("iface %p, declaration %p.\n", iface, pDecl);

    if (pDecl)
        wined3d_vertex_declaration_incref(pDecl);
    if (oldDecl)
        wined3d_vertex_declaration_decref(oldDecl);

    This->updateStateBlock->state.vertex_declaration = pDecl;
    This->updateStateBlock->changed.vertexDecl = TRUE;

    if (This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        return WINED3D_OK;
    } else if(pDecl == oldDecl) {
        /* Checked after the assignment to allow proper stateblock recording */
        TRACE("Application is setting the old declaration over, nothing to do\n");
        return WINED3D_OK;
    }

    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_VDECL);
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetVertexDeclaration(IWineD3DDevice *iface,
        struct wined3d_vertex_declaration **ppDecl)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    TRACE("iface %p, declaration %p.\n", iface, ppDecl);

    *ppDecl = This->stateBlock->state.vertex_declaration;
    if (*ppDecl)
        wined3d_vertex_declaration_incref(*ppDecl);

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetVertexShader(IWineD3DDevice *iface, struct wined3d_shader *shader)
{
    IWineD3DDeviceImpl *device = (IWineD3DDeviceImpl *)iface;
    struct wined3d_shader *prev = device->updateStateBlock->state.vertex_shader;

    device->updateStateBlock->state.vertex_shader = shader;
    device->updateStateBlock->changed.vertexShader = TRUE;

    if (device->isRecordingState)
    {
        if (shader)
            wined3d_shader_incref(shader);
        if (prev)
            wined3d_shader_decref(prev);
        TRACE("Recording... not performing anything.\n");
        return WINED3D_OK;
    }
    else if(prev == shader)
    {
        /* Checked here to allow proper stateblock recording */
        TRACE("App is setting the old shader over, nothing to do.\n");
        return WINED3D_OK;
    }

    TRACE("(%p) : setting shader(%p)\n", device, shader);
    if (shader)
        wined3d_shader_incref(shader);
    if (prev)
        wined3d_shader_decref(prev);

    IWineD3DDeviceImpl_MarkStateDirty(device, STATE_VSHADER);

    return WINED3D_OK;
}

static struct wined3d_shader * WINAPI IWineD3DDeviceImpl_GetVertexShader(IWineD3DDevice *iface)
{
    IWineD3DDeviceImpl *device = (IWineD3DDeviceImpl *)iface;
    struct wined3d_shader *shader;

    TRACE("iface %p.\n", iface);

    shader = device->stateBlock->state.vertex_shader;
    if (shader)
        wined3d_shader_incref(shader);

    TRACE("Returning %p.\n", shader);
    return shader;
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetVertexShaderConstantB(
    IWineD3DDevice *iface,
    UINT start,
    CONST BOOL *srcData,
    UINT count) {

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    unsigned int i, cnt = min(count, MAX_CONST_B - start);

    TRACE("(iface %p, srcData %p, start %d, count %d)\n",
            iface, srcData, start, count);

    if (!srcData || start >= MAX_CONST_B) return WINED3DERR_INVALIDCALL;

    memcpy(&This->updateStateBlock->state.vs_consts_b[start], srcData, cnt * sizeof(BOOL));
    for (i = 0; i < cnt; i++)
        TRACE("Set BOOL constant %u to %s\n", start + i, srcData[i]? "true":"false");

    for (i = start; i < cnt + start; ++i) {
        This->updateStateBlock->changed.vertexShaderConstantsB |= (1 << i);
    }

    if (!This->isRecordingState) IWineD3DDeviceImpl_MarkStateDirty(This, STATE_VERTEXSHADERCONSTANT);

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetVertexShaderConstantB(
    IWineD3DDevice *iface,
    UINT start,
    BOOL *dstData,
    UINT count) {

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    int cnt = min(count, MAX_CONST_B - start);

    TRACE("(iface %p, dstData %p, start %d, count %d)\n",
            iface, dstData, start, count);

    if (!dstData || cnt < 0)
        return WINED3DERR_INVALIDCALL;

    memcpy(dstData, &This->stateBlock->state.vs_consts_b[start], cnt * sizeof(BOOL));
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetVertexShaderConstantI(
    IWineD3DDevice *iface,
    UINT start,
    CONST int *srcData,
    UINT count) {

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    unsigned int i, cnt = min(count, MAX_CONST_I - start);

    TRACE("(iface %p, srcData %p, start %d, count %d)\n",
            iface, srcData, start, count);

    if (!srcData || start >= MAX_CONST_I) return WINED3DERR_INVALIDCALL;

    memcpy(&This->updateStateBlock->state.vs_consts_i[start * 4], srcData, cnt * sizeof(int) * 4);
    for (i = 0; i < cnt; i++)
        TRACE("Set INT constant %u to { %d, %d, %d, %d }\n", start + i,
           srcData[i*4], srcData[i*4+1], srcData[i*4+2], srcData[i*4+3]);

    for (i = start; i < cnt + start; ++i) {
        This->updateStateBlock->changed.vertexShaderConstantsI |= (1 << i);
    }

    if (!This->isRecordingState) IWineD3DDeviceImpl_MarkStateDirty(This, STATE_VERTEXSHADERCONSTANT);

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetVertexShaderConstantI(
    IWineD3DDevice *iface,
    UINT start,
    int *dstData,
    UINT count) {

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    int cnt = min(count, MAX_CONST_I - start);

    TRACE("(iface %p, dstData %p, start %d, count %d)\n",
            iface, dstData, start, count);

    if (!dstData || ((signed int)MAX_CONST_I - (signed int)start) <= 0)
        return WINED3DERR_INVALIDCALL;

    memcpy(dstData, &This->stateBlock->state.vs_consts_i[start * 4], cnt * sizeof(int) * 4);
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetVertexShaderConstantF(
    IWineD3DDevice *iface,
    UINT start,
    CONST float *srcData,
    UINT count) {

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    UINT i;

    TRACE("(iface %p, srcData %p, start %d, count %d)\n",
            iface, srcData, start, count);

    /* Specifically test start > limit to catch MAX_UINT overflows when adding start + count */
    if (!srcData || start + count > This->d3d_vshader_constantF || start > This->d3d_vshader_constantF)
        return WINED3DERR_INVALIDCALL;

    memcpy(&This->updateStateBlock->state.vs_consts_f[start * 4], srcData, count * sizeof(float) * 4);
    if(TRACE_ON(d3d)) {
        for (i = 0; i < count; i++)
            TRACE("Set FLOAT constant %u to { %f, %f, %f, %f }\n", start + i,
                srcData[i*4], srcData[i*4+1], srcData[i*4+2], srcData[i*4+3]);
    }

    if (!This->isRecordingState)
    {
        This->shader_backend->shader_update_float_vertex_constants(This, start, count);
        IWineD3DDeviceImpl_MarkStateDirty(This, STATE_VERTEXSHADERCONSTANT);
    }

    memset(This->updateStateBlock->changed.vertexShaderConstantsF + start, 1,
            sizeof(*This->updateStateBlock->changed.vertexShaderConstantsF) * count);

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetVertexShaderConstantF(
    IWineD3DDevice *iface,
    UINT start,
    float *dstData,
    UINT count) {

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    int cnt = min(count, This->d3d_vshader_constantF - start);

    TRACE("(iface %p, dstData %p, start %d, count %d)\n",
            iface, dstData, start, count);

    if (!dstData || cnt < 0)
        return WINED3DERR_INVALIDCALL;

    memcpy(dstData, &This->stateBlock->state.vs_consts_f[start * 4], cnt * sizeof(float) * 4);
    return WINED3D_OK;
}

static inline void markTextureStagesDirty(IWineD3DDeviceImpl *This, DWORD stage) {
    DWORD i;
    for(i = 0; i <= WINED3D_HIGHEST_TEXTURE_STATE; ++i)
    {
        IWineD3DDeviceImpl_MarkStateDirty(This, STATE_TEXTURESTAGE(stage, i));
    }
}

static void device_map_stage(IWineD3DDeviceImpl *This, DWORD stage, DWORD unit)
{
    DWORD i = This->rev_tex_unit_map[unit];
    DWORD j = This->texUnitMap[stage];

    This->texUnitMap[stage] = unit;
    if (i != WINED3D_UNMAPPED_STAGE && i != stage)
    {
        This->texUnitMap[i] = WINED3D_UNMAPPED_STAGE;
    }

    This->rev_tex_unit_map[unit] = stage;
    if (j != WINED3D_UNMAPPED_STAGE && j != unit)
    {
        This->rev_tex_unit_map[j] = WINED3D_UNMAPPED_STAGE;
    }
}

static void device_update_fixed_function_usage_map(IWineD3DDeviceImpl *This) {
    int i;

    This->fixed_function_usage_map = 0;
    for (i = 0; i < MAX_TEXTURES; ++i)
    {
        const struct wined3d_state *state = &This->stateBlock->state;
        WINED3DTEXTUREOP color_op = state->texture_states[i][WINED3DTSS_COLOROP];
        WINED3DTEXTUREOP alpha_op = state->texture_states[i][WINED3DTSS_ALPHAOP];
        DWORD color_arg1 = state->texture_states[i][WINED3DTSS_COLORARG1] & WINED3DTA_SELECTMASK;
        DWORD color_arg2 = state->texture_states[i][WINED3DTSS_COLORARG2] & WINED3DTA_SELECTMASK;
        DWORD color_arg3 = state->texture_states[i][WINED3DTSS_COLORARG0] & WINED3DTA_SELECTMASK;
        DWORD alpha_arg1 = state->texture_states[i][WINED3DTSS_ALPHAARG1] & WINED3DTA_SELECTMASK;
        DWORD alpha_arg2 = state->texture_states[i][WINED3DTSS_ALPHAARG2] & WINED3DTA_SELECTMASK;
        DWORD alpha_arg3 = state->texture_states[i][WINED3DTSS_ALPHAARG0] & WINED3DTA_SELECTMASK;

        if (color_op == WINED3DTOP_DISABLE) {
            /* Not used, and disable higher stages */
            break;
        }

        if (((color_arg1 == WINED3DTA_TEXTURE) && color_op != WINED3DTOP_SELECTARG2)
                || ((color_arg2 == WINED3DTA_TEXTURE) && color_op != WINED3DTOP_SELECTARG1)
                || ((color_arg3 == WINED3DTA_TEXTURE) && (color_op == WINED3DTOP_MULTIPLYADD || color_op == WINED3DTOP_LERP))
                || ((alpha_arg1 == WINED3DTA_TEXTURE) && alpha_op != WINED3DTOP_SELECTARG2)
                || ((alpha_arg2 == WINED3DTA_TEXTURE) && alpha_op != WINED3DTOP_SELECTARG1)
                || ((alpha_arg3 == WINED3DTA_TEXTURE) && (alpha_op == WINED3DTOP_MULTIPLYADD || alpha_op == WINED3DTOP_LERP))) {
            This->fixed_function_usage_map |= (1 << i);
        }

        if ((color_op == WINED3DTOP_BUMPENVMAP || color_op == WINED3DTOP_BUMPENVMAPLUMINANCE) && i < MAX_TEXTURES - 1) {
            This->fixed_function_usage_map |= (1 << (i + 1));
        }
    }
}

static void device_map_fixed_function_samplers(IWineD3DDeviceImpl *This, const struct wined3d_gl_info *gl_info)
{
    unsigned int i, tex;
    WORD ffu_map;

    device_update_fixed_function_usage_map(This);
    ffu_map = This->fixed_function_usage_map;

    if (This->max_ffp_textures == gl_info->limits.texture_stages
            || This->stateBlock->state.lowest_disabled_stage <= This->max_ffp_textures)
    {
        for (i = 0; ffu_map; ffu_map >>= 1, ++i)
        {
            if (!(ffu_map & 1)) continue;

            if (This->texUnitMap[i] != i) {
                device_map_stage(This, i, i);
                IWineD3DDeviceImpl_MarkStateDirty(This, STATE_SAMPLER(i));
                markTextureStagesDirty(This, i);
            }
        }
        return;
    }

    /* Now work out the mapping */
    tex = 0;
    for (i = 0; ffu_map; ffu_map >>= 1, ++i)
    {
        if (!(ffu_map & 1)) continue;

        if (This->texUnitMap[i] != tex) {
            device_map_stage(This, i, tex);
            IWineD3DDeviceImpl_MarkStateDirty(This, STATE_SAMPLER(i));
            markTextureStagesDirty(This, i);
        }

        ++tex;
    }
}

static void device_map_psamplers(IWineD3DDeviceImpl *This, const struct wined3d_gl_info *gl_info)
{
    const WINED3DSAMPLER_TEXTURE_TYPE *sampler_type =
            This->stateBlock->state.pixel_shader->reg_maps.sampler_type;
    unsigned int i;

    for (i = 0; i < MAX_FRAGMENT_SAMPLERS; ++i) {
        if (sampler_type[i] && This->texUnitMap[i] != i)
        {
            device_map_stage(This, i, i);
            IWineD3DDeviceImpl_MarkStateDirty(This, STATE_SAMPLER(i));
            if (i < gl_info->limits.texture_stages)
            {
                markTextureStagesDirty(This, i);
            }
        }
    }
}

static BOOL device_unit_free_for_vs(IWineD3DDeviceImpl *This, const WINED3DSAMPLER_TEXTURE_TYPE *pshader_sampler_tokens,
        const WINED3DSAMPLER_TEXTURE_TYPE *vshader_sampler_tokens, DWORD unit)
{
    DWORD current_mapping = This->rev_tex_unit_map[unit];

    /* Not currently used */
    if (current_mapping == WINED3D_UNMAPPED_STAGE) return TRUE;

    if (current_mapping < MAX_FRAGMENT_SAMPLERS) {
        /* Used by a fragment sampler */

        if (!pshader_sampler_tokens) {
            /* No pixel shader, check fixed function */
            return current_mapping >= MAX_TEXTURES || !(This->fixed_function_usage_map & (1 << current_mapping));
        }

        /* Pixel shader, check the shader's sampler map */
        return !pshader_sampler_tokens[current_mapping];
    }

    /* Used by a vertex sampler */
    return !vshader_sampler_tokens[current_mapping - MAX_FRAGMENT_SAMPLERS];
}

static void device_map_vsamplers(IWineD3DDeviceImpl *This, BOOL ps, const struct wined3d_gl_info *gl_info)
{
    const WINED3DSAMPLER_TEXTURE_TYPE *vshader_sampler_type =
            This->stateBlock->state.vertex_shader->reg_maps.sampler_type;
    const WINED3DSAMPLER_TEXTURE_TYPE *pshader_sampler_type = NULL;
    int start = min(MAX_COMBINED_SAMPLERS, gl_info->limits.combined_samplers) - 1;
    int i;

    if (ps)
    {
        /* Note that we only care if a sampler is sampled or not, not the sampler's specific type.
         * Otherwise we'd need to call shader_update_samplers() here for 1.x pixelshaders. */
        pshader_sampler_type = This->stateBlock->state.pixel_shader->reg_maps.sampler_type;
    }

    for (i = 0; i < MAX_VERTEX_SAMPLERS; ++i) {
        DWORD vsampler_idx = i + MAX_FRAGMENT_SAMPLERS;
        if (vshader_sampler_type[i])
        {
            if (This->texUnitMap[vsampler_idx] != WINED3D_UNMAPPED_STAGE)
            {
                /* Already mapped somewhere */
                continue;
            }

            while (start >= 0) {
                if (device_unit_free_for_vs(This, pshader_sampler_type, vshader_sampler_type, start))
                {
                    device_map_stage(This, vsampler_idx, start);
                    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_SAMPLER(vsampler_idx));

                    --start;
                    break;
                }

                --start;
            }
        }
    }
}

void IWineD3DDeviceImpl_FindTexUnitMap(IWineD3DDeviceImpl *This)
{
    const struct wined3d_gl_info *gl_info = &This->adapter->gl_info;
    const struct wined3d_state *state = &This->stateBlock->state;
    BOOL vs = use_vs(state);
    BOOL ps = use_ps(state);
    /*
     * Rules are:
     * -> Pixel shaders need a 1:1 map. In theory the shader input could be mapped too, but
     * that would be really messy and require shader recompilation
     * -> When the mapping of a stage is changed, sampler and ALL texture stage states have
     * to be reset. Because of that try to work with a 1:1 mapping as much as possible
     */
    if (ps) device_map_psamplers(This, gl_info);
    else device_map_fixed_function_samplers(This, gl_info);

    if (vs) device_map_vsamplers(This, ps, gl_info);
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetPixelShader(IWineD3DDevice *iface, struct wined3d_shader *shader)
{
    IWineD3DDeviceImpl *device = (IWineD3DDeviceImpl *)iface;
    struct wined3d_shader *prev = device->updateStateBlock->state.pixel_shader;

    device->updateStateBlock->state.pixel_shader = shader;
    device->updateStateBlock->changed.pixelShader = TRUE;

    /* Handle recording of state blocks */
    if (device->isRecordingState)
        TRACE("Recording... not performing anything\n");

    if (device->isRecordingState)
    {
        TRACE("Recording... not performing anything.\n");
        if (shader)
            wined3d_shader_incref(shader);
        if (prev)
            wined3d_shader_decref(prev);
        return WINED3D_OK;
    }

    if (shader == prev)
    {
        TRACE("App is setting the old pixel shader over, nothing to do.\n");
        return WINED3D_OK;
    }

    if (shader)
        wined3d_shader_incref(shader);
    if (prev)
        wined3d_shader_decref(prev);

    TRACE("Setting shader %p.\n", shader);
    IWineD3DDeviceImpl_MarkStateDirty(device, STATE_PIXELSHADER);

    return WINED3D_OK;
}

static struct wined3d_shader * WINAPI IWineD3DDeviceImpl_GetPixelShader(IWineD3DDevice *iface)
{
    IWineD3DDeviceImpl *device = (IWineD3DDeviceImpl *)iface;
    struct wined3d_shader *shader;

    TRACE("iface %p.\n", iface);

    shader = device->stateBlock->state.pixel_shader;
    if (shader)
        wined3d_shader_incref(shader);

    TRACE("Returning %p.\n", shader);
    return shader;
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetPixelShaderConstantB(
    IWineD3DDevice *iface,
    UINT start,
    CONST BOOL *srcData,
    UINT count) {

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    unsigned int i, cnt = min(count, MAX_CONST_B - start);

    TRACE("(iface %p, srcData %p, start %u, count %u)\n",
            iface, srcData, start, count);

    if (!srcData || start >= MAX_CONST_B) return WINED3DERR_INVALIDCALL;

    memcpy(&This->updateStateBlock->state.ps_consts_b[start], srcData, cnt * sizeof(BOOL));
    for (i = 0; i < cnt; i++)
        TRACE("Set BOOL constant %u to %s\n", start + i, srcData[i]? "true":"false");

    for (i = start; i < cnt + start; ++i) {
        This->updateStateBlock->changed.pixelShaderConstantsB |= (1 << i);
    }

    if (!This->isRecordingState) IWineD3DDeviceImpl_MarkStateDirty(This, STATE_PIXELSHADERCONSTANT);

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetPixelShaderConstantB(
    IWineD3DDevice *iface,
    UINT start,
    BOOL *dstData,
    UINT count) {

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    int cnt = min(count, MAX_CONST_B - start);

    TRACE("(iface %p, dstData %p, start %d, count %d)\n",
            iface, dstData, start, count);

    if (!dstData || cnt < 0)
        return WINED3DERR_INVALIDCALL;

    memcpy(dstData, &This->stateBlock->state.ps_consts_b[start], cnt * sizeof(BOOL));
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetPixelShaderConstantI(
    IWineD3DDevice *iface,
    UINT start,
    CONST int *srcData,
    UINT count) {

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    unsigned int i, cnt = min(count, MAX_CONST_I - start);

    TRACE("(iface %p, srcData %p, start %u, count %u)\n",
            iface, srcData, start, count);

    if (!srcData || start >= MAX_CONST_I) return WINED3DERR_INVALIDCALL;

    memcpy(&This->updateStateBlock->state.ps_consts_i[start * 4], srcData, cnt * sizeof(int) * 4);
    for (i = 0; i < cnt; i++)
        TRACE("Set INT constant %u to { %d, %d, %d, %d }\n", start + i,
           srcData[i*4], srcData[i*4+1], srcData[i*4+2], srcData[i*4+3]);

    for (i = start; i < cnt + start; ++i) {
        This->updateStateBlock->changed.pixelShaderConstantsI |= (1 << i);
    }

    if (!This->isRecordingState) IWineD3DDeviceImpl_MarkStateDirty(This, STATE_PIXELSHADERCONSTANT);

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetPixelShaderConstantI(
    IWineD3DDevice *iface,
    UINT start,
    int *dstData,
    UINT count) {

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    int cnt = min(count, MAX_CONST_I - start);

    TRACE("(iface %p, dstData %p, start %d, count %d)\n",
            iface, dstData, start, count);

    if (!dstData || cnt < 0)
        return WINED3DERR_INVALIDCALL;

    memcpy(dstData, &This->stateBlock->state.ps_consts_i[start * 4], cnt * sizeof(int) * 4);
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetPixelShaderConstantF(
    IWineD3DDevice *iface,
    UINT start,
    CONST float *srcData,
    UINT count) {

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    UINT i;

    TRACE("(iface %p, srcData %p, start %d, count %d)\n",
            iface, srcData, start, count);

    /* Specifically test start > limit to catch MAX_UINT overflows when adding start + count */
    if (!srcData || start + count > This->d3d_pshader_constantF || start > This->d3d_pshader_constantF)
        return WINED3DERR_INVALIDCALL;

    memcpy(&This->updateStateBlock->state.ps_consts_f[start * 4], srcData, count * sizeof(float) * 4);
    if(TRACE_ON(d3d)) {
        for (i = 0; i < count; i++)
            TRACE("Set FLOAT constant %u to { %f, %f, %f, %f }\n", start + i,
                srcData[i*4], srcData[i*4+1], srcData[i*4+2], srcData[i*4+3]);
    }

    if (!This->isRecordingState)
    {
        This->shader_backend->shader_update_float_pixel_constants(This, start, count);
        IWineD3DDeviceImpl_MarkStateDirty(This, STATE_PIXELSHADERCONSTANT);
    }

    memset(This->updateStateBlock->changed.pixelShaderConstantsF + start, 1,
            sizeof(*This->updateStateBlock->changed.pixelShaderConstantsF) * count);

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetPixelShaderConstantF(
    IWineD3DDevice *iface,
    UINT start,
    float *dstData,
    UINT count) {

    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    int cnt = min(count, This->d3d_pshader_constantF - start);

    TRACE("(iface %p, dstData %p, start %d, count %d)\n",
            iface, dstData, start, count);

    if (!dstData || cnt < 0)
        return WINED3DERR_INVALIDCALL;

    memcpy(dstData, &This->stateBlock->state.ps_consts_f[start * 4], cnt * sizeof(float) * 4);
    return WINED3D_OK;
}

/* Context activation is done by the caller. */
/* Do not call while under the GL lock. */
#define copy_and_next(dest, src, size) memcpy(dest, src, size); dest += (size)
static HRESULT process_vertices_strided(IWineD3DDeviceImpl *This, DWORD dwDestIndex, DWORD dwCount,
        const struct wined3d_stream_info *stream_info, struct wined3d_buffer *dest, DWORD flags,
        DWORD DestFVF)
{
    const struct wined3d_gl_info *gl_info = &This->adapter->gl_info;
    char *dest_ptr, *dest_conv = NULL, *dest_conv_addr = NULL;
    unsigned int i;
    WINED3DVIEWPORT vp;
    WINED3DMATRIX mat, proj_mat, view_mat, world_mat;
    BOOL doClip;
    DWORD numTextures;

    if (stream_info->use_map & (1 << WINED3D_FFP_NORMAL))
    {
        WARN(" lighting state not saved yet... Some strange stuff may happen !\n");
    }

    if (!(stream_info->use_map & (1 << WINED3D_FFP_POSITION)))
    {
        ERR("Source has no position mask\n");
        return WINED3DERR_INVALIDCALL;
    }

    if (!dest->resource.allocatedMemory)
        buffer_get_sysmem(dest, gl_info);

    /* Get a pointer into the destination vbo(create one if none exists) and
     * write correct opengl data into it. It's cheap and allows us to run drawStridedFast
     */
    if (!dest->buffer_object && gl_info->supported[ARB_VERTEX_BUFFER_OBJECT])
    {
        dest->flags |= WINED3D_BUFFER_CREATEBO;
        wined3d_buffer_preload(dest);
    }

    if (dest->buffer_object)
    {
        unsigned char extrabytes = 0;
        /* If the destination vertex buffer has D3DFVF_XYZ position(non-rhw), native d3d writes RHW position, where the RHW
         * gets written into the 4 bytes after the Z position. In the case of a dest buffer that only has D3DFVF_XYZ data,
         * this may write 4 extra bytes beyond the area that should be written
         */
        if(DestFVF == WINED3DFVF_XYZ) extrabytes = 4;
        dest_conv_addr = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwCount * get_flexible_vertex_size(DestFVF) + extrabytes);
        if(!dest_conv_addr) {
            ERR("Out of memory\n");
            /* Continue without storing converted vertices */
        }
        dest_conv = dest_conv_addr;
    }

    if (This->stateBlock->state.render_states[WINED3DRS_CLIPPING])
    {
        static BOOL warned = FALSE;
        /*
         * The clipping code is not quite correct. Some things need
         * to be checked against IDirect3DDevice3 (!), d3d8 and d3d9,
         * so disable clipping for now.
         * (The graphics in Half-Life are broken, and my processvertices
         *  test crashes with IDirect3DDevice3)
        doClip = TRUE;
         */
        doClip = FALSE;
        if(!warned) {
           warned = TRUE;
           FIXME("Clipping is broken and disabled for now\n");
        }
    } else doClip = FALSE;
    dest_ptr = ((char *)buffer_get_sysmem(dest, gl_info)) + dwDestIndex * get_flexible_vertex_size(DestFVF);

    IWineD3DDevice_GetTransform( (IWineD3DDevice *) This,
                                 WINED3DTS_VIEW,
                                 &view_mat);
    IWineD3DDevice_GetTransform( (IWineD3DDevice *) This,
                                 WINED3DTS_PROJECTION,
                                 &proj_mat);
    IWineD3DDevice_GetTransform( (IWineD3DDevice *) This,
                                 WINED3DTS_WORLDMATRIX(0),
                                 &world_mat);

    TRACE("View mat:\n");
    TRACE("%f %f %f %f\n", view_mat.u.s._11, view_mat.u.s._12, view_mat.u.s._13, view_mat.u.s._14);
    TRACE("%f %f %f %f\n", view_mat.u.s._21, view_mat.u.s._22, view_mat.u.s._23, view_mat.u.s._24);
    TRACE("%f %f %f %f\n", view_mat.u.s._31, view_mat.u.s._32, view_mat.u.s._33, view_mat.u.s._34);
    TRACE("%f %f %f %f\n", view_mat.u.s._41, view_mat.u.s._42, view_mat.u.s._43, view_mat.u.s._44);

    TRACE("Proj mat:\n");
    TRACE("%f %f %f %f\n", proj_mat.u.s._11, proj_mat.u.s._12, proj_mat.u.s._13, proj_mat.u.s._14);
    TRACE("%f %f %f %f\n", proj_mat.u.s._21, proj_mat.u.s._22, proj_mat.u.s._23, proj_mat.u.s._24);
    TRACE("%f %f %f %f\n", proj_mat.u.s._31, proj_mat.u.s._32, proj_mat.u.s._33, proj_mat.u.s._34);
    TRACE("%f %f %f %f\n", proj_mat.u.s._41, proj_mat.u.s._42, proj_mat.u.s._43, proj_mat.u.s._44);

    TRACE("World mat:\n");
    TRACE("%f %f %f %f\n", world_mat.u.s._11, world_mat.u.s._12, world_mat.u.s._13, world_mat.u.s._14);
    TRACE("%f %f %f %f\n", world_mat.u.s._21, world_mat.u.s._22, world_mat.u.s._23, world_mat.u.s._24);
    TRACE("%f %f %f %f\n", world_mat.u.s._31, world_mat.u.s._32, world_mat.u.s._33, world_mat.u.s._34);
    TRACE("%f %f %f %f\n", world_mat.u.s._41, world_mat.u.s._42, world_mat.u.s._43, world_mat.u.s._44);

    /* Get the viewport */
    IWineD3DDevice_GetViewport( (IWineD3DDevice *) This, &vp);
    TRACE("Viewport: X=%d, Y=%d, Width=%d, Height=%d, MinZ=%f, MaxZ=%f\n",
          vp.X, vp.Y, vp.Width, vp.Height, vp.MinZ, vp.MaxZ);

    multiply_matrix(&mat,&view_mat,&world_mat);
    multiply_matrix(&mat,&proj_mat,&mat);

    numTextures = (DestFVF & WINED3DFVF_TEXCOUNT_MASK) >> WINED3DFVF_TEXCOUNT_SHIFT;

    for (i = 0; i < dwCount; i+= 1) {
        unsigned int tex_index;

        if ( ((DestFVF & WINED3DFVF_POSITION_MASK) == WINED3DFVF_XYZ ) ||
             ((DestFVF & WINED3DFVF_POSITION_MASK) == WINED3DFVF_XYZRHW ) ) {
            /* The position first */
            const struct wined3d_stream_info_element *element = &stream_info->elements[WINED3D_FFP_POSITION];
            const float *p = (const float *)(element->data + i * element->stride);
            float x, y, z, rhw;
            TRACE("In: ( %06.2f %06.2f %06.2f )\n", p[0], p[1], p[2]);

            /* Multiplication with world, view and projection matrix */
            x =   (p[0] * mat.u.s._11) + (p[1] * mat.u.s._21) + (p[2] * mat.u.s._31) + (1.0f * mat.u.s._41);
            y =   (p[0] * mat.u.s._12) + (p[1] * mat.u.s._22) + (p[2] * mat.u.s._32) + (1.0f * mat.u.s._42);
            z =   (p[0] * mat.u.s._13) + (p[1] * mat.u.s._23) + (p[2] * mat.u.s._33) + (1.0f * mat.u.s._43);
            rhw = (p[0] * mat.u.s._14) + (p[1] * mat.u.s._24) + (p[2] * mat.u.s._34) + (1.0f * mat.u.s._44);

            TRACE("x=%f y=%f z=%f rhw=%f\n", x, y, z, rhw);

            /* WARNING: The following things are taken from d3d7 and were not yet checked
             * against d3d8 or d3d9!
             */

            /* Clipping conditions: From msdn
             *
             * A vertex is clipped if it does not match the following requirements
             * -rhw < x <= rhw
             * -rhw < y <= rhw
             *    0 < z <= rhw
             *    0 < rhw ( Not in d3d7, but tested in d3d7)
             *
             * If clipping is on is determined by the D3DVOP_CLIP flag in D3D7, and
             * by the D3DRS_CLIPPING in D3D9(according to the msdn, not checked)
             *
             */

            if( !doClip ||
                ( (-rhw -eps < x) && (-rhw -eps < y) && ( -eps < z) &&
                  (x <= rhw + eps) && (y <= rhw + eps ) && (z <= rhw + eps) &&
                  ( rhw > eps ) ) ) {

                /* "Normal" viewport transformation (not clipped)
                 * 1) The values are divided by rhw
                 * 2) The y axis is negative, so multiply it with -1
                 * 3) Screen coordinates go from -(Width/2) to +(Width/2) and
                 *    -(Height/2) to +(Height/2). The z range is MinZ to MaxZ
                 * 4) Multiply x with Width/2 and add Width/2
                 * 5) The same for the height
                 * 6) Add the viewpoint X and Y to the 2D coordinates and
                 *    The minimum Z value to z
                 * 7) rhw = 1 / rhw Reciprocal of Homogeneous W....
                 *
                 * Well, basically it's simply a linear transformation into viewport
                 * coordinates
                 */

                x /= rhw;
                y /= rhw;
                z /= rhw;

                y *= -1;

                x *= vp.Width / 2;
                y *= vp.Height / 2;
                z *= vp.MaxZ - vp.MinZ;

                x += vp.Width / 2 + vp.X;
                y += vp.Height / 2 + vp.Y;
                z += vp.MinZ;

                rhw = 1 / rhw;
            } else {
                /* That vertex got clipped
                 * Contrary to OpenGL it is not dropped completely, it just
                 * undergoes a different calculation.
                 */
                TRACE("Vertex got clipped\n");
                x += rhw;
                y += rhw;

                x  /= 2;
                y  /= 2;

                /* Msdn mentions that Direct3D9 keeps a list of clipped vertices
                 * outside of the main vertex buffer memory. That needs some more
                 * investigation...
                 */
            }

            TRACE("Writing (%f %f %f) %f\n", x, y, z, rhw);


            ( (float *) dest_ptr)[0] = x;
            ( (float *) dest_ptr)[1] = y;
            ( (float *) dest_ptr)[2] = z;
            ( (float *) dest_ptr)[3] = rhw; /* SIC, see ddraw test! */

            dest_ptr += 3 * sizeof(float);

            if((DestFVF & WINED3DFVF_POSITION_MASK) == WINED3DFVF_XYZRHW) {
                dest_ptr += sizeof(float);
            }

            if(dest_conv) {
                float w = 1 / rhw;
                ( (float *) dest_conv)[0] = x * w;
                ( (float *) dest_conv)[1] = y * w;
                ( (float *) dest_conv)[2] = z * w;
                ( (float *) dest_conv)[3] = w;

                dest_conv += 3 * sizeof(float);

                if((DestFVF & WINED3DFVF_POSITION_MASK) == WINED3DFVF_XYZRHW) {
                    dest_conv += sizeof(float);
                }
            }
        }
        if (DestFVF & WINED3DFVF_PSIZE) {
            dest_ptr += sizeof(DWORD);
            if(dest_conv) dest_conv += sizeof(DWORD);
        }
        if (DestFVF & WINED3DFVF_NORMAL) {
            const struct wined3d_stream_info_element *element = &stream_info->elements[WINED3D_FFP_NORMAL];
            const float *normal = (const float *)(element->data + i * element->stride);
            /* AFAIK this should go into the lighting information */
            FIXME("Didn't expect the destination to have a normal\n");
            copy_and_next(dest_ptr, normal, 3 * sizeof(float));
            if(dest_conv) {
                copy_and_next(dest_conv, normal, 3 * sizeof(float));
            }
        }

        if (DestFVF & WINED3DFVF_DIFFUSE) {
            const struct wined3d_stream_info_element *element = &stream_info->elements[WINED3D_FFP_DIFFUSE];
            const DWORD *color_d = (const DWORD *)(element->data + i * element->stride);
            if (!(stream_info->use_map & (1 << WINED3D_FFP_DIFFUSE)))
            {
                static BOOL warned = FALSE;

                if(!warned) {
                    ERR("No diffuse color in source, but destination has one\n");
                    warned = TRUE;
                }

                *( (DWORD *) dest_ptr) = 0xffffffff;
                dest_ptr += sizeof(DWORD);

                if(dest_conv) {
                    *( (DWORD *) dest_conv) = 0xffffffff;
                    dest_conv += sizeof(DWORD);
                }
            }
            else {
                copy_and_next(dest_ptr, color_d, sizeof(DWORD));
                if(dest_conv) {
                    *( (DWORD *) dest_conv)  = (*color_d & 0xff00ff00)      ; /* Alpha + green */
                    *( (DWORD *) dest_conv) |= (*color_d & 0x00ff0000) >> 16; /* Red */
                    *( (DWORD *) dest_conv) |= (*color_d & 0xff0000ff) << 16; /* Blue */
                    dest_conv += sizeof(DWORD);
                }
            }
        }

        if (DestFVF & WINED3DFVF_SPECULAR)
        {
            /* What's the color value in the feedback buffer? */
            const struct wined3d_stream_info_element *element = &stream_info->elements[WINED3D_FFP_SPECULAR];
            const DWORD *color_s = (const DWORD *)(element->data + i * element->stride);
            if (!(stream_info->use_map & (1 << WINED3D_FFP_SPECULAR)))
            {
                static BOOL warned = FALSE;

                if(!warned) {
                    ERR("No specular color in source, but destination has one\n");
                    warned = TRUE;
                }

                *( (DWORD *) dest_ptr) = 0xFF000000;
                dest_ptr += sizeof(DWORD);

                if(dest_conv) {
                    *( (DWORD *) dest_conv) = 0xFF000000;
                    dest_conv += sizeof(DWORD);
                }
            }
            else {
                copy_and_next(dest_ptr, color_s, sizeof(DWORD));
                if(dest_conv) {
                    *( (DWORD *) dest_conv)  = (*color_s & 0xff00ff00)      ; /* Alpha + green */
                    *( (DWORD *) dest_conv) |= (*color_s & 0x00ff0000) >> 16; /* Red */
                    *( (DWORD *) dest_conv) |= (*color_s & 0xff0000ff) << 16; /* Blue */
                    dest_conv += sizeof(DWORD);
                }
            }
        }

        for (tex_index = 0; tex_index < numTextures; tex_index++) {
            const struct wined3d_stream_info_element *element = &stream_info->elements[WINED3D_FFP_TEXCOORD0 + tex_index];
            const float *tex_coord = (const float *)(element->data + i * element->stride);
            if (!(stream_info->use_map & (1 << (WINED3D_FFP_TEXCOORD0 + tex_index))))
            {
                ERR("No source texture, but destination requests one\n");
                dest_ptr+=GET_TEXCOORD_SIZE_FROM_FVF(DestFVF, tex_index) * sizeof(float);
                if(dest_conv) dest_conv += GET_TEXCOORD_SIZE_FROM_FVF(DestFVF, tex_index) * sizeof(float);
            }
            else {
                copy_and_next(dest_ptr, tex_coord, GET_TEXCOORD_SIZE_FROM_FVF(DestFVF, tex_index) * sizeof(float));
                if(dest_conv) {
                    copy_and_next(dest_conv, tex_coord, GET_TEXCOORD_SIZE_FROM_FVF(DestFVF, tex_index) * sizeof(float));
                }
            }
        }
    }

    if (dest_conv)
    {
        ENTER_GL();

        GL_EXTCALL(glBindBufferARB(GL_ARRAY_BUFFER_ARB, dest->buffer_object));
        checkGLcall("glBindBufferARB(GL_ARRAY_BUFFER_ARB)");
        GL_EXTCALL(glBufferSubDataARB(GL_ARRAY_BUFFER_ARB, dwDestIndex * get_flexible_vertex_size(DestFVF),
                                      dwCount * get_flexible_vertex_size(DestFVF),
                                      dest_conv_addr));
        checkGLcall("glBufferSubDataARB(GL_ARRAY_BUFFER_ARB)");

        LEAVE_GL();

        HeapFree(GetProcessHeap(), 0, dest_conv_addr);
    }

    return WINED3D_OK;
}
#undef copy_and_next

/* Do not call while under the GL lock. */
static HRESULT WINAPI IWineD3DDeviceImpl_ProcessVertices(IWineD3DDevice *iface,
        UINT SrcStartIndex, UINT DestIndex, UINT VertexCount, struct wined3d_buffer *dst_buffer,
        struct wined3d_vertex_declaration *pVertexDecl, DWORD flags, DWORD DestFVF)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    struct wined3d_stream_info stream_info;
    const struct wined3d_gl_info *gl_info;
    struct wined3d_context *context;
    BOOL vbo = FALSE, streamWasUP = This->stateBlock->state.user_stream;
    HRESULT hr;

    TRACE("(%p)->(%d,%d,%d,%p,%p,%d\n", This, SrcStartIndex, DestIndex, VertexCount, dst_buffer, pVertexDecl, flags);

    if(pVertexDecl) {
        ERR("Output vertex declaration not implemented yet\n");
    }

    /* Need any context to write to the vbo. */
    context = context_acquire(This, NULL);
    gl_info = context->gl_info;

    /* ProcessVertices reads from vertex buffers, which have to be assigned. DrawPrimitive and DrawPrimitiveUP
     * control the streamIsUP flag, thus restore it afterwards.
     */
    This->stateBlock->state.user_stream = FALSE;
    device_stream_info_from_declaration(This, FALSE, &stream_info, &vbo);
    This->stateBlock->state.user_stream = streamWasUP;

    if(vbo || SrcStartIndex) {
        unsigned int i;
        /* ProcessVertices can't convert FROM a vbo, and vertex buffers used to source into ProcessVertices are
         * unlikely to ever be used for drawing. Release vbos in those buffers and fix up the stream_info structure
         *
         * Also get the start index in, but only loop over all elements if there's something to add at all.
         */
        for (i = 0; i < (sizeof(stream_info.elements) / sizeof(*stream_info.elements)); ++i)
        {
            struct wined3d_stream_info_element *e;

            if (!(stream_info.use_map & (1 << i))) continue;

            e = &stream_info.elements[i];
            if (e->buffer_object)
            {
                struct wined3d_buffer *vb = This->stateBlock->state.streams[e->stream_idx].buffer;
                e->buffer_object = 0;
                e->data = (BYTE *)((ULONG_PTR)e->data + (ULONG_PTR)buffer_get_sysmem(vb, gl_info));
                ENTER_GL();
                GL_EXTCALL(glDeleteBuffersARB(1, &vb->buffer_object));
                vb->buffer_object = 0;
                LEAVE_GL();
            }
            if (e->data) e->data += e->stride * SrcStartIndex;
        }
    }

    hr = process_vertices_strided(This, DestIndex, VertexCount,
            &stream_info, dst_buffer, flags, DestFVF);

    context_release(context);

    return hr;
}

/*****
 * Get / Set Texture Stage States
 * TODO: Verify against dx9 definitions
 *****/
static HRESULT WINAPI IWineD3DDeviceImpl_SetTextureStageState(IWineD3DDevice *iface, DWORD Stage, WINED3DTEXTURESTAGESTATETYPE Type, DWORD Value)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    const struct wined3d_gl_info *gl_info = &This->adapter->gl_info;
    DWORD oldValue;

    TRACE("(%p) : Stage=%d, Type=%s(%d), Value=%d\n", This, Stage, debug_d3dtexturestate(Type), Type, Value);

    if (Type > WINED3D_HIGHEST_TEXTURE_STATE)
    {
        WARN("Invalid Type %d passed.\n", Type);
        return WINED3D_OK;
    }

    if (Stage >= gl_info->limits.texture_stages)
    {
        WARN("Attempting to set stage %u which is higher than the max stage %u, ignoring.\n",
                Stage, gl_info->limits.texture_stages - 1);
        return WINED3D_OK;
    }

    oldValue = This->updateStateBlock->state.texture_states[Stage][Type];
    This->updateStateBlock->changed.textureState[Stage] |= 1 << Type;
    This->updateStateBlock->state.texture_states[Stage][Type] = Value;

    if (This->isRecordingState) {
        TRACE("Recording... not performing anything\n");
        return WINED3D_OK;
    }

    /* Checked after the assignments to allow proper stateblock recording */
    if(oldValue == Value) {
        TRACE("App is setting the old value over, nothing to do\n");
        return WINED3D_OK;
    }

    if (Stage > This->stateBlock->state.lowest_disabled_stage
            && This->StateTable[STATE_TEXTURESTAGE(0, Type)].representative
            == STATE_TEXTURESTAGE(0, WINED3DTSS_COLOROP))
    {
        /* Colorop change above lowest disabled stage? That won't change anything in the gl setup
         * Changes in other states are important on disabled stages too
         */
        return WINED3D_OK;
    }

    if(Type == WINED3DTSS_COLOROP) {
        unsigned int i;

        if(Value == WINED3DTOP_DISABLE && oldValue != WINED3DTOP_DISABLE) {
            /* Previously enabled stage disabled now. Make sure to dirtify all enabled stages above Stage,
             * they have to be disabled
             *
             * The current stage is dirtified below.
             */
            for (i = Stage + 1; i < This->stateBlock->state.lowest_disabled_stage; ++i)
            {
                TRACE("Additionally dirtifying stage %u\n", i);
                IWineD3DDeviceImpl_MarkStateDirty(This, STATE_TEXTURESTAGE(i, WINED3DTSS_COLOROP));
            }
            This->stateBlock->state.lowest_disabled_stage = Stage;
            TRACE("New lowest disabled: %u\n", Stage);
        } else if(Value != WINED3DTOP_DISABLE && oldValue == WINED3DTOP_DISABLE) {
            /* Previously disabled stage enabled. Stages above it may need enabling
             * stage must be lowest_disabled_stage here, if it's bigger success is returned above,
             * and stages below the lowest disabled stage can't be enabled(because they are enabled already).
             *
             * Again stage Stage doesn't need to be dirtified here, it is handled below.
             */

            for (i = Stage + 1; i < This->adapter->gl_info.limits.texture_stages; ++i)
            {
                if (This->updateStateBlock->state.texture_states[i][WINED3DTSS_COLOROP] == WINED3DTOP_DISABLE)
                    break;
                TRACE("Additionally dirtifying stage %u due to enable\n", i);
                IWineD3DDeviceImpl_MarkStateDirty(This, STATE_TEXTURESTAGE(i, WINED3DTSS_COLOROP));
            }
            This->stateBlock->state.lowest_disabled_stage = i;
            TRACE("New lowest disabled: %u\n", i);
        }
    }

    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_TEXTURESTAGE(Stage, Type));

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetTextureStageState(IWineD3DDevice *iface, DWORD Stage, WINED3DTEXTURESTAGESTATETYPE Type, DWORD *pValue)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    TRACE("iface %p, stage %u, state %s, value %p.\n",
            iface, Stage, debug_d3dtexturestate(Type), pValue);

    if (Type > WINED3D_HIGHEST_TEXTURE_STATE)
    {
        WARN("Invalid Type %d passed.\n", Type);
        return WINED3D_OK;
    }

    *pValue = This->updateStateBlock->state.texture_states[Stage][Type];
    TRACE("Returning %#x.\n", *pValue);

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetTexture(IWineD3DDevice *iface,
        DWORD stage, struct wined3d_texture *texture)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    const struct wined3d_gl_info *gl_info = &This->adapter->gl_info;
    struct wined3d_texture *prev;

    TRACE("iface %p, stage %u, texture %p.\n", iface, stage, texture);

    if (stage >= WINED3DVERTEXTEXTURESAMPLER0 && stage <= WINED3DVERTEXTEXTURESAMPLER3)
        stage -= (WINED3DVERTEXTEXTURESAMPLER0 - MAX_FRAGMENT_SAMPLERS);

    /* Windows accepts overflowing this array... we do not. */
    if (stage >= sizeof(This->stateBlock->state.textures) / sizeof(*This->stateBlock->state.textures))
    {
        WARN("Ignoring invalid stage %u.\n", stage);
        return WINED3D_OK;
    }

    /* SetTexture isn't allowed on textures in WINED3DPOOL_SCRATCH */
    if (texture && texture->resource.pool == WINED3DPOOL_SCRATCH)
    {
        WARN("Rejecting attempt to set scratch texture.\n");
        return WINED3DERR_INVALIDCALL;
    }

    This->updateStateBlock->changed.textures |= 1 << stage;

    prev = This->updateStateBlock->state.textures[stage];
    TRACE("Previous texture %p.\n", prev);

    if (texture == prev)
    {
        TRACE("App is setting the same texture again, nothing to do.\n");
        return WINED3D_OK;
    }

    TRACE("Setting new texture to %p.\n", texture);
    This->updateStateBlock->state.textures[stage] = texture;

    if (This->isRecordingState)
    {
        TRACE("Recording... not performing anything\n");

        if (texture) wined3d_texture_incref(texture);
        if (prev) wined3d_texture_decref(prev);

        return WINED3D_OK;
    }

    if (texture)
    {
        LONG bind_count = InterlockedIncrement(&texture->bind_count);

        wined3d_texture_incref(texture);

        if (!prev || texture->target != prev->target)
            IWineD3DDeviceImpl_MarkStateDirty(This, STATE_PIXELSHADER);

        if (!prev && stage < gl_info->limits.texture_stages)
        {
            /* The source arguments for color and alpha ops have different
             * meanings when a NULL texture is bound, so the COLOROP and
             * ALPHAOP have to be dirtified. */
            IWineD3DDeviceImpl_MarkStateDirty(This, STATE_TEXTURESTAGE(stage, WINED3DTSS_COLOROP));
            IWineD3DDeviceImpl_MarkStateDirty(This, STATE_TEXTURESTAGE(stage, WINED3DTSS_ALPHAOP));
        }

        if (bind_count == 1)
            texture->sampler = stage;
    }

    if (prev)
    {
        LONG bind_count = InterlockedDecrement(&prev->bind_count);

        wined3d_texture_decref(prev);

        if (!texture && stage < gl_info->limits.texture_stages)
        {
            IWineD3DDeviceImpl_MarkStateDirty(This, STATE_TEXTURESTAGE(stage, WINED3DTSS_COLOROP));
            IWineD3DDeviceImpl_MarkStateDirty(This, STATE_TEXTURESTAGE(stage, WINED3DTSS_ALPHAOP));
        }

        if (bind_count && prev->sampler == stage)
        {
            unsigned int i;

            /* Search for other stages the texture is bound to. Shouldn't
             * happen if applications bind textures to a single stage only. */
            TRACE("Searching for other stages the texture is bound to.\n");
            for (i = 0; i < MAX_COMBINED_SAMPLERS; ++i)
            {
                if (This->updateStateBlock->state.textures[i] == prev)
                {
                    TRACE("Texture is also bound to stage %u.\n", i);
                    prev->sampler = i;
                    break;
                }
            }
        }
    }

    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_SAMPLER(stage));

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetTexture(IWineD3DDevice *iface,
        DWORD stage, struct wined3d_texture **texture)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    TRACE("iface %p, stage %u, texture %p.\n", iface, stage, texture);

    if (stage >= WINED3DVERTEXTEXTURESAMPLER0 && stage <= WINED3DVERTEXTEXTURESAMPLER3)
        stage -= (WINED3DVERTEXTEXTURESAMPLER0 - MAX_FRAGMENT_SAMPLERS);

    if (stage >= sizeof(This->stateBlock->state.textures) / sizeof(*This->stateBlock->state.textures))
    {
        WARN("Current stage overflows textures array (stage %u).\n", stage);
        return WINED3D_OK; /* Windows accepts overflowing this array ... we do not. */
    }

    *texture = This->stateBlock->state.textures[stage];
    if (*texture)
        wined3d_texture_incref(*texture);

    TRACE("Returning %p.\n", *texture);

    return WINED3D_OK;
}

/*****
 * Get Back Buffer
 *****/
static HRESULT WINAPI IWineD3DDeviceImpl_GetBackBuffer(IWineD3DDevice *iface, UINT swapchain_idx,
        UINT backbuffer_idx, WINED3DBACKBUFFER_TYPE backbuffer_type, IWineD3DSurface **backbuffer)
{
    IWineD3DSwapChain *swapchain;
    HRESULT hr;

    TRACE("iface %p, swapchain_idx %u, backbuffer_idx %u, backbuffer_type %#x, backbuffer %p.\n",
            iface, swapchain_idx, backbuffer_idx, backbuffer_type, backbuffer);

    hr = IWineD3DDeviceImpl_GetSwapChain(iface, swapchain_idx, &swapchain);
    if (FAILED(hr))
    {
        WARN("Failed to get swapchain %u, hr %#x.\n", swapchain_idx, hr);
        return hr;
    }

    hr = IWineD3DSwapChain_GetBackBuffer(swapchain, backbuffer_idx, backbuffer_type, backbuffer);
    IWineD3DSwapChain_Release(swapchain);
    if (FAILED(hr))
    {
        WARN("Failed to get backbuffer %u, hr %#x.\n", backbuffer_idx, hr);
        return hr;
    }

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetDeviceCaps(IWineD3DDevice *iface, WINED3DCAPS *caps)
{
    IWineD3DDeviceImpl *device = (IWineD3DDeviceImpl *)iface;

    TRACE("iface %p, caps %p.\n", iface, caps);

    return wined3d_get_device_caps(device->wined3d, device->adapter->ordinal, device->devType, caps);
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetDisplayMode(IWineD3DDevice *iface, UINT iSwapChain, WINED3DDISPLAYMODE* pMode) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DSwapChain *swapChain;
    HRESULT hr;

    if(iSwapChain > 0) {
        hr = IWineD3DDeviceImpl_GetSwapChain(iface, iSwapChain, &swapChain);
        if (hr == WINED3D_OK) {
            hr = IWineD3DSwapChain_GetDisplayMode(swapChain, pMode);
            IWineD3DSwapChain_Release(swapChain);
        } else {
            FIXME("(%p) Error getting display mode\n", This);
        }
    } else {
        /* Don't read the real display mode,
           but return the stored mode instead. X11 can't change the color
           depth, and some apps are pretty angry if they SetDisplayMode from
           24 to 16 bpp and find out that GetDisplayMode still returns 24 bpp

           Also don't relay to the swapchain because with ddraw it's possible
           that there isn't a swapchain at all */
        pMode->Width = This->ddraw_width;
        pMode->Height = This->ddraw_height;
        pMode->Format = This->ddraw_format;
        pMode->RefreshRate = 0;
        hr = WINED3D_OK;
    }

    return hr;
}

/*****
 * Stateblock related functions
 *****/

static HRESULT WINAPI IWineD3DDeviceImpl_BeginStateBlock(IWineD3DDevice *iface)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    struct wined3d_stateblock *stateblock;
    HRESULT hr;

    TRACE("(%p)\n", This);

    if (This->isRecordingState) return WINED3DERR_INVALIDCALL;

    hr = IWineD3DDeviceImpl_CreateStateBlock(iface, WINED3DSBT_RECORDED, &stateblock);
    if (FAILED(hr)) return hr;

    wined3d_stateblock_decref(This->updateStateBlock);
    This->updateStateBlock = stateblock;
    This->isRecordingState = TRUE;

    TRACE("(%p) recording stateblock %p\n", This, stateblock);

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_EndStateBlock(IWineD3DDevice *iface,
        struct wined3d_stateblock **stateblock)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    struct wined3d_stateblock *object = This->updateStateBlock;

    TRACE("iface %p, stateblock %p.\n", iface, stateblock);

    if (!This->isRecordingState) {
        WARN("(%p) not recording! returning error\n", This);
        *stateblock = NULL;
        return WINED3DERR_INVALIDCALL;
    }

    stateblock_init_contained_states(object);

    *stateblock = object;
    This->isRecordingState = FALSE;
    This->updateStateBlock = This->stateBlock;
    wined3d_stateblock_incref(This->updateStateBlock);

    TRACE("Returning stateblock %p.\n", *stateblock);

    return WINED3D_OK;
}

/*****
 * Scene related functions
 *****/
static HRESULT WINAPI IWineD3DDeviceImpl_BeginScene(IWineD3DDevice *iface) {
    /* At the moment we have no need for any functionality at the beginning
       of a scene                                                          */
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p)\n", This);

    if(This->inScene) {
        TRACE("Already in Scene, returning WINED3DERR_INVALIDCALL\n");
        return WINED3DERR_INVALIDCALL;
    }
    This->inScene = TRUE;
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_EndScene(IWineD3DDevice *iface)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    struct wined3d_context *context;

    TRACE("(%p)\n", This);

    if(!This->inScene) {
        TRACE("Not in scene, returning WINED3DERR_INVALIDCALL\n");
        return WINED3DERR_INVALIDCALL;
    }

    context = context_acquire(This, NULL);
    /* We only have to do this if we need to read the, swapbuffers performs a flush for us */
    wglFlush();
    /* No checkGLcall here to avoid locking the lock just for checking a call that hardly ever
     * fails. */
    context_release(context);

    This->inScene = FALSE;
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_Present(IWineD3DDevice *iface, const RECT *src_rect,
        const RECT *dst_rect, HWND dst_window_override, const RGNDATA *dirty_region)
{
    IWineD3DDeviceImpl *device = (IWineD3DDeviceImpl *)iface;
    UINT i;

    TRACE("iface %p, src_rect %s, dst_rect %s, dst_window_override %p, dirty_region %p.\n",
            iface, wine_dbgstr_rect(src_rect), wine_dbgstr_rect(dst_rect),
            dst_window_override, dirty_region);

    for (i = 0; i < device->swapchain_count; ++i)
    {
        IWineD3DSwapChain_Present((IWineD3DSwapChain *)device->swapchains[i],
                src_rect, dst_rect, dst_window_override, dirty_region, 0);
    }

    return WINED3D_OK;
}

/* Do not call while under the GL lock. */
static HRESULT WINAPI IWineD3DDeviceImpl_Clear(IWineD3DDevice *iface, DWORD rect_count,
        const RECT *rects, DWORD flags, WINED3DCOLOR color, float depth, DWORD stencil)
{
    const WINED3DCOLORVALUE c = {D3DCOLOR_R(color), D3DCOLOR_G(color), D3DCOLOR_B(color), D3DCOLOR_A(color)};
    IWineD3DDeviceImpl *device = (IWineD3DDeviceImpl *)iface;
    RECT draw_rect;

    TRACE("iface %p, rect_count %u, rects %p, flags %#x, color 0x%08x, depth %.8e, stencil %u.\n",
            iface, rect_count, rects, flags, color, depth, stencil);

    if (flags & (WINED3DCLEAR_ZBUFFER | WINED3DCLEAR_STENCIL))
    {
        IWineD3DSurfaceImpl *ds = device->depth_stencil;
        if (!ds)
        {
            WARN("Clearing depth and/or stencil without a depth stencil buffer attached, returning WINED3DERR_INVALIDCALL\n");
            /* TODO: What about depth stencil buffers without stencil bits? */
            return WINED3DERR_INVALIDCALL;
        }
        else if (flags & WINED3DCLEAR_TARGET)
        {
            if(ds->resource.width < device->render_targets[0]->resource.width ||
               ds->resource.height < device->render_targets[0]->resource.height)
            {
                WARN("Silently ignoring depth and target clear with mismatching sizes\n");
                return WINED3D_OK;
            }
        }
    }

    device_get_draw_rect(device, &draw_rect);

    return device_clear_render_targets(device, device->adapter->gl_info.limits.buffers,
            device->render_targets, device->depth_stencil, rect_count, rects,
            &draw_rect, flags, &c, depth, stencil);
}

/*****
 * Drawing functions
 *****/

static void WINAPI IWineD3DDeviceImpl_SetPrimitiveType(IWineD3DDevice *iface,
        WINED3DPRIMITIVETYPE primitive_type)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    TRACE("iface %p, primitive_type %s\n", iface, debug_d3dprimitivetype(primitive_type));

    This->updateStateBlock->changed.primitive_type = TRUE;
    This->updateStateBlock->state.gl_primitive_type = gl_primitive_type_from_d3d(primitive_type);
}

static void WINAPI IWineD3DDeviceImpl_GetPrimitiveType(IWineD3DDevice *iface,
        WINED3DPRIMITIVETYPE *primitive_type)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    TRACE("iface %p, primitive_type %p\n", iface, primitive_type);

    *primitive_type = d3d_primitive_type_from_gl(This->stateBlock->state.gl_primitive_type);

    TRACE("Returning %s\n", debug_d3dprimitivetype(*primitive_type));
}

static HRESULT WINAPI IWineD3DDeviceImpl_DrawPrimitive(IWineD3DDevice *iface, UINT StartVertex, UINT vertex_count)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    TRACE("(%p) : start %u, count %u\n", This, StartVertex, vertex_count);

    if (!This->stateBlock->state.vertex_declaration)
    {
        WARN("(%p) : Called without a valid vertex declaration set\n", This);
        return WINED3DERR_INVALIDCALL;
    }

    /* The index buffer is not needed here, but restore it, otherwise it is hell to keep track of */
    if (This->stateBlock->state.user_stream)
    {
        IWineD3DDeviceImpl_MarkStateDirty(This, STATE_INDEXBUFFER);
        This->stateBlock->state.user_stream = FALSE;
    }

    if (This->stateBlock->state.load_base_vertex_index)
    {
        This->stateBlock->state.load_base_vertex_index = 0;
        IWineD3DDeviceImpl_MarkStateDirty(This, STATE_STREAMSRC);
    }
    /* Account for the loading offset due to index buffers. Instead of reloading all sources correct it with the startvertex parameter */
    drawPrimitive(This, vertex_count, StartVertex /* start_idx */, 0 /* indxSize */, NULL /* indxData */);
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_DrawIndexedPrimitive(IWineD3DDevice *iface, UINT startIndex, UINT index_count)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    struct wined3d_buffer *index_buffer;
    UINT                 idxStride = 2;
    GLuint vbo;

    index_buffer = This->stateBlock->state.index_buffer;
    if (!index_buffer)
    {
        /* D3D9 returns D3DERR_INVALIDCALL when DrawIndexedPrimitive is called
         * without an index buffer set. (The first time at least...)
         * D3D8 simply dies, but I doubt it can do much harm to return
         * D3DERR_INVALIDCALL there as well. */
        WARN("(%p) : Called without a valid index buffer set, returning WINED3DERR_INVALIDCALL\n", This);
        return WINED3DERR_INVALIDCALL;
    }

    if (!This->stateBlock->state.vertex_declaration)
    {
        WARN("(%p) : Called without a valid vertex declaration set\n", This);
        return WINED3DERR_INVALIDCALL;
    }

    if (This->stateBlock->state.user_stream)
    {
        IWineD3DDeviceImpl_MarkStateDirty(This, STATE_INDEXBUFFER);
        This->stateBlock->state.user_stream = FALSE;
    }
    vbo = index_buffer->buffer_object;

    TRACE("(%p) : startIndex %u, index count %u.\n", This, startIndex, index_count);

    if (This->stateBlock->state.index_format == WINED3DFMT_R16_UINT)
        idxStride = 2;
    else
        idxStride = 4;

    if (This->stateBlock->state.load_base_vertex_index != This->stateBlock->state.base_vertex_index)
    {
        This->stateBlock->state.load_base_vertex_index = This->stateBlock->state.base_vertex_index;
        IWineD3DDeviceImpl_MarkStateDirty(This, STATE_STREAMSRC);
    }

    drawPrimitive(This, index_count, startIndex, idxStride,
            vbo ? NULL : index_buffer->resource.allocatedMemory);

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_DrawPrimitiveUP(IWineD3DDevice *iface, UINT vertex_count,
        const void *pVertexStreamZeroData, UINT VertexStreamZeroStride)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    struct wined3d_stream_state *stream;
    struct wined3d_buffer *vb;

    TRACE("(%p) : vertex count %u, pVtxData %p, stride %u\n",
            This, vertex_count, pVertexStreamZeroData, VertexStreamZeroStride);

    if (!This->stateBlock->state.vertex_declaration)
    {
        WARN("(%p) : Called without a valid vertex declaration set\n", This);
        return WINED3DERR_INVALIDCALL;
    }

    /* Note in the following, it's not this type, but that's the purpose of streamIsUP */
    stream = &This->stateBlock->state.streams[0];
    vb = stream->buffer;
    stream->buffer = (struct wined3d_buffer *)pVertexStreamZeroData;
    if (vb)
        wined3d_buffer_decref(vb);
    stream->offset = 0;
    stream->stride = VertexStreamZeroStride;
    This->stateBlock->state.user_stream = TRUE;
    This->stateBlock->state.load_base_vertex_index = 0;

    /* TODO: Only mark dirty if drawing from a different UP address */
    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_STREAMSRC);

    drawPrimitive(This, vertex_count, 0 /* start_idx */, 0 /* indxSize*/, NULL /* indxData */);

    /* MSDN specifies stream zero settings must be set to NULL */
    stream->buffer = NULL;
    stream->stride = 0;

    /* stream zero settings set to null at end, as per the msdn. No need to mark dirty here, the app has to set
     * the new stream sources or use UP drawing again
     */
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_DrawIndexedPrimitiveUP(IWineD3DDevice *iface,
        UINT index_count, const void *pIndexData, enum wined3d_format_id IndexDataFormat,
        const void *pVertexStreamZeroData, UINT VertexStreamZeroStride)
{
    int                 idxStride;
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    struct wined3d_stream_state *stream;
    struct wined3d_buffer *vb, *ib;

    TRACE("(%p) : index count %u, pidxdata %p, IdxFmt %u, pVtxdata %p, stride=%u.\n",
            This, index_count, pIndexData, IndexDataFormat, pVertexStreamZeroData, VertexStreamZeroStride);

    if (!This->stateBlock->state.vertex_declaration)
    {
        WARN("(%p) : Called without a valid vertex declaration set\n", This);
        return WINED3DERR_INVALIDCALL;
    }

    if (IndexDataFormat == WINED3DFMT_R16_UINT) {
        idxStride = 2;
    } else {
        idxStride = 4;
    }

    stream = &This->stateBlock->state.streams[0];
    vb = stream->buffer;
    stream->buffer = (struct wined3d_buffer *)pVertexStreamZeroData;
    if (vb)
        wined3d_buffer_decref(vb);
    stream->offset = 0;
    stream->stride = VertexStreamZeroStride;
    This->stateBlock->state.user_stream = TRUE;

    /* Set to 0 as per msdn. Do it now due to the stream source loading during drawPrimitive */
    This->stateBlock->state.base_vertex_index = 0;
    This->stateBlock->state.load_base_vertex_index = 0;
    /* Mark the state dirty until we have nicer tracking of the stream source pointers */
    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_VDECL);
    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_INDEXBUFFER);

    drawPrimitive(This, index_count, 0 /* start_idx */, idxStride, pIndexData);

    /* MSDN specifies stream zero settings and index buffer must be set to NULL */
    stream->buffer = NULL;
    stream->stride = 0;
    ib = This->stateBlock->state.index_buffer;
    if (ib)
    {
        wined3d_buffer_decref(ib);
        This->stateBlock->state.index_buffer = NULL;
    }
    /* No need to mark the stream source state dirty here. Either the app calls UP drawing again, or it has to call
     * SetStreamSource to specify a vertex buffer
     */

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_DrawPrimitiveStrided(IWineD3DDevice *iface,
        UINT vertex_count, const WineDirect3DVertexStridedData *DrawPrimStrideData)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *) iface;

    /* Mark the state dirty until we have nicer tracking
     * its fine to change baseVertexIndex because that call is only called by ddraw which does not need
     * that value.
     */
    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_VDECL);
    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_INDEXBUFFER);
    This->stateBlock->state.base_vertex_index = 0;
    This->up_strided = DrawPrimStrideData;
    drawPrimitive(This, vertex_count, 0, 0, NULL);
    This->up_strided = NULL;
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_DrawIndexedPrimitiveStrided(IWineD3DDevice *iface,
        UINT vertex_count, const WineDirect3DVertexStridedData *DrawPrimStrideData,
        UINT NumVertices, const void *pIndexData, enum wined3d_format_id IndexDataFormat)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *) iface;
    DWORD idxSize = (IndexDataFormat == WINED3DFMT_R32_UINT ? 4 : 2);

    /* Mark the state dirty until we have nicer tracking
     * its fine to change baseVertexIndex because that call is only called by ddraw which does not need
     * that value.
     */
    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_VDECL);
    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_INDEXBUFFER);
    This->stateBlock->state.user_stream = TRUE;
    This->stateBlock->state.base_vertex_index = 0;
    This->up_strided = DrawPrimStrideData;
    drawPrimitive(This, vertex_count, 0 /* start_idx */, idxSize, pIndexData);
    This->up_strided = NULL;
    return WINED3D_OK;
}

/* This is a helper function for UpdateTexture, there is no UpdateVolume method in D3D. */
static HRESULT IWineD3DDeviceImpl_UpdateVolume(IWineD3DDevice *iface,
        IWineD3DVolume *pSourceVolume, IWineD3DVolume *pDestinationVolume)
{
    WINED3DLOCKED_BOX src;
    WINED3DLOCKED_BOX dst;
    HRESULT hr;

    TRACE("iface %p, src_volume %p, dst_volume %p.\n",
            iface, pSourceVolume, pDestinationVolume);

    /* TODO: Implement direct loading into the gl volume instead of using memcpy and
     * dirtification to improve loading performance.
     */
    hr = IWineD3DVolume_Map(pSourceVolume, &src, NULL, WINED3DLOCK_READONLY);
    if (FAILED(hr)) return hr;
    hr = IWineD3DVolume_Map(pDestinationVolume, &dst, NULL, WINED3DLOCK_DISCARD);
    if (FAILED(hr))
    {
        IWineD3DVolume_Unmap(pSourceVolume);
        return hr;
    }

    memcpy(dst.pBits, src.pBits, ((IWineD3DVolumeImpl *) pDestinationVolume)->resource.size);

    hr = IWineD3DVolume_Unmap(pDestinationVolume);
    if (FAILED(hr))
        IWineD3DVolume_Unmap(pSourceVolume);
    else
        hr = IWineD3DVolume_Unmap(pSourceVolume);

    return hr;
}

static HRESULT WINAPI IWineD3DDeviceImpl_UpdateTexture(IWineD3DDevice *iface,
        struct wined3d_texture *src_texture, struct wined3d_texture *dst_texture)
{
    unsigned int level_count, i;
    WINED3DRESOURCETYPE type;
    HRESULT hr;

    TRACE("iface %p, src_texture %p, dst_texture %p.\n", iface, src_texture, dst_texture);

    /* Verify that the source and destination textures are non-NULL. */
    if (!src_texture || !dst_texture)
    {
        WARN("Source and destination textures must be non-NULL, returning WINED3DERR_INVALIDCALL.\n");
        return WINED3DERR_INVALIDCALL;
    }

    if (src_texture == dst_texture)
    {
        WARN("Source and destination are the same object, returning WINED3DERR_INVALIDCALL.\n");
        return WINED3DERR_INVALIDCALL;
    }

    /* Verify that the source and destination textures are the same type. */
    type = wined3d_texture_get_type(src_texture);
    if (wined3d_texture_get_type(dst_texture) != type)
    {
        WARN("Source and destination have different types, returning WINED3DERR_INVALIDCALL.\n");
        return WINED3DERR_INVALIDCALL;
    }

    /* Check that both textures have the identical numbers of levels. */
    level_count = wined3d_texture_get_level_count(src_texture);
    if (wined3d_texture_get_level_count(dst_texture) != level_count)
    {
        WARN("Source and destination have different level counts, returning WINED3DERR_INVALIDCALL.\n");
        return WINED3DERR_INVALIDCALL;
    }

    /* Make sure that the destination texture is loaded. */
    dst_texture->texture_ops->texture_preload(dst_texture, SRGB_RGB);

    /* Update every surface level of the texture. */
    switch (type)
    {
        case WINED3DRTYPE_TEXTURE:
        {
            IWineD3DSurface *src_surface;
            IWineD3DSurface *dst_surface;

            for (i = 0; i < level_count; ++i)
            {
                src_surface = (IWineD3DSurface *)surface_from_resource(wined3d_texture_get_sub_resource(
                        src_texture, i));
                dst_surface = (IWineD3DSurface *)surface_from_resource(wined3d_texture_get_sub_resource(
                        dst_texture, i));
                hr = IWineD3DDevice_UpdateSurface(iface, src_surface, NULL, dst_surface, NULL);
                if (FAILED(hr))
                {
                    WARN("IWineD3DDevice_UpdateSurface failed, hr %#x.\n", hr);
                    return hr;
                }
            }
            break;
        }

        case WINED3DRTYPE_CUBETEXTURE:
        {
            IWineD3DSurface *src_surface;
            IWineD3DSurface *dst_surface;

            for (i = 0; i < level_count * 6; ++i)
            {
                src_surface = (IWineD3DSurface *)surface_from_resource(wined3d_texture_get_sub_resource(
                        src_texture, i));
                dst_surface = (IWineD3DSurface *)surface_from_resource(wined3d_texture_get_sub_resource(
                        dst_texture, i));
                hr = IWineD3DDevice_UpdateSurface(iface, src_surface, NULL, dst_surface, NULL);
                if (FAILED(hr))
                {
                    WARN("IWineD3DDevice_UpdateSurface failed, hr %#x.\n", hr);
                    return hr;
                }
            }
            break;
        }

        case WINED3DRTYPE_VOLUMETEXTURE:
        {
            IWineD3DVolume *src_volume;
            IWineD3DVolume *dst_volume;

            for (i = 0; i < level_count; ++i)
            {
                src_volume = (IWineD3DVolume *)volume_from_resource(wined3d_texture_get_sub_resource(src_texture, i));
                dst_volume = (IWineD3DVolume *)volume_from_resource(wined3d_texture_get_sub_resource(dst_texture, i));
                hr = IWineD3DDeviceImpl_UpdateVolume(iface, src_volume, dst_volume);
                if (FAILED(hr))
                {
                    WARN("IWineD3DDeviceImpl_UpdateVolume failed, hr %#x.\n", hr);
                    return hr;
                }
            }
            break;
        }

        default:
            FIXME("Unsupported texture type %#x.\n", type);
            return WINED3DERR_INVALIDCALL;
    }

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetFrontBufferData(IWineD3DDevice *iface,
        UINT swapchain_idx, IWineD3DSurface *dst_surface)
{
    IWineD3DSwapChain *swapchain;
    HRESULT hr;

    TRACE("iface %p, swapchain_idx %u, dst_surface %p.\n", iface, swapchain_idx, dst_surface);

    hr = IWineD3DDeviceImpl_GetSwapChain(iface, swapchain_idx, &swapchain);
    if (FAILED(hr)) return hr;

    hr = IWineD3DSwapChain_GetFrontBufferData(swapchain, dst_surface);
    IWineD3DSwapChain_Release(swapchain);

    return hr;
}

static HRESULT WINAPI IWineD3DDeviceImpl_ValidateDevice(IWineD3DDevice *iface, DWORD *pNumPasses)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    const struct wined3d_state *state = &This->stateBlock->state;
    struct wined3d_texture *texture;
    DWORD i;

    TRACE("(%p) : %p\n", This, pNumPasses);

    for (i = 0; i < MAX_COMBINED_SAMPLERS; ++i)
    {
        if (state->sampler_states[i][WINED3DSAMP_MINFILTER] == WINED3DTEXF_NONE)
        {
            WARN("Sampler state %u has minfilter D3DTEXF_NONE, returning D3DERR_UNSUPPORTEDTEXTUREFILTER\n", i);
            return WINED3DERR_UNSUPPORTEDTEXTUREFILTER;
        }
        if (state->sampler_states[i][WINED3DSAMP_MAGFILTER] == WINED3DTEXF_NONE)
        {
            WARN("Sampler state %u has magfilter D3DTEXF_NONE, returning D3DERR_UNSUPPORTEDTEXTUREFILTER\n", i);
            return WINED3DERR_UNSUPPORTEDTEXTUREFILTER;
        }

        texture = state->textures[i];
        if (!texture || texture->resource.format->flags & WINED3DFMT_FLAG_FILTERING) continue;

        if (state->sampler_states[i][WINED3DSAMP_MAGFILTER] != WINED3DTEXF_POINT)
        {
            WARN("Non-filterable texture and mag filter enabled on samper %u, returning E_FAIL\n", i);
            return E_FAIL;
        }
        if (state->sampler_states[i][WINED3DSAMP_MINFILTER] != WINED3DTEXF_POINT)
        {
            WARN("Non-filterable texture and min filter enabled on samper %u, returning E_FAIL\n", i);
            return E_FAIL;
        }
        if (state->sampler_states[i][WINED3DSAMP_MIPFILTER] != WINED3DTEXF_NONE
                && state->sampler_states[i][WINED3DSAMP_MIPFILTER] != WINED3DTEXF_POINT)
        {
            WARN("Non-filterable texture and mip filter enabled on samper %u, returning E_FAIL\n", i);
            return E_FAIL;
        }
    }

    if (state->render_states[WINED3DRS_ZENABLE] || state->render_states[WINED3DRS_ZWRITEENABLE] ||
        state->render_states[WINED3DRS_STENCILENABLE])
    {
        IWineD3DSurfaceImpl *ds = This->depth_stencil;
        IWineD3DSurfaceImpl *target = This->render_targets[0];

        if(ds && target
                && (ds->resource.width < target->resource.width || ds->resource.height < target->resource.height))
        {
            WARN("Depth stencil is smaller than the color buffer, returning D3DERR_CONFLICTINGRENDERSTATE\n");
            return WINED3DERR_CONFLICTINGRENDERSTATE;
        }
    }

    /* return a sensible default */
    *pNumPasses = 1;

    TRACE("returning D3D_OK\n");
    return WINED3D_OK;
}

static void dirtify_p8_texture_samplers(IWineD3DDeviceImpl *device)
{
    int i;

    for (i = 0; i < MAX_COMBINED_SAMPLERS; ++i)
    {
        struct wined3d_texture *texture = device->stateBlock->state.textures[i];
        if (texture && (texture->resource.format->id == WINED3DFMT_P8_UINT
                || texture->resource.format->id == WINED3DFMT_P8_UINT_A8_UNORM))
        {
            IWineD3DDeviceImpl_MarkStateDirty(device, STATE_SAMPLER(i));
        }
    }
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetPaletteEntries(IWineD3DDevice *iface,
        UINT PaletteNumber, const PALETTEENTRY *pEntries)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    int j;
    UINT NewSize;
    PALETTEENTRY **palettes;

    TRACE("(%p) : PaletteNumber %u\n", This, PaletteNumber);

    if (PaletteNumber >= MAX_PALETTES) {
        ERR("(%p) : (%u) Out of range 0-%u, returning Invalid Call\n", This, PaletteNumber, MAX_PALETTES);
        return WINED3DERR_INVALIDCALL;
    }

    if (PaletteNumber >= This->palette_count)
    {
        NewSize = This->palette_count;
        do {
           NewSize *= 2;
        } while(PaletteNumber >= NewSize);
        palettes = HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, This->palettes, sizeof(PALETTEENTRY*) * NewSize);
        if (!palettes) {
            ERR("Out of memory!\n");
            return E_OUTOFMEMORY;
        }
        This->palettes = palettes;
        This->palette_count = NewSize;
    }

    if (!This->palettes[PaletteNumber]) {
        This->palettes[PaletteNumber] = HeapAlloc(GetProcessHeap(),  0, sizeof(PALETTEENTRY) * 256);
        if (!This->palettes[PaletteNumber]) {
            ERR("Out of memory!\n");
            return E_OUTOFMEMORY;
        }
    }

    for (j = 0; j < 256; ++j) {
        This->palettes[PaletteNumber][j].peRed   = pEntries[j].peRed;
        This->palettes[PaletteNumber][j].peGreen = pEntries[j].peGreen;
        This->palettes[PaletteNumber][j].peBlue  = pEntries[j].peBlue;
        This->palettes[PaletteNumber][j].peFlags = pEntries[j].peFlags;
    }
    if (PaletteNumber == This->currentPalette) dirtify_p8_texture_samplers(This);
    TRACE("(%p) : returning\n", This);
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetPaletteEntries(IWineD3DDevice *iface,
        UINT PaletteNumber, PALETTEENTRY *pEntries)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    int j;
    TRACE("(%p) : PaletteNumber %u\n", This, PaletteNumber);

    if (PaletteNumber >= This->palette_count || !This->palettes[PaletteNumber])
    {
        /* What happens in such situation isn't documented; Native seems to silently abort
           on such conditions. Return Invalid Call. */
        ERR("(%p) : (%u) Nonexistent palette. Palette count %u.\n", This, PaletteNumber, This->palette_count);
        return WINED3DERR_INVALIDCALL;
    }
    for (j = 0; j < 256; ++j) {
        pEntries[j].peRed   = This->palettes[PaletteNumber][j].peRed;
        pEntries[j].peGreen = This->palettes[PaletteNumber][j].peGreen;
        pEntries[j].peBlue  = This->palettes[PaletteNumber][j].peBlue;
        pEntries[j].peFlags = This->palettes[PaletteNumber][j].peFlags;
    }
    TRACE("(%p) : returning\n", This);
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetCurrentTexturePalette(IWineD3DDevice *iface, UINT PaletteNumber)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    TRACE("(%p) : PaletteNumber %u\n", This, PaletteNumber);
    /* Native appears to silently abort on attempt to make an uninitialized palette current and render.
       (tested with reference rasterizer). Return Invalid Call. */
    if (PaletteNumber >= This->palette_count || !This->palettes[PaletteNumber])
    {
        ERR("(%p) : (%u) Nonexistent palette. Palette count %u.\n", This, PaletteNumber, This->palette_count);
        return WINED3DERR_INVALIDCALL;
    }
    /*TODO: stateblocks */
    if (This->currentPalette != PaletteNumber) {
        This->currentPalette = PaletteNumber;
        dirtify_p8_texture_samplers(This);
    }
    TRACE("(%p) : returning\n", This);
    return WINED3D_OK;
}

static HRESULT  WINAPI  IWineD3DDeviceImpl_GetCurrentTexturePalette(IWineD3DDevice *iface, UINT* PaletteNumber) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;

    if (!PaletteNumber)
    {
        WARN("(%p) : returning Invalid Call\n", This);
        return WINED3DERR_INVALIDCALL;
    }
    /*TODO: stateblocks */
    *PaletteNumber = This->currentPalette;
    TRACE("(%p) : returning  %u\n", This, *PaletteNumber);
    return WINED3D_OK;
}

static HRESULT  WINAPI  IWineD3DDeviceImpl_SetSoftwareVertexProcessing(IWineD3DDevice *iface, BOOL bSoftware) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    static BOOL warned;
    if (!warned)
    {
        FIXME("(%p) : stub\n", This);
        warned = TRUE;
    }

    This->softwareVertexProcessing = bSoftware;
    return WINED3D_OK;
}


static BOOL     WINAPI  IWineD3DDeviceImpl_GetSoftwareVertexProcessing(IWineD3DDevice *iface) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    static BOOL warned;
    if (!warned)
    {
        FIXME("(%p) : stub\n", This);
        warned = TRUE;
    }
    return This->softwareVertexProcessing;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetRasterStatus(IWineD3DDevice *iface,
        UINT swapchain_idx, WINED3DRASTER_STATUS *raster_status)
{
    IWineD3DSwapChain *swapchain;
    HRESULT hr;

    TRACE("iface %p, swapchain_idx %u, raster_status %p.\n",
            iface, swapchain_idx, raster_status);

    hr = IWineD3DDeviceImpl_GetSwapChain(iface, swapchain_idx, &swapchain);
    if (FAILED(hr))
    {
        WARN("Failed to get swapchain %u, hr %#x.\n", swapchain_idx, hr);
        return hr;
    }

    hr = IWineD3DSwapChain_GetRasterStatus(swapchain, raster_status);
    IWineD3DSwapChain_Release(swapchain);
    if (FAILED(hr))
    {
        WARN("Failed to get raster status, hr %#x.\n", hr);
        return hr;
    }

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetNPatchMode(IWineD3DDevice *iface, float nSegments)
{
    static BOOL warned;
    if(nSegments != 0.0f) {
        if (!warned)
        {
            FIXME("iface %p, nSegments %.8e stub!\n", iface, nSegments);
            warned = TRUE;
        }
    }
    return WINED3D_OK;
}

static float WINAPI IWineD3DDeviceImpl_GetNPatchMode(IWineD3DDevice *iface)
{
    static BOOL warned;
    if (!warned)
    {
        FIXME("iface %p stub!\n", iface);
        warned = TRUE;
    }
    return 0.0f;
}

static HRESULT WINAPI IWineD3DDeviceImpl_UpdateSurface(IWineD3DDevice *iface,
        IWineD3DSurface *src_surface, const RECT *src_rect,
        IWineD3DSurface *dst_surface, const POINT *dst_point)
{
    IWineD3DSurfaceImpl *src_impl = (IWineD3DSurfaceImpl *)src_surface;
    IWineD3DSurfaceImpl *dst_impl = (IWineD3DSurfaceImpl *)dst_surface;
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    const struct wined3d_format *src_format;
    const struct wined3d_format *dst_format;
    const struct wined3d_gl_info *gl_info;
    struct wined3d_context *context;
    const unsigned char *data;
    UINT update_w, update_h;
    CONVERT_TYPES convert;
    UINT src_w, src_h;
    UINT dst_x, dst_y;
    DWORD sampler;
    struct wined3d_format format;

    TRACE("iface %p, src_surface %p, src_rect %s, dst_surface %p, dst_point %s.\n",
            iface, src_surface, wine_dbgstr_rect(src_rect),
            dst_surface, wine_dbgstr_point(dst_point));

    if (src_impl->resource.pool != WINED3DPOOL_SYSTEMMEM || dst_impl->resource.pool != WINED3DPOOL_DEFAULT)
    {
        WARN("source %p must be SYSTEMMEM and dest %p must be DEFAULT, returning WINED3DERR_INVALIDCALL\n",
                src_surface, dst_surface);
        return WINED3DERR_INVALIDCALL;
    }

    src_format = src_impl->resource.format;
    dst_format = dst_impl->resource.format;

    if (src_format->id != dst_format->id)
    {
        WARN("Source and destination surfaces should have the same format.\n");
        return WINED3DERR_INVALIDCALL;
    }

    dst_x = dst_point ? dst_point->x : 0;
    dst_y = dst_point ? dst_point->y : 0;

    /* This call loads the OpenGL surface directly, instead of copying the
     * surface to the destination's sysmem copy. If surface conversion is
     * needed, use BltFast instead to copy in sysmem and use regular surface
     * loading. */
    d3dfmt_get_conv(dst_impl, FALSE, TRUE, &format, &convert);
    if (convert != NO_CONVERSION || format.convert)
        return IWineD3DSurface_BltFast(dst_surface, dst_x, dst_y, src_surface, src_rect, 0);

    context = context_acquire(This, NULL);
    gl_info = context->gl_info;

    ENTER_GL();
    GL_EXTCALL(glActiveTextureARB(GL_TEXTURE0_ARB));
    checkGLcall("glActiveTextureARB");
    LEAVE_GL();

    /* Make sure the surface is loaded and up to date */
    surface_internal_preload(dst_impl, SRGB_RGB);
    surface_bind(dst_impl, gl_info, FALSE);

    src_w = src_impl->resource.width;
    src_h = src_impl->resource.height;
    update_w = src_rect ? src_rect->right - src_rect->left : src_w;
    update_h = src_rect ? src_rect->bottom - src_rect->top : src_h;

    data = src_impl->resource.allocatedMemory;
    if (!data) ERR("Source surface has no allocated memory, but should be a sysmem surface.\n");

    ENTER_GL();

    if (dst_format->flags & WINED3DFMT_FLAG_COMPRESSED)
    {
        UINT row_length = wined3d_format_calculate_size(src_format, 1, update_w, 1);
        UINT row_count = (update_h + src_format->block_height - 1) / src_format->block_height;
        UINT src_pitch = wined3d_format_calculate_size(src_format, 1, src_w, 1);

        if (src_rect)
        {
            data += (src_rect->top / src_format->block_height) * src_pitch;
            data += (src_rect->left / src_format->block_width) * src_format->block_byte_count;
        }

        TRACE("glCompressedTexSubImage2DARB, target %#x, level %d, x %d, y %d, w %d, h %d, "
                "format %#x, image_size %#x, data %p.\n", dst_impl->texture_target, dst_impl->texture_level,
                dst_x, dst_y, update_w, update_h, dst_format->glFormat, row_count * row_length, data);

        if (row_length == src_pitch)
        {
            GL_EXTCALL(glCompressedTexSubImage2DARB(dst_impl->texture_target, dst_impl->texture_level,
                    dst_x, dst_y, update_w, update_h, dst_format->glInternal, row_count * row_length, data));
        }
        else
        {
            UINT row, y;

            /* glCompressedTexSubImage2DARB() ignores pixel store state, so we
             * can't use the unpack row length like below. */
            for (row = 0, y = dst_y; row < row_count; ++row)
            {
                GL_EXTCALL(glCompressedTexSubImage2DARB(dst_impl->texture_target, dst_impl->texture_level,
                        dst_x, y, update_w, src_format->block_height, dst_format->glInternal, row_length, data));
                y += src_format->block_height;
                data += src_pitch;
            }
        }
        checkGLcall("glCompressedTexSubImage2DARB");
    }
    else
    {
        if (src_rect)
        {
            data += src_rect->top * src_w * src_format->byte_count;
            data += src_rect->left * src_format->byte_count;
        }

        TRACE("glTexSubImage2D, target %#x, level %d, x %d, y %d, w %d, h %d, format %#x, type %#x, data %p.\n",
                dst_impl->texture_target, dst_impl->texture_level, dst_x, dst_y,
                update_w, update_h, dst_format->glFormat, dst_format->glType, data);

        glPixelStorei(GL_UNPACK_ROW_LENGTH, src_w);
        glTexSubImage2D(dst_impl->texture_target, dst_impl->texture_level, dst_x, dst_y,
                update_w, update_h, dst_format->glFormat, dst_format->glType, data);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        checkGLcall("glTexSubImage2D");
    }

    LEAVE_GL();
    context_release(context);

    surface_modify_location(dst_impl, SFLAG_INTEXTURE, TRUE);
    sampler = This->rev_tex_unit_map[0];
    if (sampler != WINED3D_UNMAPPED_STAGE)
    {
        IWineD3DDeviceImpl_MarkStateDirty(This, STATE_SAMPLER(sampler));
    }

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_DrawRectPatch(IWineD3DDevice *iface, UINT Handle, CONST float* pNumSegs, CONST WINED3DRECTPATCH_INFO* pRectPatchInfo) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    struct WineD3DRectPatch *patch;
    GLenum old_primitive_type;
    unsigned int i;
    struct list *e;
    BOOL found;
    TRACE("(%p) Handle(%d) noSegs(%p) rectpatch(%p)\n", This, Handle, pNumSegs, pRectPatchInfo);

    if(!(Handle || pRectPatchInfo)) {
        /* TODO: Write a test for the return value, thus the FIXME */
        FIXME("Both Handle and pRectPatchInfo are NULL\n");
        return WINED3DERR_INVALIDCALL;
    }

    if(Handle) {
        i = PATCHMAP_HASHFUNC(Handle);
        found = FALSE;
        LIST_FOR_EACH(e, &This->patches[i]) {
            patch = LIST_ENTRY(e, struct WineD3DRectPatch, entry);
            if(patch->Handle == Handle) {
                found = TRUE;
                break;
            }
        }

        if(!found) {
            TRACE("Patch does not exist. Creating a new one\n");
            patch = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*patch));
            patch->Handle = Handle;
            list_add_head(&This->patches[i], &patch->entry);
        } else {
            TRACE("Found existing patch %p\n", patch);
        }
    } else {
        /* Since opengl does not load tesselated vertex attributes into numbered vertex
         * attributes we have to tesselate, read back, and draw. This needs a patch
         * management structure instance. Create one.
         *
         * A possible improvement is to check if a vertex shader is used, and if not directly
         * draw the patch.
         */
        FIXME("Drawing an uncached patch. This is slow\n");
        patch = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*patch));
    }

    if (pNumSegs[0] != patch->numSegs[0] || pNumSegs[1] != patch->numSegs[1]
            || pNumSegs[2] != patch->numSegs[2] || pNumSegs[3] != patch->numSegs[3]
            || (pRectPatchInfo && memcmp(pRectPatchInfo, &patch->RectPatchInfo, sizeof(*pRectPatchInfo))))
    {
        HRESULT hr;
        TRACE("Tesselation density or patch info changed, retesselating\n");

        if(pRectPatchInfo) {
            patch->RectPatchInfo = *pRectPatchInfo;
        }
        patch->numSegs[0] = pNumSegs[0];
        patch->numSegs[1] = pNumSegs[1];
        patch->numSegs[2] = pNumSegs[2];
        patch->numSegs[3] = pNumSegs[3];

        hr = tesselate_rectpatch(This, patch);
        if(FAILED(hr)) {
            WARN("Patch tesselation failed\n");

            /* Do not release the handle to store the params of the patch */
            if(!Handle) {
                HeapFree(GetProcessHeap(), 0, patch);
            }
            return hr;
        }
    }

    This->currentPatch = patch;
    old_primitive_type = This->stateBlock->state.gl_primitive_type;
    This->stateBlock->state.gl_primitive_type = GL_TRIANGLES;
    IWineD3DDevice_DrawPrimitiveStrided(iface, patch->numSegs[0] * patch->numSegs[1] * 2 * 3, &patch->strided);
    This->stateBlock->state.gl_primitive_type = old_primitive_type;
    This->currentPatch = NULL;

    /* Destroy uncached patches */
    if(!Handle) {
        HeapFree(GetProcessHeap(), 0, patch->mem);
        HeapFree(GetProcessHeap(), 0, patch);
    }
    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_DrawTriPatch(IWineD3DDevice *iface,
        UINT handle, const float *segment_count, const WINED3DTRIPATCH_INFO *patch_info)
{
    FIXME("iface %p, handle %#x, segment_count %p, patch_info %p stub!\n",
            iface, handle, segment_count, patch_info);

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_DeletePatch(IWineD3DDevice *iface, UINT Handle) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    int i;
    struct WineD3DRectPatch *patch;
    struct list *e;
    TRACE("(%p) Handle(%d)\n", This, Handle);

    i = PATCHMAP_HASHFUNC(Handle);
    LIST_FOR_EACH(e, &This->patches[i]) {
        patch = LIST_ENTRY(e, struct WineD3DRectPatch, entry);
        if(patch->Handle == Handle) {
            TRACE("Deleting patch %p\n", patch);
            list_remove(&patch->entry);
            HeapFree(GetProcessHeap(), 0, patch->mem);
            HeapFree(GetProcessHeap(), 0, patch);
            return WINED3D_OK;
        }
    }

    /* TODO: Write a test for the return value */
    FIXME("Attempt to destroy nonexistent patch\n");
    return WINED3DERR_INVALIDCALL;
}

/* Do not call while under the GL lock. */
static HRESULT WINAPI IWineD3DDeviceImpl_ColorFill(IWineD3DDevice *iface,
        IWineD3DSurface *surface, const RECT *rect, const WINED3DCOLORVALUE *color)
{
    IWineD3DSurfaceImpl *s = (IWineD3DSurfaceImpl *)surface;

    TRACE("iface %p, surface %p, rect %s, color {%.8e, %.8e, %.8e, %.8e}.\n",
            iface, surface, wine_dbgstr_rect(rect),
            color->r, color->g, color->b, color->a);

    if (s->resource.pool != WINED3DPOOL_DEFAULT && s->resource.pool != WINED3DPOOL_SYSTEMMEM)
    {
        FIXME("call to colorfill with non WINED3DPOOL_DEFAULT or WINED3DPOOL_SYSTEMMEM surface\n");
        return WINED3DERR_INVALIDCALL;
    }

    return surface_color_fill(s, rect, color);
}

/* Do not call while under the GL lock. */
static void WINAPI IWineD3DDeviceImpl_ClearRendertargetView(IWineD3DDevice *iface,
        struct wined3d_rendertarget_view *rendertarget_view, const WINED3DCOLORVALUE *color)
{
    struct wined3d_resource *resource;
    HRESULT hr;

    resource = rendertarget_view->resource;
    if (resource->resourceType != WINED3DRTYPE_SURFACE)
    {
        FIXME("Only supported on surface resources\n");
        return;
    }

    hr = surface_color_fill(surface_from_resource(resource), NULL, color);
    if (FAILED(hr)) ERR("Color fill failed, hr %#x.\n", hr);
}

/* rendertarget and depth stencil functions */
static HRESULT WINAPI IWineD3DDeviceImpl_GetRenderTarget(IWineD3DDevice *iface,
        DWORD render_target_idx, IWineD3DSurface **render_target)
{
    IWineD3DDeviceImpl *device = (IWineD3DDeviceImpl *)iface;

    TRACE("iface %p, render_target_idx %u, render_target %p.\n",
            iface, render_target_idx, render_target);

    if (render_target_idx >= device->adapter->gl_info.limits.buffers)
    {
        WARN("Only %u render targets are supported.\n", device->adapter->gl_info.limits.buffers);
        return WINED3DERR_INVALIDCALL;
    }

    *render_target = (IWineD3DSurface *)device->render_targets[render_target_idx];
    if (*render_target) IWineD3DSurface_AddRef(*render_target);

    TRACE("Returning render target %p.\n", *render_target);

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetDepthStencilSurface(IWineD3DDevice *iface, IWineD3DSurface **depth_stencil)
{
    IWineD3DDeviceImpl *device = (IWineD3DDeviceImpl *)iface;

    TRACE("iface %p, depth_stencil %p.\n", iface, depth_stencil);

    *depth_stencil = (IWineD3DSurface *)device->depth_stencil;
    TRACE("Returning depth/stencil surface %p.\n", *depth_stencil);
    if (!*depth_stencil) return WINED3DERR_NOTFOUND;
    IWineD3DSurface_AddRef(*depth_stencil);

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetRenderTarget(IWineD3DDevice *iface,
        DWORD render_target_idx, IWineD3DSurface *render_target, BOOL set_viewport)
{
    IWineD3DDeviceImpl *device = (IWineD3DDeviceImpl *)iface;
    IWineD3DSurfaceImpl *prev;

    TRACE("iface %p, render_target_idx %u, render_target %p, set_viewport %#x.\n",
            iface, render_target_idx, render_target, set_viewport);

    if (render_target_idx >= device->adapter->gl_info.limits.buffers)
    {
        WARN("Only %u render targets are supported.\n", device->adapter->gl_info.limits.buffers);
        return WINED3DERR_INVALIDCALL;
    }

    prev = device->render_targets[render_target_idx];
    if (render_target == (IWineD3DSurface *)prev)
    {
        TRACE("Trying to do a NOP SetRenderTarget operation.\n");
        return WINED3D_OK;
    }

    /* Render target 0 can't be set to NULL. */
    if (!render_target && !render_target_idx)
    {
        WARN("Trying to set render target 0 to NULL.\n");
        return WINED3DERR_INVALIDCALL;
    }

    if (render_target && !(((IWineD3DSurfaceImpl *)render_target)->resource.usage & WINED3DUSAGE_RENDERTARGET))
    {
        FIXME("Surface %p doesn't have render target usage.\n", render_target);
        return WINED3DERR_INVALIDCALL;
    }

    if (render_target) IWineD3DSurface_AddRef(render_target);
    device->render_targets[render_target_idx] = (IWineD3DSurfaceImpl *)render_target;
    /* Release after the assignment, to prevent device_resource_released()
     * from seeing the surface as still in use. */
    if (prev) IWineD3DSurface_Release((IWineD3DSurface *)prev);

    /* Render target 0 is special. */
    if (!render_target_idx && set_viewport)
    {
        /* Set the viewport and scissor rectangles, if requested. Tests show
         * that stateblock recording is ignored, the change goes directly
         * into the primary stateblock. */
        device->stateBlock->state.viewport.Height = device->render_targets[0]->resource.height;
        device->stateBlock->state.viewport.Width  = device->render_targets[0]->resource.width;
        device->stateBlock->state.viewport.X      = 0;
        device->stateBlock->state.viewport.Y      = 0;
        device->stateBlock->state.viewport.MaxZ   = 1.0f;
        device->stateBlock->state.viewport.MinZ   = 0.0f;
        IWineD3DDeviceImpl_MarkStateDirty(device, STATE_VIEWPORT);

        device->stateBlock->state.scissor_rect.top = 0;
        device->stateBlock->state.scissor_rect.left = 0;
        device->stateBlock->state.scissor_rect.right = device->stateBlock->state.viewport.Width;
        device->stateBlock->state.scissor_rect.bottom = device->stateBlock->state.viewport.Height;
        IWineD3DDeviceImpl_MarkStateDirty(device, STATE_SCISSORRECT);
    }

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetDepthStencilSurface(IWineD3DDevice *iface, IWineD3DSurface *depth_stencil)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DSurfaceImpl *tmp;

    TRACE("device %p, depth_stencil %p, old depth_stencil %p.\n", This, depth_stencil, This->depth_stencil);

    if (This->depth_stencil == (IWineD3DSurfaceImpl *)depth_stencil)
    {
        TRACE("Trying to do a NOP SetRenderTarget operation.\n");
        return WINED3D_OK;
    }

    if (This->depth_stencil)
    {
        if (This->swapchains[0]->presentParms.Flags & WINED3DPRESENTFLAG_DISCARD_DEPTHSTENCIL
                || This->depth_stencil->flags & SFLAG_DISCARD)
        {
            surface_modify_ds_location(This->depth_stencil, SFLAG_DS_DISCARDED,
                    This->depth_stencil->resource.width,
                    This->depth_stencil->resource.height);
            if (This->depth_stencil == This->onscreen_depth_stencil)
            {
                IWineD3DSurface_Release((IWineD3DSurface *)This->onscreen_depth_stencil);
                This->onscreen_depth_stencil = NULL;
            }
        }
    }

    tmp = This->depth_stencil;
    This->depth_stencil = (IWineD3DSurfaceImpl *)depth_stencil;
    if (This->depth_stencil) IWineD3DSurface_AddRef((IWineD3DSurface *)This->depth_stencil);
    if (tmp) IWineD3DSurface_Release((IWineD3DSurface *)tmp);

    if ((!tmp && depth_stencil) || (!depth_stencil && tmp))
    {
        /* Swapping NULL / non NULL depth stencil affects the depth and tests */
        IWineD3DDeviceImpl_MarkStateDirty(This, STATE_RENDER(WINED3DRS_ZENABLE));
        IWineD3DDeviceImpl_MarkStateDirty(This, STATE_RENDER(WINED3DRS_STENCILENABLE));
        IWineD3DDeviceImpl_MarkStateDirty(This, STATE_RENDER(WINED3DRS_STENCILWRITEMASK));
    }

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetCursorProperties(IWineD3DDevice *iface,
        UINT XHotSpot, UINT YHotSpot, IWineD3DSurface *cursor_image)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    IWineD3DSurfaceImpl *s = (IWineD3DSurfaceImpl *)cursor_image;
    WINED3DLOCKED_RECT lockedRect;

    TRACE("iface %p, hotspot_x %u, hotspot_y %u, cursor_image %p.\n",
            iface, XHotSpot, YHotSpot, cursor_image);

    /* some basic validation checks */
    if (This->cursorTexture)
    {
        struct wined3d_context *context = context_acquire(This, NULL);
        ENTER_GL();
        glDeleteTextures(1, &This->cursorTexture);
        LEAVE_GL();
        context_release(context);
        This->cursorTexture = 0;
    }

    if (s->resource.width == 32 && s->resource.height == 32)
        This->haveHardwareCursor = TRUE;
    else
        This->haveHardwareCursor = FALSE;

    if (cursor_image)
    {
        WINED3DLOCKED_RECT rect;

        /* MSDN: Cursor must be A8R8G8B8 */
        if (s->resource.format->id != WINED3DFMT_B8G8R8A8_UNORM)
        {
            WARN("surface %p has an invalid format.\n", cursor_image);
            return WINED3DERR_INVALIDCALL;
        }

        /* MSDN: Cursor must be smaller than the display mode */
        if (s->resource.width > This->ddraw_width
                || s->resource.height > This->ddraw_height)
        {
            WARN("Surface %p dimensions are %ux%u, but screen dimensions are %ux%u.\n",
                    s, s->resource.width, s->resource.height, This->ddraw_width, This->ddraw_height);
            return WINED3DERR_INVALIDCALL;
        }

        if (!This->haveHardwareCursor) {
            /* TODO: MSDN: Cursor sizes must be a power of 2 */

            /* Do not store the surface's pointer because the application may
             * release it after setting the cursor image. Windows doesn't
             * addref the set surface, so we can't do this either without
             * creating circular refcount dependencies. Copy out the gl texture
             * instead.
             */
            This->cursorWidth = s->resource.width;
            This->cursorHeight = s->resource.height;
            if (SUCCEEDED(IWineD3DSurface_Map(cursor_image, &rect, NULL, WINED3DLOCK_READONLY)))
            {
                const struct wined3d_gl_info *gl_info = &This->adapter->gl_info;
                const struct wined3d_format *format = wined3d_get_format(gl_info, WINED3DFMT_B8G8R8A8_UNORM);
                struct wined3d_context *context;
                char *mem, *bits = rect.pBits;
                GLint intfmt = format->glInternal;
                GLint gl_format = format->glFormat;
                GLint type = format->glType;
                INT height = This->cursorHeight;
                INT width = This->cursorWidth;
                INT bpp = format->byte_count;
                DWORD sampler;
                INT i;

                /* Reformat the texture memory (pitch and width can be
                 * different) */
                mem = HeapAlloc(GetProcessHeap(), 0, width * height * bpp);
                for(i = 0; i < height; i++)
                    memcpy(&mem[width * bpp * i], &bits[rect.Pitch * i], width * bpp);
                IWineD3DSurface_Unmap(cursor_image);

                context = context_acquire(This, NULL);

                ENTER_GL();

                if (gl_info->supported[APPLE_CLIENT_STORAGE])
                {
                    glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_FALSE);
                    checkGLcall("glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_FALSE)");
                }

                /* Make sure that a proper texture unit is selected */
                GL_EXTCALL(glActiveTextureARB(GL_TEXTURE0_ARB));
                checkGLcall("glActiveTextureARB");
                sampler = This->rev_tex_unit_map[0];
                if (sampler != WINED3D_UNMAPPED_STAGE)
                {
                    IWineD3DDeviceImpl_MarkStateDirty(This, STATE_SAMPLER(sampler));
                }
                /* Create a new cursor texture */
                glGenTextures(1, &This->cursorTexture);
                checkGLcall("glGenTextures");
                glBindTexture(GL_TEXTURE_2D, This->cursorTexture);
                checkGLcall("glBindTexture");
                /* Copy the bitmap memory into the cursor texture */
                glTexImage2D(GL_TEXTURE_2D, 0, intfmt, width, height, 0, gl_format, type, mem);
                checkGLcall("glTexImage2D");
                HeapFree(GetProcessHeap(), 0, mem);

                if (gl_info->supported[APPLE_CLIENT_STORAGE])
                {
                    glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_TRUE);
                    checkGLcall("glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_TRUE)");
                }

                LEAVE_GL();

                context_release(context);
            }
            else
            {
                FIXME("A cursor texture was not returned.\n");
                This->cursorTexture = 0;
            }
        }
        else
        {
            /* Draw a hardware cursor */
            ICONINFO cursorInfo;
            HCURSOR cursor;
            /* Create and clear maskBits because it is not needed for
             * 32-bit cursors.  32x32 bits split into 32-bit chunks == 32
             * chunks. */
            DWORD *maskBits = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                    (s->resource.width * s->resource.height / 8));
            IWineD3DSurface_Map(cursor_image, &lockedRect, NULL,
                    WINED3DLOCK_NO_DIRTY_UPDATE | WINED3DLOCK_READONLY);
            TRACE("width: %u height: %u.\n", s->resource.width, s->resource.height);

            cursorInfo.fIcon = FALSE;
            cursorInfo.xHotspot = XHotSpot;
            cursorInfo.yHotspot = YHotSpot;
            cursorInfo.hbmMask = CreateBitmap(s->resource.width, s->resource.height, 1, 1, maskBits);
            cursorInfo.hbmColor = CreateBitmap(s->resource.width, s->resource.height, 1, 32, lockedRect.pBits);
            IWineD3DSurface_Unmap(cursor_image);
            /* Create our cursor and clean up. */
            cursor = CreateIconIndirect(&cursorInfo);
            SetCursor(cursor);
            if (cursorInfo.hbmMask) DeleteObject(cursorInfo.hbmMask);
            if (cursorInfo.hbmColor) DeleteObject(cursorInfo.hbmColor);
            if (This->hardwareCursor) DestroyCursor(This->hardwareCursor);
            This->hardwareCursor = cursor;
            HeapFree(GetProcessHeap(), 0, maskBits);
        }
    }

    This->xHotSpot = XHotSpot;
    This->yHotSpot = YHotSpot;
    return WINED3D_OK;
}

static void WINAPI IWineD3DDeviceImpl_SetCursorPosition(IWineD3DDevice *iface,
        int XScreenSpace, int YScreenSpace, DWORD flags)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *) iface;

    TRACE("iface %p, x %d, y %d, flags %#x.\n",
            iface, XScreenSpace, YScreenSpace, flags);

    This->xScreenSpace = XScreenSpace;
    This->yScreenSpace = YScreenSpace;
}

static BOOL     WINAPI  IWineD3DDeviceImpl_ShowCursor(IWineD3DDevice* iface, BOOL bShow) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *) iface;
    BOOL oldVisible = This->bCursorVisible;
    POINT pt;

    TRACE("(%p) : visible(%d)\n", This, bShow);

    /*
     * When ShowCursor is first called it should make the cursor appear at the OS's last
     * known cursor position.  Because of this, some applications just repetitively call
     * ShowCursor in order to update the cursor's position.  This behavior is undocumented.
     */
    GetCursorPos(&pt);
    This->xScreenSpace = pt.x;
    This->yScreenSpace = pt.y;

    if (This->haveHardwareCursor) {
        This->bCursorVisible = bShow;
        if (bShow)
            SetCursor(This->hardwareCursor);
        else
            SetCursor(NULL);
    }
    else
    {
        if (This->cursorTexture)
            This->bCursorVisible = bShow;
    }

    return oldVisible;
}

static HRESULT WINAPI evict_managed_resource(struct wined3d_resource *resource, void *data)
{
    TRACE("checking resource %p for eviction\n", resource);

    if (resource->pool == WINED3DPOOL_MANAGED)
    {
        TRACE("Evicting %p.\n", resource);
        resource->resource_ops->resource_unload(resource);
    }

    return S_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_EvictManagedResources(IWineD3DDevice *iface)
{
    TRACE("iface %p.\n", iface);

    IWineD3DDevice_EnumResources(iface, evict_managed_resource, NULL);
    /* Invalidate stream sources, the buffer(s) may have been evicted. */
    IWineD3DDeviceImpl_MarkStateDirty((IWineD3DDeviceImpl *)iface, STATE_STREAMSRC);

    return WINED3D_OK;
}

static HRESULT updateSurfaceDesc(IWineD3DSurfaceImpl *surface, const WINED3DPRESENT_PARAMETERS* pPresentationParameters)
{
    IWineD3DDeviceImpl *device = surface->resource.device;
    const struct wined3d_gl_info *gl_info = &device->adapter->gl_info;

    /* Reallocate proper memory for the front and back buffer and adjust their sizes */
    if (surface->flags & SFLAG_DIBSECTION)
    {
        /* Release the DC */
        SelectObject(surface->hDC, surface->dib.holdbitmap);
        DeleteDC(surface->hDC);
        /* Release the DIB section */
        DeleteObject(surface->dib.DIBsection);
        surface->dib.bitmap_data = NULL;
        surface->resource.allocatedMemory = NULL;
        surface->flags &= ~SFLAG_DIBSECTION;
    }
    surface->resource.width = pPresentationParameters->BackBufferWidth;
    surface->resource.height = pPresentationParameters->BackBufferHeight;
    if (gl_info->supported[ARB_TEXTURE_NON_POWER_OF_TWO] || gl_info->supported[ARB_TEXTURE_RECTANGLE]
            || gl_info->supported[WINED3D_GL_NORMALIZED_TEXRECT])
    {
        surface->pow2Width = pPresentationParameters->BackBufferWidth;
        surface->pow2Height = pPresentationParameters->BackBufferHeight;
    } else {
        surface->pow2Width = surface->pow2Height = 1;
        while (surface->pow2Width < pPresentationParameters->BackBufferWidth) surface->pow2Width <<= 1;
        while (surface->pow2Height < pPresentationParameters->BackBufferHeight) surface->pow2Height <<= 1;
    }

    if (surface->texture_name)
    {
        struct wined3d_context *context = context_acquire(device, NULL);
        ENTER_GL();
        glDeleteTextures(1, &surface->texture_name);
        LEAVE_GL();
        context_release(context);
        surface->texture_name = 0;
        surface->flags &= ~SFLAG_CLIENT;
    }
    if (surface->pow2Width != pPresentationParameters->BackBufferWidth
            || surface->pow2Height != pPresentationParameters->BackBufferHeight)
    {
        surface->flags |= SFLAG_NONPOW2;
    }
    else
    {
        surface->flags &= ~SFLAG_NONPOW2;
    }
    HeapFree(GetProcessHeap(), 0, surface->resource.heapMemory);
    surface->resource.allocatedMemory = NULL;
    surface->resource.heapMemory = NULL;
    surface->resource.size = IWineD3DSurface_GetPitch((IWineD3DSurface *) surface) * surface->pow2Width;

    /* Put all surfaces into sysmem - the drawable might disappear if the backbuffer was rendered
     * to a FBO */
    if (!surface_init_sysmem(surface))
    {
        return E_OUTOFMEMORY;
    }
    return WINED3D_OK;
}

static BOOL is_display_mode_supported(IWineD3DDeviceImpl *This, const WINED3DPRESENT_PARAMETERS *pp)
{
    UINT i, count;
    WINED3DDISPLAYMODE m;
    HRESULT hr;

    /* All Windowed modes are supported, as is leaving the current mode */
    if(pp->Windowed) return TRUE;
    if(!pp->BackBufferWidth) return TRUE;
    if(!pp->BackBufferHeight) return TRUE;

    count = wined3d_get_adapter_mode_count(This->wined3d, This->adapter->ordinal, WINED3DFMT_UNKNOWN);
    for (i = 0; i < count; ++i)
    {
        memset(&m, 0, sizeof(m));
        hr = wined3d_enum_adapter_modes(This->wined3d, This->adapter->ordinal, WINED3DFMT_UNKNOWN, i, &m);
        if (FAILED(hr))
            ERR("Failed to enumerate adapter mode.\n");
        if (m.Width == pp->BackBufferWidth && m.Height == pp->BackBufferHeight)
            /* Mode found, it is supported. */
            return TRUE;
    }
    /* Mode not found -> not supported */
    return FALSE;
}

/* Do not call while under the GL lock. */
static void delete_opengl_contexts(IWineD3DDeviceImpl *device, IWineD3DSwapChainImpl *swapchain)
{
    const struct wined3d_gl_info *gl_info;
    struct wined3d_context *context;
    struct wined3d_shader *shader;

    context = context_acquire(device, NULL);
    gl_info = context->gl_info;

    IWineD3DDevice_EnumResources((IWineD3DDevice *)device, device_unload_resource, NULL);
    LIST_FOR_EACH_ENTRY(shader, &device->shaders, struct wined3d_shader, shader_list_entry)
    {
        device->shader_backend->shader_destroy(shader);
    }

    ENTER_GL();
    if (device->depth_blt_texture)
    {
        glDeleteTextures(1, &device->depth_blt_texture);
        device->depth_blt_texture = 0;
    }
    if (device->depth_blt_rb)
    {
        gl_info->fbo_ops.glDeleteRenderbuffers(1, &device->depth_blt_rb);
        device->depth_blt_rb = 0;
        device->depth_blt_rb_w = 0;
        device->depth_blt_rb_h = 0;
    }
    LEAVE_GL();

    device->blitter->free_private(device);
    device->frag_pipe->free_private(device);
    device->shader_backend->shader_free_private(device);
    destroy_dummy_textures(device, gl_info);

    context_release(context);

    while (device->context_count)
    {
        context_destroy(device, device->contexts[0]);
    }
    HeapFree(GetProcessHeap(), 0, swapchain->context);
    swapchain->context = NULL;
    swapchain->num_contexts = 0;
}

/* Do not call while under the GL lock. */
static HRESULT create_primary_opengl_context(IWineD3DDeviceImpl *device, IWineD3DSwapChainImpl *swapchain)
{
    struct wined3d_context *context;
    HRESULT hr;
    IWineD3DSurfaceImpl *target;

    /* Recreate the primary swapchain's context */
    swapchain->context = HeapAlloc(GetProcessHeap(), 0, sizeof(*swapchain->context));
    if (!swapchain->context)
    {
        ERR("Failed to allocate memory for swapchain context array.\n");
        return E_OUTOFMEMORY;
    }

    target = swapchain->back_buffers ? swapchain->back_buffers[0] : swapchain->front_buffer;
    if (!(context = context_create(swapchain, target, swapchain->ds_format)))
    {
        WARN("Failed to create context.\n");
        HeapFree(GetProcessHeap(), 0, swapchain->context);
        return E_FAIL;
    }

    swapchain->context[0] = context;
    swapchain->num_contexts = 1;
    create_dummy_textures(device);
    context_release(context);

    hr = device->shader_backend->shader_alloc_private(device);
    if (FAILED(hr))
    {
        ERR("Failed to allocate shader private data, hr %#x.\n", hr);
        goto err;
    }

    hr = device->frag_pipe->alloc_private(device);
    if (FAILED(hr))
    {
        ERR("Failed to allocate fragment pipe private data, hr %#x.\n", hr);
        device->shader_backend->shader_free_private(device);
        goto err;
    }

    hr = device->blitter->alloc_private(device);
    if (FAILED(hr))
    {
        ERR("Failed to allocate blitter private data, hr %#x.\n", hr);
        device->frag_pipe->free_private(device);
        device->shader_backend->shader_free_private(device);
        goto err;
    }

    return WINED3D_OK;

err:
    context_acquire(device, NULL);
    destroy_dummy_textures(device, context->gl_info);
    context_release(context);
    context_destroy(device, context);
    HeapFree(GetProcessHeap(), 0, swapchain->context);
    swapchain->num_contexts = 0;
    return hr;
}

/* Do not call while under the GL lock. */
static HRESULT WINAPI IWineD3DDeviceImpl_Reset(IWineD3DDevice *iface,
        WINED3DPRESENT_PARAMETERS *pPresentationParameters)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *) iface;
    IWineD3DSwapChainImpl *swapchain;
    HRESULT hr;
    BOOL DisplayModeChanged = FALSE;
    WINED3DDISPLAYMODE mode;
    TRACE("(%p)\n", This);

    hr = IWineD3DDevice_GetSwapChain(iface, 0, (IWineD3DSwapChain **) &swapchain);
    if(FAILED(hr)) {
        ERR("Failed to get the first implicit swapchain\n");
        return hr;
    }

    if(!is_display_mode_supported(This, pPresentationParameters)) {
        WARN("Rejecting Reset() call because the requested display mode is not supported\n");
        WARN("Requested mode: %d, %d\n", pPresentationParameters->BackBufferWidth,
             pPresentationParameters->BackBufferHeight);
        IWineD3DSwapChain_Release((IWineD3DSwapChain *)swapchain);
        return WINED3DERR_INVALIDCALL;
    }

    /* Is it necessary to recreate the gl context? Actually every setting can be changed
     * on an existing gl context, so there's no real need for recreation.
     *
     * TODO: Figure out how Reset influences resources in D3DPOOL_DEFAULT, D3DPOOL_SYSTEMMEMORY and D3DPOOL_MANAGED
     *
     * TODO: Figure out what happens to explicit swapchains, or if we have more than one implicit swapchain
     */
    TRACE("New params:\n");
    TRACE("BackBufferWidth = %d\n", pPresentationParameters->BackBufferWidth);
    TRACE("BackBufferHeight = %d\n", pPresentationParameters->BackBufferHeight);
    TRACE("BackBufferFormat = %s\n", debug_d3dformat(pPresentationParameters->BackBufferFormat));
    TRACE("BackBufferCount = %d\n", pPresentationParameters->BackBufferCount);
    TRACE("MultiSampleType = %d\n", pPresentationParameters->MultiSampleType);
    TRACE("MultiSampleQuality = %d\n", pPresentationParameters->MultiSampleQuality);
    TRACE("SwapEffect = %d\n", pPresentationParameters->SwapEffect);
    TRACE("hDeviceWindow = %p\n", pPresentationParameters->hDeviceWindow);
    TRACE("Windowed = %s\n", pPresentationParameters->Windowed ? "true" : "false");
    TRACE("EnableAutoDepthStencil = %s\n", pPresentationParameters->EnableAutoDepthStencil ? "true" : "false");
    TRACE("Flags = %08x\n", pPresentationParameters->Flags);
    TRACE("FullScreen_RefreshRateInHz = %d\n", pPresentationParameters->FullScreen_RefreshRateInHz);
    TRACE("PresentationInterval = %d\n", pPresentationParameters->PresentationInterval);

    /* No special treatment of these parameters. Just store them */
    swapchain->presentParms.SwapEffect = pPresentationParameters->SwapEffect;
    swapchain->presentParms.Flags = pPresentationParameters->Flags;
    swapchain->presentParms.PresentationInterval = pPresentationParameters->PresentationInterval;
    swapchain->presentParms.FullScreen_RefreshRateInHz = pPresentationParameters->FullScreen_RefreshRateInHz;

    /* What to do about these? */
    if (pPresentationParameters->BackBufferCount
            && pPresentationParameters->BackBufferCount != swapchain->presentParms.BackBufferCount)
        ERR("Cannot change the back buffer count yet\n");

    if(pPresentationParameters->BackBufferFormat != WINED3DFMT_UNKNOWN &&
        pPresentationParameters->BackBufferFormat != swapchain->presentParms.BackBufferFormat) {
        ERR("Cannot change the back buffer format yet\n");
    }

    if (pPresentationParameters->hDeviceWindow
            && pPresentationParameters->hDeviceWindow != swapchain->presentParms.hDeviceWindow)
        ERR("Cannot change the device window yet\n");

    if (pPresentationParameters->EnableAutoDepthStencil && !This->auto_depth_stencil)
    {
        HRESULT hrc;

        TRACE("Creating the depth stencil buffer\n");

        hrc = IWineD3DDeviceParent_CreateDepthStencilSurface(This->device_parent,
                pPresentationParameters->BackBufferWidth,
                pPresentationParameters->BackBufferHeight,
                pPresentationParameters->AutoDepthStencilFormat,
                pPresentationParameters->MultiSampleType,
                pPresentationParameters->MultiSampleQuality,
                FALSE,
                (IWineD3DSurface **)&This->auto_depth_stencil);

        if (FAILED(hrc)) {
            ERR("Failed to create the depth stencil buffer\n");
            IWineD3DSwapChain_Release((IWineD3DSwapChain *) swapchain);
            return WINED3DERR_INVALIDCALL;
        }
    }

    if (This->onscreen_depth_stencil)
    {
        IWineD3DSurface_Release((IWineD3DSurface *)This->onscreen_depth_stencil);
        This->onscreen_depth_stencil = NULL;
    }

    /* Reset the depth stencil */
    if (pPresentationParameters->EnableAutoDepthStencil)
        IWineD3DDevice_SetDepthStencilSurface(iface, (IWineD3DSurface *)This->auto_depth_stencil);
    else
        IWineD3DDevice_SetDepthStencilSurface(iface, NULL);

    TRACE("Resetting stateblock\n");
    wined3d_stateblock_decref(This->updateStateBlock);
    wined3d_stateblock_decref(This->stateBlock);

    delete_opengl_contexts(This, swapchain);

    if(pPresentationParameters->Windowed) {
        mode.Width = swapchain->orig_width;
        mode.Height = swapchain->orig_height;
        mode.RefreshRate = 0;
        mode.Format = swapchain->presentParms.BackBufferFormat;
    } else {
        mode.Width = pPresentationParameters->BackBufferWidth;
        mode.Height = pPresentationParameters->BackBufferHeight;
        mode.RefreshRate = pPresentationParameters->FullScreen_RefreshRateInHz;
        mode.Format = swapchain->presentParms.BackBufferFormat;
    }

    /* Should Width == 800 && Height == 0 set 800x600? */
    if (pPresentationParameters->BackBufferWidth && pPresentationParameters->BackBufferHeight
            && (pPresentationParameters->BackBufferWidth != swapchain->presentParms.BackBufferWidth
            || pPresentationParameters->BackBufferHeight != swapchain->presentParms.BackBufferHeight))
    {
        UINT i;

        if(!pPresentationParameters->Windowed) {
            DisplayModeChanged = TRUE;
        }
        swapchain->presentParms.BackBufferWidth = pPresentationParameters->BackBufferWidth;
        swapchain->presentParms.BackBufferHeight = pPresentationParameters->BackBufferHeight;

        hr = updateSurfaceDesc(swapchain->front_buffer, pPresentationParameters);
        if(FAILED(hr))
        {
            IWineD3DSwapChain_Release((IWineD3DSwapChain *) swapchain);
            return hr;
        }

        for (i = 0; i < swapchain->presentParms.BackBufferCount; ++i)
        {
            hr = updateSurfaceDesc(swapchain->back_buffers[i], pPresentationParameters);
            if(FAILED(hr))
            {
                IWineD3DSwapChain_Release((IWineD3DSwapChain *) swapchain);
                return hr;
            }
        }
        if (This->auto_depth_stencil)
        {
            hr = updateSurfaceDesc(This->auto_depth_stencil, pPresentationParameters);
            if(FAILED(hr))
            {
                IWineD3DSwapChain_Release((IWineD3DSwapChain *) swapchain);
                return hr;
            }
        }
    }

    if (!pPresentationParameters->Windowed != !swapchain->presentParms.Windowed
            || DisplayModeChanged)
    {
        BOOL filter = This->filter_messages;
        This->filter_messages = TRUE;

        IWineD3DDevice_SetDisplayMode(iface, 0, &mode);

        if (!pPresentationParameters->Windowed)
        {
            if (swapchain->presentParms.Windowed)
            {
                HWND focus_window = This->createParms.hFocusWindow;
                if (!focus_window) focus_window = pPresentationParameters->hDeviceWindow;
                if (FAILED(hr = IWineD3DDevice_AcquireFocusWindow(iface, focus_window)))
                {
                    ERR("Failed to acquire focus window, hr %#x.\n", hr);
                    IWineD3DSwapChain_Release((IWineD3DSwapChain *)swapchain);
                    return hr;
                }

                /* switch from windowed to fs */
                IWineD3DDevice_SetupFullscreenWindow(iface, swapchain->device_window,
                        pPresentationParameters->BackBufferWidth,
                        pPresentationParameters->BackBufferHeight);
            }
            else
            {
                /* Fullscreen -> fullscreen mode change */
                MoveWindow(swapchain->device_window, 0, 0,
                           pPresentationParameters->BackBufferWidth, pPresentationParameters->BackBufferHeight,
                           TRUE);
            }
        }
        else if (!swapchain->presentParms.Windowed)
        {
            /* Fullscreen -> windowed switch */
            IWineD3DDevice_RestoreFullscreenWindow(iface, swapchain->device_window);
            IWineD3DDevice_ReleaseFocusWindow(iface);
        }
        swapchain->presentParms.Windowed = pPresentationParameters->Windowed;

        This->filter_messages = filter;
    }
    else if (!pPresentationParameters->Windowed)
    {
        DWORD style = This->style, exStyle = This->exStyle;
        /* If we're in fullscreen, and the mode wasn't changed, we have to get the window back into
         * the right position. Some applications(Battlefield 2, Guild Wars) move it and then call
         * Reset to clear up their mess. Guild Wars also loses the device during that.
         */
        This->style = 0;
        This->exStyle = 0;
        IWineD3DDevice_SetupFullscreenWindow(iface, swapchain->device_window,
                pPresentationParameters->BackBufferWidth,
                pPresentationParameters->BackBufferHeight);
        This->style = style;
        This->exStyle = exStyle;
    }

    /* Note: No parent needed for initial internal stateblock */
    hr = IWineD3DDevice_CreateStateBlock(iface, WINED3DSBT_INIT, &This->stateBlock);
    if (FAILED(hr)) ERR("Resetting the stateblock failed with error 0x%08x\n", hr);
    else TRACE("Created stateblock %p\n", This->stateBlock);
    This->updateStateBlock = This->stateBlock;
    wined3d_stateblock_incref(This->updateStateBlock);

    stateblock_init_default_state(This->stateBlock);

    if(wined3d_settings.offscreen_rendering_mode == ORM_FBO)
    {
        RECT client_rect;
        GetClientRect(swapchain->win_handle, &client_rect);

        if(!swapchain->presentParms.BackBufferCount)
        {
            TRACE("Single buffered rendering\n");
            swapchain->render_to_fbo = FALSE;
        }
        else if(swapchain->presentParms.BackBufferWidth  != client_rect.right  ||
                swapchain->presentParms.BackBufferHeight != client_rect.bottom )
        {
            TRACE("Rendering to FBO. Backbuffer %ux%u, window %ux%u\n",
                    swapchain->presentParms.BackBufferWidth,
                    swapchain->presentParms.BackBufferHeight,
                    client_rect.right, client_rect.bottom);
            swapchain->render_to_fbo = TRUE;
        }
        else
        {
            TRACE("Rendering directly to GL_BACK\n");
            swapchain->render_to_fbo = FALSE;
        }
    }

    hr = create_primary_opengl_context(This, swapchain);
    IWineD3DSwapChain_Release((IWineD3DSwapChain *) swapchain);

    /* All done. There is no need to reload resources or shaders, this will happen automatically on the
     * first use
     */
    return hr;
}

static HRESULT WINAPI IWineD3DDeviceImpl_SetDialogBoxMode(IWineD3DDevice *iface, BOOL enable_dialogs)
{
    TRACE("iface %p, enable_dialogs %#x.\n", iface, enable_dialogs);

    if (!enable_dialogs) FIXME("Dialogs cannot be disabled yet.\n");

    return WINED3D_OK;
}


static HRESULT  WINAPI  IWineD3DDeviceImpl_GetCreationParameters(IWineD3DDevice *iface, WINED3DDEVICE_CREATION_PARAMETERS *pParameters) {
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *) iface;
    TRACE("(%p) : pParameters %p\n", This, pParameters);

    *pParameters = This->createParms;
    return WINED3D_OK;
}

static void WINAPI IWineD3DDeviceImpl_SetGammaRamp(IWineD3DDevice *iface,
        UINT iSwapChain, DWORD flags, const WINED3DGAMMARAMP *pRamp)
{
    IWineD3DSwapChain *swapchain;

    TRACE("Relaying  to swapchain\n");

    if (IWineD3DDeviceImpl_GetSwapChain(iface, iSwapChain, &swapchain) == WINED3D_OK)
    {
        IWineD3DSwapChain_SetGammaRamp(swapchain, flags, pRamp);
        IWineD3DSwapChain_Release(swapchain);
    }
}

static void WINAPI IWineD3DDeviceImpl_GetGammaRamp(IWineD3DDevice *iface, UINT iSwapChain, WINED3DGAMMARAMP* pRamp) {
    IWineD3DSwapChain *swapchain;

    TRACE("Relaying  to swapchain\n");

    if (IWineD3DDeviceImpl_GetSwapChain(iface, iSwapChain, &swapchain) == WINED3D_OK) {
        IWineD3DSwapChain_GetGammaRamp(swapchain, pRamp);
        IWineD3DSwapChain_Release(swapchain);
    }
}

void device_resource_add(struct IWineD3DDeviceImpl *device, struct wined3d_resource *resource)
{
    TRACE("device %p, resource %p.\n", device, resource);

    list_add_head(&device->resources, &resource->resource_list_entry);
}

static void device_resource_remove(struct IWineD3DDeviceImpl *device, struct wined3d_resource *resource)
{
    TRACE("device %p, resource %p.\n", device, resource);

    list_remove(&resource->resource_list_entry);
}

void device_resource_released(struct IWineD3DDeviceImpl *device, struct wined3d_resource *resource)
{
    WINED3DRESOURCETYPE type = resource->resourceType;
    unsigned int i;

    TRACE("device %p, resource %p, type %s.\n", device, resource, debug_d3dresourcetype(type));

    context_resource_released(device, resource, type);

    switch (type)
    {
        case WINED3DRTYPE_SURFACE:
            {
                IWineD3DSurfaceImpl *surface = surface_from_resource(resource);

                if (!device->d3d_initialized) break;

                for (i = 0; i < device->adapter->gl_info.limits.buffers; ++i)
                {
                    if (device->render_targets[i] == surface)
                    {
                        ERR("Surface %p is still in use as render target %u.\n", surface, i);
                        device->render_targets[i] = NULL;
                    }
                }

                if (device->depth_stencil == surface)
                {
                    ERR("Surface %p is still in use as depth/stencil buffer.\n", surface);
                    device->depth_stencil = NULL;
                }
            }
            break;

        case WINED3DRTYPE_TEXTURE:
        case WINED3DRTYPE_CUBETEXTURE:
        case WINED3DRTYPE_VOLUMETEXTURE:
            for (i = 0; i < MAX_COMBINED_SAMPLERS; ++i)
            {
                struct wined3d_texture *texture = wined3d_texture_from_resource(resource);

                if (device->stateBlock && device->stateBlock->state.textures[i] == texture)
                {
                    ERR("Texture %p is still in use by stateblock %p, stage %u.\n",
                            texture, device->stateBlock, i);
                    device->stateBlock->state.textures[i] = NULL;
                }

                if (device->updateStateBlock != device->stateBlock
                        && device->updateStateBlock->state.textures[i] == texture)
                {
                    ERR("Texture %p is still in use by stateblock %p, stage %u.\n",
                            texture, device->updateStateBlock, i);
                    device->updateStateBlock->state.textures[i] = NULL;
                }
            }
            break;

        case WINED3DRTYPE_BUFFER:
            {
                struct wined3d_buffer *buffer = buffer_from_resource(resource);

                for (i = 0; i < MAX_STREAMS; ++i)
                {
                    if (device->stateBlock && device->stateBlock->state.streams[i].buffer == buffer)
                    {
                        ERR("Buffer %p is still in use by stateblock %p, stream %u.\n",
                                buffer, device->stateBlock, i);
                        device->stateBlock->state.streams[i].buffer = NULL;
                    }

                    if (device->updateStateBlock != device->stateBlock
                            && device->updateStateBlock->state.streams[i].buffer == buffer)
                    {
                        ERR("Buffer %p is still in use by stateblock %p, stream %u.\n",
                                buffer, device->updateStateBlock, i);
                        device->updateStateBlock->state.streams[i].buffer = NULL;
                    }

                }

                if (device->stateBlock && device->stateBlock->state.index_buffer == buffer)
                {
                    ERR("Buffer %p is still in use by stateblock %p as index buffer.\n",
                            buffer, device->stateBlock);
                    device->stateBlock->state.index_buffer =  NULL;
                }

                if (device->updateStateBlock != device->stateBlock
                        && device->updateStateBlock->state.index_buffer == buffer)
                {
                    ERR("Buffer %p is still in use by stateblock %p as index buffer.\n",
                            buffer, device->updateStateBlock);
                    device->updateStateBlock->state.index_buffer =  NULL;
                }
            }
            break;

        default:
            break;
    }

    /* Remove the resource from the resourceStore */
    device_resource_remove(device, resource);

    TRACE("Resource released.\n");
}

static HRESULT WINAPI IWineD3DDeviceImpl_EnumResources(IWineD3DDevice *iface,
        D3DCB_ENUMRESOURCES callback, void *data)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *) iface;
    struct wined3d_resource *resource, *cursor;

    TRACE("iface %p, callback %p, data %p.\n", iface, callback, data);

    LIST_FOR_EACH_ENTRY_SAFE(resource, cursor, &This->resources, struct wined3d_resource, resource_list_entry)
    {
        TRACE("enumerating resource %p.\n", resource);
        if (callback(resource, data) == S_FALSE)
        {
            TRACE("Canceling enumeration.\n");
            break;
        }
    }

    return WINED3D_OK;
}

static HRESULT WINAPI IWineD3DDeviceImpl_GetSurfaceFromDC(IWineD3DDevice *iface, HDC dc, IWineD3DSurface **surface)
{
    IWineD3DDeviceImpl *This = (IWineD3DDeviceImpl *)iface;
    struct wined3d_resource *resource;

    LIST_FOR_EACH_ENTRY(resource, &This->resources, struct wined3d_resource, resource_list_entry)
    {
        if (resource->resourceType == WINED3DRTYPE_SURFACE)
        {
            IWineD3DSurfaceImpl *s = surface_from_resource(resource);

            if (s->hDC == dc)
            {
                TRACE("Found surface %p for dc %p.\n", s, dc);
                *surface = (IWineD3DSurface *)s;
                return WINED3D_OK;
            }
        }
    }

    return WINED3DERR_INVALIDCALL;
}

/**********************************************************
 * IWineD3DDevice VTbl follows
 **********************************************************/

static const IWineD3DDeviceVtbl IWineD3DDevice_Vtbl =
{
    /*** IUnknown methods ***/
    IWineD3DDeviceImpl_QueryInterface,
    IWineD3DDeviceImpl_AddRef,
    IWineD3DDeviceImpl_Release,
    /*** IWineD3DDevice methods ***/
    /*** Creation methods**/
    IWineD3DDeviceImpl_CreateBuffer,
    IWineD3DDeviceImpl_CreateVertexBuffer,
    IWineD3DDeviceImpl_CreateIndexBuffer,
    IWineD3DDeviceImpl_CreateStateBlock,
    IWineD3DDeviceImpl_CreateSurface,
    IWineD3DDeviceImpl_CreateRendertargetView,
    IWineD3DDeviceImpl_CreateTexture,
    IWineD3DDeviceImpl_CreateVolumeTexture,
    IWineD3DDeviceImpl_CreateVolume,
    IWineD3DDeviceImpl_CreateCubeTexture,
    IWineD3DDeviceImpl_CreateQuery,
    IWineD3DDeviceImpl_CreateSwapChain,
    IWineD3DDeviceImpl_CreateVertexDeclaration,
    IWineD3DDeviceImpl_CreateVertexDeclarationFromFVF,
    IWineD3DDeviceImpl_CreateVertexShader,
    IWineD3DDeviceImpl_CreateGeometryShader,
    IWineD3DDeviceImpl_CreatePixelShader,
    IWineD3DDeviceImpl_CreatePalette,
    /*** Odd functions **/
    IWineD3DDeviceImpl_Init3D,
    IWineD3DDeviceImpl_InitGDI,
    IWineD3DDeviceImpl_Uninit3D,
    IWineD3DDeviceImpl_UninitGDI,
    IWineD3DDeviceImpl_SetMultithreaded,
    IWineD3DDeviceImpl_EvictManagedResources,
    IWineD3DDeviceImpl_GetAvailableTextureMem,
    IWineD3DDeviceImpl_GetBackBuffer,
    IWineD3DDeviceImpl_GetCreationParameters,
    IWineD3DDeviceImpl_GetDeviceCaps,
    IWineD3DDeviceImpl_GetDirect3D,
    IWineD3DDeviceImpl_GetDisplayMode,
    IWineD3DDeviceImpl_SetDisplayMode,
    IWineD3DDeviceImpl_GetNumberOfSwapChains,
    IWineD3DDeviceImpl_GetRasterStatus,
    IWineD3DDeviceImpl_GetSwapChain,
    IWineD3DDeviceImpl_Reset,
    IWineD3DDeviceImpl_SetDialogBoxMode,
    IWineD3DDeviceImpl_SetCursorProperties,
    IWineD3DDeviceImpl_SetCursorPosition,
    IWineD3DDeviceImpl_ShowCursor,
    /*** Getters and setters **/
    IWineD3DDeviceImpl_SetClipPlane,
    IWineD3DDeviceImpl_GetClipPlane,
    IWineD3DDeviceImpl_SetClipStatus,
    IWineD3DDeviceImpl_GetClipStatus,
    IWineD3DDeviceImpl_SetCurrentTexturePalette,
    IWineD3DDeviceImpl_GetCurrentTexturePalette,
    IWineD3DDeviceImpl_SetDepthStencilSurface,
    IWineD3DDeviceImpl_GetDepthStencilSurface,
    IWineD3DDeviceImpl_SetGammaRamp,
    IWineD3DDeviceImpl_GetGammaRamp,
    IWineD3DDeviceImpl_SetIndexBuffer,
    IWineD3DDeviceImpl_GetIndexBuffer,
    IWineD3DDeviceImpl_SetBaseVertexIndex,
    IWineD3DDeviceImpl_GetBaseVertexIndex,
    IWineD3DDeviceImpl_SetLight,
    IWineD3DDeviceImpl_GetLight,
    IWineD3DDeviceImpl_SetLightEnable,
    IWineD3DDeviceImpl_GetLightEnable,
    IWineD3DDeviceImpl_SetMaterial,
    IWineD3DDeviceImpl_GetMaterial,
    IWineD3DDeviceImpl_SetNPatchMode,
    IWineD3DDeviceImpl_GetNPatchMode,
    IWineD3DDeviceImpl_SetPaletteEntries,
    IWineD3DDeviceImpl_GetPaletteEntries,
    IWineD3DDeviceImpl_SetPixelShader,
    IWineD3DDeviceImpl_GetPixelShader,
    IWineD3DDeviceImpl_SetPixelShaderConstantB,
    IWineD3DDeviceImpl_GetPixelShaderConstantB,
    IWineD3DDeviceImpl_SetPixelShaderConstantI,
    IWineD3DDeviceImpl_GetPixelShaderConstantI,
    IWineD3DDeviceImpl_SetPixelShaderConstantF,
    IWineD3DDeviceImpl_GetPixelShaderConstantF,
    IWineD3DDeviceImpl_SetRenderState,
    IWineD3DDeviceImpl_GetRenderState,
    IWineD3DDeviceImpl_SetRenderTarget,
    IWineD3DDeviceImpl_GetRenderTarget,
    IWineD3DDeviceImpl_SetSamplerState,
    IWineD3DDeviceImpl_GetSamplerState,
    IWineD3DDeviceImpl_SetScissorRect,
    IWineD3DDeviceImpl_GetScissorRect,
    IWineD3DDeviceImpl_SetSoftwareVertexProcessing,
    IWineD3DDeviceImpl_GetSoftwareVertexProcessing,
    IWineD3DDeviceImpl_SetStreamSource,
    IWineD3DDeviceImpl_GetStreamSource,
    IWineD3DDeviceImpl_SetStreamSourceFreq,
    IWineD3DDeviceImpl_GetStreamSourceFreq,
    IWineD3DDeviceImpl_SetTexture,
    IWineD3DDeviceImpl_GetTexture,
    IWineD3DDeviceImpl_SetTextureStageState,
    IWineD3DDeviceImpl_GetTextureStageState,
    IWineD3DDeviceImpl_SetTransform,
    IWineD3DDeviceImpl_GetTransform,
    IWineD3DDeviceImpl_SetVertexDeclaration,
    IWineD3DDeviceImpl_GetVertexDeclaration,
    IWineD3DDeviceImpl_SetVertexShader,
    IWineD3DDeviceImpl_GetVertexShader,
    IWineD3DDeviceImpl_SetVertexShaderConstantB,
    IWineD3DDeviceImpl_GetVertexShaderConstantB,
    IWineD3DDeviceImpl_SetVertexShaderConstantI,
    IWineD3DDeviceImpl_GetVertexShaderConstantI,
    IWineD3DDeviceImpl_SetVertexShaderConstantF,
    IWineD3DDeviceImpl_GetVertexShaderConstantF,
    IWineD3DDeviceImpl_SetViewport,
    IWineD3DDeviceImpl_GetViewport,
    IWineD3DDeviceImpl_MultiplyTransform,
    IWineD3DDeviceImpl_ValidateDevice,
    IWineD3DDeviceImpl_ProcessVertices,
    /*** State block ***/
    IWineD3DDeviceImpl_BeginStateBlock,
    IWineD3DDeviceImpl_EndStateBlock,
    /*** Scene management ***/
    IWineD3DDeviceImpl_BeginScene,
    IWineD3DDeviceImpl_EndScene,
    IWineD3DDeviceImpl_Present,
    IWineD3DDeviceImpl_Clear,
    IWineD3DDeviceImpl_ClearRendertargetView,
    /*** Drawing ***/
    IWineD3DDeviceImpl_SetPrimitiveType,
    IWineD3DDeviceImpl_GetPrimitiveType,
    IWineD3DDeviceImpl_DrawPrimitive,
    IWineD3DDeviceImpl_DrawIndexedPrimitive,
    IWineD3DDeviceImpl_DrawPrimitiveUP,
    IWineD3DDeviceImpl_DrawIndexedPrimitiveUP,
    IWineD3DDeviceImpl_DrawPrimitiveStrided,
    IWineD3DDeviceImpl_DrawIndexedPrimitiveStrided,
    IWineD3DDeviceImpl_DrawRectPatch,
    IWineD3DDeviceImpl_DrawTriPatch,
    IWineD3DDeviceImpl_DeletePatch,
    IWineD3DDeviceImpl_ColorFill,
    IWineD3DDeviceImpl_UpdateTexture,
    IWineD3DDeviceImpl_UpdateSurface,
    IWineD3DDeviceImpl_GetFrontBufferData,
    /*** object tracking ***/
    IWineD3DDeviceImpl_EnumResources,
    IWineD3DDeviceImpl_GetSurfaceFromDC,
    IWineD3DDeviceImpl_AcquireFocusWindow,
    IWineD3DDeviceImpl_ReleaseFocusWindow,
    IWineD3DDeviceImpl_SetupFullscreenWindow,
    IWineD3DDeviceImpl_RestoreFullscreenWindow,
};

HRESULT device_init(IWineD3DDeviceImpl *device, struct wined3d *wined3d,
        UINT adapter_idx, WINED3DDEVTYPE device_type, HWND focus_window, DWORD flags,
        IWineD3DDeviceParent *device_parent)
{
    struct wined3d_adapter *adapter = &wined3d->adapters[adapter_idx];
    const struct fragment_pipeline *fragment_pipeline;
    struct shader_caps shader_caps;
    struct fragment_caps ffp_caps;
    WINED3DDISPLAYMODE mode;
    unsigned int i;
    HRESULT hr;

    device->lpVtbl = &IWineD3DDevice_Vtbl;
    device->ref = 1;
    device->wined3d = wined3d;
    wined3d_incref(device->wined3d);
    device->adapter = wined3d->adapter_count ? adapter : NULL;
    device->device_parent = device_parent;
    list_init(&device->resources);
    list_init(&device->shaders);

    device->surface_alignment = wined3d->dxVersion == 7 ? DDRAW_PITCH_ALIGNMENT : D3D8_PITCH_ALIGNMENT;

    /* Get the initial screen setup for ddraw. */
    hr = wined3d_get_adapter_display_mode(wined3d, adapter_idx, &mode);
    if (FAILED(hr))
    {
        ERR("Failed to get the adapter's display mode, hr %#x.\n", hr);
        wined3d_decref(device->wined3d);
        return hr;
    }
    device->ddraw_width = mode.Width;
    device->ddraw_height = mode.Height;
    device->ddraw_format = mode.Format;

    /* Save the creation parameters. */
    device->createParms.AdapterOrdinal = adapter_idx;
    device->createParms.DeviceType = device_type;
    device->createParms.hFocusWindow = focus_window;
    device->createParms.BehaviorFlags = flags;

    device->devType = device_type;
    for (i = 0; i < PATCHMAP_SIZE; ++i) list_init(&device->patches[i]);

    select_shader_mode(&adapter->gl_info, &device->ps_selected_mode, &device->vs_selected_mode);
    device->shader_backend = adapter->shader_backend;

    if (device->shader_backend)
    {
        device->shader_backend->shader_get_caps(&adapter->gl_info, &shader_caps);
        device->d3d_vshader_constantF = shader_caps.MaxVertexShaderConst;
        device->d3d_pshader_constantF = shader_caps.MaxPixelShaderConst;
        device->vs_clipping = shader_caps.VSClipping;
    }
    fragment_pipeline = adapter->fragment_pipe;
    device->frag_pipe = fragment_pipeline;
    if (fragment_pipeline)
    {
        fragment_pipeline->get_caps(&adapter->gl_info, &ffp_caps);
        device->max_ffp_textures = ffp_caps.MaxSimultaneousTextures;

        hr = compile_state_table(device->StateTable, device->multistate_funcs, &adapter->gl_info,
                                 ffp_vertexstate_template, fragment_pipeline, misc_state_template);
        if (FAILED(hr))
        {
            ERR("Failed to compile state table, hr %#x.\n", hr);
            wined3d_decref(device->wined3d);
            return hr;
        }
    }
    device->blitter = adapter->blitter;

    return WINED3D_OK;
}


void IWineD3DDeviceImpl_MarkStateDirty(IWineD3DDeviceImpl *This, DWORD state) {
    DWORD rep = This->StateTable[state].representative;
    struct wined3d_context *context;
    DWORD idx;
    BYTE shift;
    UINT i;

    for (i = 0; i < This->context_count; ++i)
    {
        context = This->contexts[i];
        if(isStateDirty(context, rep)) continue;

        context->dirtyArray[context->numDirtyEntries++] = rep;
        idx = rep / (sizeof(*context->isStateDirty) * CHAR_BIT);
        shift = rep & ((sizeof(*context->isStateDirty) * CHAR_BIT) - 1);
        context->isStateDirty[idx] |= (1 << shift);
    }
}

void get_drawable_size_fbo(struct wined3d_context *context, UINT *width, UINT *height)
{
    /* The drawable size of a fbo target is the opengl texture size, which is the power of two size. */
    *width = context->current_rt->pow2Width;
    *height = context->current_rt->pow2Height;
}

void get_drawable_size_backbuffer(struct wined3d_context *context, UINT *width, UINT *height)
{
    IWineD3DSwapChainImpl *swapchain = context->swapchain;
    /* The drawable size of a backbuffer / aux buffer offscreen target is the size of the
     * current context's drawable, which is the size of the back buffer of the swapchain
     * the active context belongs to. */
    *width = swapchain->presentParms.BackBufferWidth;
    *height = swapchain->presentParms.BackBufferHeight;
}

LRESULT device_process_message(IWineD3DDeviceImpl *device, HWND window, BOOL unicode,
        UINT message, WPARAM wparam, LPARAM lparam, WNDPROC proc)
{
    if (device->filter_messages)
    {
        TRACE("Filtering message: window %p, message %#x, wparam %#lx, lparam %#lx.\n",
                window, message, wparam, lparam);
        if (unicode)
            return DefWindowProcW(window, message, wparam, lparam);
        else
            return DefWindowProcA(window, message, wparam, lparam);
    }

    if (message == WM_DESTROY)
    {
        TRACE("unregister window %p.\n", window);
        wined3d_unregister_window(window);

        if (device->focus_window == window) device->focus_window = NULL;
        else ERR("Window %p is not the focus window for device %p.\n", window, device);
    }

    if (unicode)
        return CallWindowProcW(proc, window, message, wparam, lparam);
    else
        return CallWindowProcA(proc, window, message, wparam, lparam);
}
