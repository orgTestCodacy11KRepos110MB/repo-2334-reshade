/*
 * Copyright (C) 2021 Patrick Mours. All rights reserved.
 * License: https://github.com/crosire/reshade#license
 */

#include "render_gl.hpp"
#include <cassert>

using namespace reshade::api;

static inline GLenum get_binding_for_target(GLenum target)
{
	switch (target)
	{
	default:
		return GL_NONE;
	case GL_ARRAY_BUFFER:
		return GL_ARRAY_BUFFER_BINDING;
	case GL_ELEMENT_ARRAY_BUFFER:
		return GL_ELEMENT_ARRAY_BUFFER_BINDING;
	case GL_PIXEL_PACK_BUFFER:
		return GL_PIXEL_PACK_BUFFER_BINDING;
	case GL_PIXEL_UNPACK_BUFFER:
		return GL_PIXEL_UNPACK_BUFFER_BINDING;
	case GL_UNIFORM_BUFFER:
		return GL_UNIFORM_BUFFER_BINDING;
	case GL_TEXTURE_BUFFER:
		return GL_TEXTURE_BINDING_BUFFER;
	case GL_TRANSFORM_FEEDBACK_BUFFER:
		return GL_TRANSFORM_FEEDBACK_BUFFER_BINDING;
	case GL_COPY_READ_BUFFER:
		return GL_COPY_READ_BUFFER_BINDING;
	case GL_COPY_WRITE_BUFFER:
		return GL_COPY_WRITE_BUFFER_BINDING;
	case GL_DRAW_INDIRECT_BUFFER:
		return GL_DRAW_INDIRECT_BUFFER_BINDING;
	case GL_SHADER_STORAGE_BUFFER:
		return GL_SHADER_STORAGE_BUFFER_BINDING;
	case GL_DISPATCH_INDIRECT_BUFFER:
		return GL_DISPATCH_INDIRECT_BUFFER_BINDING;
	case GL_QUERY_BUFFER:
		return GL_QUERY_BUFFER_BINDING;
	case GL_ATOMIC_COUNTER_BUFFER:
		return GL_ATOMIC_COUNTER_BUFFER_BINDING;
	case GL_TEXTURE_1D:
		return GL_TEXTURE_BINDING_1D;
	case GL_TEXTURE_1D_ARRAY:
		return GL_TEXTURE_BINDING_1D_ARRAY;
	case GL_TEXTURE_2D:
		return GL_TEXTURE_BINDING_2D;
	case GL_TEXTURE_2D_ARRAY:
		return GL_TEXTURE_BINDING_2D_ARRAY;
	case GL_TEXTURE_2D_MULTISAMPLE:
		return GL_TEXTURE_BINDING_2D_MULTISAMPLE;
	case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
		return GL_TEXTURE_BINDING_2D_MULTISAMPLE_ARRAY;
	case GL_TEXTURE_3D:
		return GL_TEXTURE_BINDING_3D;
	case GL_TEXTURE_CUBE_MAP:
		return GL_TEXTURE_BINDING_CUBE_MAP;
	case GL_TEXTURE_CUBE_MAP_ARRAY:
		return GL_TEXTURE_BINDING_CUBE_MAP_ARRAY;
	case GL_TEXTURE_RECTANGLE:
		return GL_TEXTURE_BINDING_RECTANGLE;
	}
}

static inline GLenum convert_to_internal_format(GLenum format)
{
	// Convert depth formats to internal texture formats
	switch (format)
	{
	default:
		return format;
	case GL_DEPTH_STENCIL:
		return GL_DEPTH24_STENCIL8;
	case GL_DEPTH_COMPONENT:
		return GL_DEPTH_COMPONENT24;
	}
}

static inline GLboolean is_depth_stencil_format(GLenum format, GLenum usage = GL_DEPTH_STENCIL)
{
	switch (format)
	{
	case GL_DEPTH_COMPONENT:
	case GL_DEPTH_COMPONENT16:
	case GL_DEPTH_COMPONENT24:
	case GL_DEPTH_COMPONENT32:
	case GL_DEPTH_COMPONENT32F:
		return usage == GL_DEPTH_STENCIL || usage == GL_DEPTH;
	case GL_DEPTH_STENCIL:
	case GL_DEPTH24_STENCIL8:
	case GL_DEPTH32F_STENCIL8:
		return usage == GL_DEPTH_STENCIL || usage == GL_DEPTH || usage == GL_STENCIL;
	case GL_STENCIL:
	case GL_STENCIL_INDEX1:
	case GL_STENCIL_INDEX4:
	case GL_STENCIL_INDEX8:
	case GL_STENCIL_INDEX16:
		return usage == GL_DEPTH_STENCIL || usage == GL_STENCIL;
	default:
		return GL_FALSE;
	}
}

static GLint get_rbo_param(GLuint id, GLenum param)
{
	GLint value = 0;
	if (gl3wProcs.gl.GetNamedRenderbufferParameteriv != nullptr)
	{
		glGetNamedRenderbufferParameteriv(id, param, &value);
	}
	else
	{
		GLint prev_binding = 0;
		glGetIntegerv(GL_RENDERBUFFER_BINDING, &prev_binding);
		glBindRenderbuffer(GL_RENDERBUFFER, id);
		glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &value);
		glBindRenderbuffer(GL_RENDERBUFFER, prev_binding);
	}
	return value;
}
static GLint get_buf_param(GLenum target, GLuint id, GLenum param)
{
	GLint value = 0;
	if (gl3wProcs.gl.GetNamedBufferParameteriv != nullptr)
	{
		glGetNamedBufferParameteriv(id, param, &value);
	}
	else
	{
		if (GL_TEXTURE == target)
			target = GL_TEXTURE_2D;

		GLint prev_binding = 0;
		glGetIntegerv(get_binding_for_target(target), &prev_binding);
		glBindBuffer(target, id);
		glGetBufferParameteriv(target, param, &value);
		glBindBuffer(target, prev_binding);
	}
	return value;
}
static GLint get_tex_param(GLenum target, GLuint id, GLenum param)
{
	GLint value = 0;
	if (gl3wProcs.gl.GetTextureParameteriv != nullptr)
	{
		glGetTextureParameteriv(id, param, &value);
	}
	else
	{
		if (GL_TEXTURE == target)
			target = GL_TEXTURE_2D;

		GLint prev_binding = 0;
		glGetIntegerv(get_binding_for_target(target), &prev_binding);
		glBindTexture(target, id);
		glGetTexParameteriv(target, param, &value);
		glBindTexture(target, prev_binding);
	}
	return value;
}
static GLint get_tex_level_param(GLenum target, GLuint id, GLuint level, GLenum param)
{
	GLint value = 0;
	if (gl3wProcs.gl.GetTextureLevelParameteriv != nullptr)
	{
		glGetTextureLevelParameteriv(id, level, param, &value);
	}
	else
	{
		if (GL_TEXTURE == target)
			target = GL_TEXTURE_2D;

		GLint prev_binding = 0;
		glGetIntegerv(get_binding_for_target(target), &prev_binding);
		glBindTexture(target, id);
		glGetTexLevelParameteriv(target, level, param, &value);
		glBindTexture(target, prev_binding);
	}
	return value;
}
static GLint get_fbo_attachment_param(GLuint id, GLenum attachment, GLenum param)
{
	GLint value = 0;
	if (gl3wProcs.gl.GetNamedFramebufferAttachmentParameteriv != nullptr)
	{
		glGetNamedFramebufferAttachmentParameteriv(id, attachment, param, &value);
	}
	else
	{
		GLint prev_binding = 0;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_binding);
		glBindFramebuffer(GL_FRAMEBUFFER, id);
		glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, attachment, param, &value);
		glBindFramebuffer(GL_FRAMEBUFFER, prev_binding);
	}
	return value;
}

resource_type reshade::opengl::convert_resource_type(GLenum target)
{
	switch (target)
	{
	default:
		return resource_type::unknown;
	case GL_ARRAY_BUFFER:
	case GL_ELEMENT_ARRAY_BUFFER:
	case GL_PIXEL_PACK_BUFFER:
	case GL_PIXEL_UNPACK_BUFFER:
	case GL_UNIFORM_BUFFER:
	case GL_TEXTURE_BUFFER:
	case GL_TRANSFORM_FEEDBACK_BUFFER:
	case GL_COPY_READ_BUFFER:
	case GL_COPY_WRITE_BUFFER:
	case GL_DRAW_INDIRECT_BUFFER:
	case GL_SHADER_STORAGE_BUFFER:
	case GL_DISPATCH_INDIRECT_BUFFER:
	case GL_QUERY_BUFFER:
	case GL_ATOMIC_COUNTER_BUFFER:
		return resource_type::buffer;
	case GL_TEXTURE_1D:
	case GL_TEXTURE_1D_ARRAY:
	case GL_PROXY_TEXTURE_1D:
	case GL_PROXY_TEXTURE_1D_ARRAY:
		return resource_type::texture_1d;
	case GL_TEXTURE_2D:
	case GL_TEXTURE_2D_ARRAY:
	case GL_TEXTURE_RECTANGLE: // This is not technically compatible with 2D textures
	case GL_PROXY_TEXTURE_2D:
	case GL_PROXY_TEXTURE_2D_ARRAY:
	case GL_PROXY_TEXTURE_RECTANGLE:
		return resource_type::texture_2d;
	case GL_TEXTURE_2D_MULTISAMPLE:
	case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
	case GL_PROXY_TEXTURE_2D_MULTISAMPLE:
	case GL_PROXY_TEXTURE_2D_MULTISAMPLE_ARRAY:
		return resource_type::texture_2d;
	case GL_TEXTURE_3D:
	case GL_PROXY_TEXTURE_3D:
		return resource_type::texture_3d;
	case GL_TEXTURE_CUBE_MAP:
	case GL_TEXTURE_CUBE_MAP_ARRAY:
	case GL_PROXY_TEXTURE_CUBE_MAP:
	case GL_PROXY_TEXTURE_CUBE_MAP_ARRAY:
	case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
	case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
	case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
	case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
	case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
	case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
		return resource_type::texture_2d;
	case GL_RENDERBUFFER:
	case GL_FRAMEBUFFER_DEFAULT:
		return resource_type::surface;
	}
}
resource_desc reshade::opengl::convert_resource_desc(GLsizeiptr buffer_size)
{
	resource_desc desc = {};
	desc.buffer_size = buffer_size;
	desc.usage = resource_usage::shader_resource | resource_usage::copy_dest | resource_usage::copy_source;
	return desc;
}
resource_desc reshade::opengl::convert_resource_desc(resource_type type, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth)
{
	resource_desc desc = {};
	desc.width = width;
	desc.height = height;
	assert(depth <= std::numeric_limits<uint16_t>::max());
	desc.depth_or_layers = static_cast<uint16_t>(depth);
	assert(levels <= std::numeric_limits<uint16_t>::max());
	desc.levels = static_cast<uint16_t>(levels);
	desc.format = internalformat;
	desc.samples = 1;

	desc.usage = resource_usage::copy_dest | resource_usage::copy_source;
	if (is_depth_stencil_format(internalformat))
		desc.usage |= resource_usage::depth_stencil;
	if (type == resource_type::texture_1d || type == resource_type::texture_2d || type == resource_type::surface)
		desc.usage |= resource_usage::render_target;
	if (type != resource_type::surface)
		desc.usage |= resource_usage::shader_resource;

	return desc;
}

resource_view_dimension reshade::opengl::convert_resource_view_dimension(GLenum target)
{
	switch (target)
	{
	default:
		return resource_view_dimension::unknown;
	case GL_TEXTURE_BUFFER:
		return resource_view_dimension::buffer;
	case GL_TEXTURE_1D:
		return resource_view_dimension::texture_1d;
	case GL_TEXTURE_1D_ARRAY:
		return resource_view_dimension::texture_1d_array;
	case GL_TEXTURE_2D:
	case GL_TEXTURE_RECTANGLE:
		return resource_view_dimension::texture_2d;
	case GL_TEXTURE_2D_ARRAY:
		return resource_view_dimension::texture_2d_array;
	case GL_TEXTURE_2D_MULTISAMPLE:
		return resource_view_dimension::texture_2d_multisample;
	case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
		return resource_view_dimension::texture_2d_multisample_array;
	case GL_TEXTURE_3D:
		return resource_view_dimension::texture_3d;
	case GL_TEXTURE_CUBE_MAP:
		return resource_view_dimension::texture_cube;
	case GL_TEXTURE_CUBE_MAP_ARRAY:
		return resource_view_dimension::texture_cube_array;
	}
}

reshade::opengl::device_impl::device_impl(HDC hdc, HGLRC hglrc) : _hglrc(hglrc)
{
	RECT window_rect = {};
	GetClientRect(WindowFromDC(hdc), &window_rect);

	_default_fbo_width = window_rect.right - window_rect.left;
	_default_fbo_height = window_rect.bottom - window_rect.top;

	PIXELFORMATDESCRIPTOR pfd = { sizeof(pfd) };
	DescribePixelFormat(hdc, GetPixelFormat(hdc), sizeof(pfd), &pfd);

	switch (pfd.cDepthBits)
	{
	default:
	case  0: _default_depth_format = GL_NONE; // No depth in this pixel format
		break;
	case 16: _default_depth_format = GL_DEPTH_COMPONENT16;
		break;
	case 24: _default_depth_format = pfd.cStencilBits ? GL_DEPTH24_STENCIL8 : GL_DEPTH_COMPONENT24;
		break;
	case 32: _default_depth_format = pfd.cStencilBits ? GL_DEPTH32F_STENCIL8 : GL_DEPTH_COMPONENT32;
		break;
	}

	// Check for special extension to detect whether this is a compatibility context (https://www.khronos.org/opengl/wiki/OpenGL_Context#OpenGL_3.1_and_ARB_compatibility)
	GLint num_extensions = 0;
	glGetIntegerv(GL_NUM_EXTENSIONS, &num_extensions);
	for (GLint i = 0; i < num_extensions; ++i)
	{
		const GLubyte *const extension = glGetStringi(GL_EXTENSIONS, i);
		if (std::strcmp(reinterpret_cast<const char *>(extension), "GL_ARB_compatibility") == 0)
		{
			_compatibility_context = true;
			break;
		}
	}

#if RESHADE_ADDON
	// Load and initialize add-ons
	reshade::addon::load_addons();
#endif

	RESHADE_ADDON_EVENT(init_device, this);
	RESHADE_ADDON_EVENT(init_command_queue, this);

#if RESHADE_ADDON
	// Communicate default state to add-ons
	const resource_view_handle default_render_target = get_render_target_from_fbo(0, 0);
	const resource_view_handle default_depth_stencil = get_depth_stencil_from_fbo(0);
	RESHADE_ADDON_EVENT(set_render_targets_and_depth_stencil, this, 1, &default_render_target, default_depth_stencil);
#endif
}
reshade::opengl::device_impl::~device_impl()
{
	RESHADE_ADDON_EVENT(destroy_command_queue, this);
	RESHADE_ADDON_EVENT(destroy_device, this);

#if RESHADE_ADDON
	reshade::addon::unload_addons();
#endif

	// Destroy framebuffers used in 'copy_resource' implementation
	glDeleteFramebuffers(2, _copy_fbo);
}

bool reshade::opengl::device_impl::check_format_support(uint32_t format, resource_usage usage)
{
	GLint supported = GL_FALSE;
	glGetInternalformativ(GL_TEXTURE_2D, format, GL_INTERNALFORMAT_SUPPORTED, 1, &supported);

	GLint supported_renderable = GL_CAVEAT_SUPPORT;
	if ((usage & resource_usage::render_target) != 0)
		glGetInternalformativ(GL_TEXTURE_2D, format, GL_FRAMEBUFFER_RENDERABLE, 1, &supported_renderable);

	GLint supported_image_load = GL_CAVEAT_SUPPORT;
	if ((usage & resource_usage::unordered_access) != 0)
		glGetInternalformativ(GL_TEXTURE_2D, format, GL_SHADER_IMAGE_LOAD, 1, &supported_image_load);

	return supported != GL_FALSE && supported_renderable != GL_NONE && supported_image_load != GL_NONE;
}

bool reshade::opengl::device_impl::check_resource_handle_valid(resource_handle resource)
{
	switch (resource.handle >> 40)
	{
	default:
		return false;
	case GL_BUFFER:
	case GL_ARRAY_BUFFER:
	case GL_ELEMENT_ARRAY_BUFFER:
	case GL_PIXEL_PACK_BUFFER:
	case GL_PIXEL_UNPACK_BUFFER:
	case GL_UNIFORM_BUFFER:
	case GL_TRANSFORM_FEEDBACK_BUFFER:
	case GL_COPY_READ_BUFFER:
	case GL_COPY_WRITE_BUFFER:
	case GL_DRAW_INDIRECT_BUFFER:
	case GL_SHADER_STORAGE_BUFFER:
	case GL_DISPATCH_INDIRECT_BUFFER:
	case GL_QUERY_BUFFER:
	case GL_ATOMIC_COUNTER_BUFFER:
		return glIsBuffer(resource.handle & 0xFFFFFFFF) != GL_FALSE;
	case GL_TEXTURE:
	case GL_TEXTURE_BUFFER:
	case GL_TEXTURE_1D:
	case GL_TEXTURE_1D_ARRAY:
	case GL_TEXTURE_2D:
	case GL_TEXTURE_2D_ARRAY:
	case GL_TEXTURE_2D_MULTISAMPLE:
	case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
	case GL_TEXTURE_3D:
	case GL_TEXTURE_CUBE_MAP:
	case GL_TEXTURE_CUBE_MAP_ARRAY:
	case GL_TEXTURE_RECTANGLE:
		return glIsTexture(resource.handle & 0xFFFFFFFF) != GL_FALSE;
	case GL_RENDERBUFFER:
		return glIsRenderbuffer(resource.handle & 0xFFFFFFFF) != GL_FALSE;
	case GL_FRAMEBUFFER_DEFAULT:
		return (resource.handle & 0xFFFFFFFF) != GL_DEPTH_ATTACHMENT || _default_depth_format != GL_NONE;
	}
}
bool reshade::opengl::device_impl::check_resource_view_handle_valid(resource_view_handle view)
{
	const GLenum attachment = view.handle >> 40;
	if ((attachment >= GL_COLOR_ATTACHMENT0 && attachment <= GL_COLOR_ATTACHMENT31) || attachment == GL_DEPTH_ATTACHMENT || attachment == GL_STENCIL_ATTACHMENT)
	{
		const GLuint fbo = view.handle & 0xFFFFFFFF;
		return fbo == 0 || glIsFramebuffer(fbo) != GL_FALSE;
	}
	else
	{
		return check_resource_handle_valid({ view.handle });
	}
}

bool reshade::opengl::device_impl::create_resource(resource_type type, const resource_desc &desc, resource_usage, resource_handle *out_resource)
{
	GLenum target = GL_NONE;
	switch (type)
	{
	default:
		return false;
	case resource_type::buffer:
		if ((desc.usage & resource_usage::index_buffer) != 0)
			target = GL_ELEMENT_ARRAY_BUFFER;
		else
			target = GL_ARRAY_BUFFER;
		break;
	case resource_type::texture_1d:
		target = desc.depth_or_layers > 1 ? GL_TEXTURE_1D_ARRAY : GL_TEXTURE_1D;
		break;
	case resource_type::texture_2d:
		target = desc.depth_or_layers > 1 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
		break;
	case resource_type::texture_3d:
		target = GL_TEXTURE_3D;
		break;
	}

	const GLenum internal_format = convert_to_internal_format(desc.format);
	if (internal_format == GL_NONE)
		return false;

	GLint prev_object = 0;
	glGetIntegerv(get_binding_for_target(target), &prev_object);

	GLuint object = 0;
	if (type == resource_type::buffer)
	{
		glGenBuffers(1, &object);
		glBindBuffer(target, object);

		assert(desc.buffer_size <= static_cast<uint64_t>(std::numeric_limits<GLsizeiptr>::max()));
		glBufferData(target, static_cast<GLsizeiptr>(desc.buffer_size), nullptr, GL_STATIC_DRAW);

		glBindBuffer(target, prev_object);
	}
	else
	{
		glGenTextures(1, &object);
		glBindTexture(target, object);

		switch (target)
		{
		case GL_TEXTURE_1D:
			glTexStorage1D(target, desc.levels, internal_format, desc.width);
			break;
		case GL_TEXTURE_1D_ARRAY:
			glTexStorage2D(target, desc.levels, internal_format, desc.width, desc.depth_or_layers);
			break;
		case GL_TEXTURE_2D:
			glTexStorage2D(target, desc.levels, internal_format, desc.width, desc.height);
			break;
		case GL_TEXTURE_2D_ARRAY:
		case GL_TEXTURE_3D:
			glTexStorage3D(target, desc.levels, internal_format, desc.width, desc.height, desc.depth_or_layers);
			break;
		}

		glBindTexture(target, prev_object);
	}

	*out_resource = { (static_cast<uint64_t>(target) << 40) | object };
	return true;
}
bool reshade::opengl::device_impl::create_resource_view(resource_handle resource, resource_view_type, const resource_view_desc &desc, resource_view_handle *out_view)
{
	assert(resource.handle != 0);

	GLenum target = GL_NONE;
	switch (desc.dimension)
	{
	default:
		return false;
	case resource_view_dimension::buffer:
		target = GL_TEXTURE_BUFFER;
		break;
	case resource_view_dimension::texture_1d:
		target = GL_TEXTURE_1D;
		break;
	case resource_view_dimension::texture_1d_array:
		target = GL_TEXTURE_1D_ARRAY;
		break;
	case resource_view_dimension::texture_2d:
		target = GL_TEXTURE_2D;
		break;
	case resource_view_dimension::texture_2d_array:
		target = GL_TEXTURE_2D_ARRAY;
		break;
	case resource_view_dimension::texture_2d_multisample:
		target = GL_TEXTURE_2D_MULTISAMPLE;
		break;
	case resource_view_dimension::texture_2d_multisample_array:
		target = GL_TEXTURE_2D_MULTISAMPLE_ARRAY;
		break;
	case resource_view_dimension::texture_3d:
		target = GL_TEXTURE_3D;
		break;
	case resource_view_dimension::texture_cube:
		target = GL_TEXTURE_CUBE_MAP;
		break;
	case resource_view_dimension::texture_cube_array:
		target = GL_TEXTURE_CUBE_MAP_ARRAY;
		break;
	}

	const GLenum internal_format = convert_to_internal_format(desc.format);
	if (internal_format == GL_NONE)
		return false;

	if (target == (resource.handle >> 40) &&
		desc.first_level == 0 && desc.first_layer == 0 &&
		get_resource_desc(resource).format == internal_format)
	{
		// No need to create a view, so use resource directly, but set a bit so to not destroy it twice via 'destroy_resource_view'
		*out_view = { resource.handle | 0x100000000 };
		return true;
	}
	else
	{
		GLuint object = 0;
		glGenTextures(1, &object);

		if (target != GL_TEXTURE_BUFFER)
		{
			// Number of levels and layers are clamped to those of the original texture
			glTextureView(object, target, resource.handle & 0xFFFFFFFF, internal_format, desc.first_level, desc.levels, desc.first_layer, desc.layers);
		}
		else
		{
			GLint prev_object = 0;
			glGetIntegerv(get_binding_for_target(target), &prev_object);

			glBindTexture(target, object);

			if (desc.buffer_offset == 0 && desc.buffer_size == 0)
			{
				glTexBuffer(target, internal_format, resource.handle & 0xFFFFFFFF);
			}
			else
			{
				assert(desc.buffer_offset <= static_cast<uint64_t>(std::numeric_limits<GLintptr>::max()));
				assert(desc.buffer_size <= static_cast<uint64_t>(std::numeric_limits<GLsizeiptr>::max()));
				glTexBufferRange(target, internal_format, resource.handle & 0xFFFFFFFF, static_cast<GLintptr>(desc.buffer_offset), static_cast<GLsizeiptr>(desc.buffer_size));
			}

			glBindTexture(target, prev_object);
		}

		*out_view = { (static_cast<uint64_t>(target) << 40) | object };
		return true;
	}
}

void reshade::opengl::device_impl::destroy_resource(resource_handle resource)
{
	const GLuint object = resource.handle & 0xFFFFFFFF;
	switch (resource.handle >> 40)
	{
	case GL_BUFFER:
		glDeleteBuffers(1, &object);
		break;
	case GL_TEXTURE:
	case GL_TEXTURE_BUFFER:
	case GL_TEXTURE_1D:
	case GL_TEXTURE_1D_ARRAY:
	case GL_TEXTURE_2D:
	case GL_TEXTURE_2D_ARRAY:
	case GL_TEXTURE_2D_MULTISAMPLE:
	case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
	case GL_TEXTURE_3D:
	case GL_TEXTURE_CUBE_MAP:
	case GL_TEXTURE_CUBE_MAP_ARRAY:
	case GL_TEXTURE_RECTANGLE:
		glDeleteTextures(1, &object);
		break;
	case GL_RENDERBUFFER:
		glDeleteRenderbuffers(1, &object);
		break;
	case GL_FRAMEBUFFER_DEFAULT:
		assert(false); // It is not allowed to destroy the default frame buffer
		break;
	default:
		assert(object == 0);
		break;
	}
}
void reshade::opengl::device_impl::destroy_resource_view(resource_view_handle view)
{
	if ((view.handle & 0x100000000) == 0)
		destroy_resource({ view.handle });
}

resource_view_handle reshade::opengl::device_impl::get_depth_stencil_from_fbo(GLuint fbo)
{
	if (fbo == 0 && _default_depth_format == GL_NONE)
		return { 0 }; // No default depth buffer exists

	// TODO: Try again with stencil attachment if there is no depth attachment
	const GLenum attachment = GL_DEPTH_ATTACHMENT;
	if (fbo != 0 && get_fbo_attachment_param(fbo, attachment, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE) == GL_NONE)
		return { 0 }; // FBO does not have this attachment

	return { (static_cast<uint64_t>(attachment) << 40) | fbo };
}
resource_view_handle reshade::opengl::device_impl::get_render_target_from_fbo(GLuint fbo, GLuint drawbuffer)
{
	const GLenum attachment = GL_COLOR_ATTACHMENT0 + drawbuffer;
	if (fbo != 0 && get_fbo_attachment_param(fbo, attachment, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE) == GL_NONE)
		return { 0 }; // FBO does not have this attachment

	return { (static_cast<uint64_t>(attachment) << 40) | fbo };
}

void reshade::opengl::device_impl::get_resource_from_view(resource_view_handle view, resource_handle *out_resource)
{
	assert(view.handle != 0);

	const GLenum attachment = view.handle >> 40;
	if ((attachment >= GL_COLOR_ATTACHMENT0 && attachment <= GL_COLOR_ATTACHMENT31) || attachment == GL_DEPTH_ATTACHMENT || attachment == GL_STENCIL_ATTACHMENT)
	{
		GLenum target = GL_FRAMEBUFFER_DEFAULT;
		GLuint object = attachment;

		// Zero is valid too, in which case the default frame buffer is referenced, instead of a FBO
		const GLuint fbo = view.handle & 0xFFFFFFFF;
		if (fbo != 0)
		{
			target = get_fbo_attachment_param(fbo, attachment, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE);
			assert(target != GL_NONE);
			object = get_fbo_attachment_param(fbo, attachment, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME);
			if (target == GL_TEXTURE)
				target  = get_tex_param(target, object, GL_TEXTURE_TARGET);
		}

		*out_resource = { (static_cast<uint64_t>(target) << 40) | object };
	}
	else
	{
		*out_resource = { view.handle };
	}
}

resource_desc reshade::opengl::device_impl::get_resource_desc(resource_handle resource)
{
	GLsizei width = 0, height = 1, depth = 1, buffer_size = 0; GLenum internal_format = GL_NONE;

	const GLenum target = resource.handle >> 40;
	const GLuint object = resource.handle & 0xFFFFFFFF;
	switch (target)
	{
	default:
		assert(false);
		break;
	case GL_BUFFER:
	case GL_ARRAY_BUFFER:
	case GL_ELEMENT_ARRAY_BUFFER:
	case GL_PIXEL_PACK_BUFFER:
	case GL_PIXEL_UNPACK_BUFFER:
	case GL_UNIFORM_BUFFER:
	case GL_TRANSFORM_FEEDBACK_BUFFER:
	case GL_COPY_READ_BUFFER:
	case GL_COPY_WRITE_BUFFER:
	case GL_DRAW_INDIRECT_BUFFER:
	case GL_SHADER_STORAGE_BUFFER:
	case GL_DISPATCH_INDIRECT_BUFFER:
	case GL_QUERY_BUFFER:
	case GL_ATOMIC_COUNTER_BUFFER:
		buffer_size = get_buf_param(target, object, GL_BUFFER_SIZE);
		break;
	case GL_TEXTURE:
	case GL_TEXTURE_BUFFER:
	case GL_TEXTURE_1D:
	case GL_TEXTURE_1D_ARRAY:
	case GL_TEXTURE_2D:
	case GL_TEXTURE_2D_ARRAY:
	case GL_TEXTURE_2D_MULTISAMPLE:
	case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
	case GL_TEXTURE_3D:
	case GL_TEXTURE_CUBE_MAP:
	case GL_TEXTURE_CUBE_MAP_ARRAY:
	case GL_TEXTURE_RECTANGLE:
		width = get_tex_level_param(target, object, 0, GL_TEXTURE_WIDTH);
		height = get_tex_level_param(target, object, 0, GL_TEXTURE_HEIGHT);
		depth = get_tex_level_param(target, object, 0, GL_TEXTURE_DEPTH);
		internal_format = get_tex_level_param(target, object, 0, GL_TEXTURE_INTERNAL_FORMAT);
		break;
	case GL_RENDERBUFFER:
		width = get_rbo_param(object, GL_RENDERBUFFER_WIDTH);
		height = get_rbo_param(object, GL_RENDERBUFFER_HEIGHT);
		internal_format = get_rbo_param(object, GL_RENDERBUFFER_INTERNAL_FORMAT);
		break;
	case GL_FRAMEBUFFER_DEFAULT:
		width = _default_fbo_width;
		height = _default_fbo_height;
		internal_format = (object == GL_DEPTH_ATTACHMENT) ? _default_depth_format : GL_NONE;
		break;
	}

	if (buffer_size != 0)
		return convert_resource_desc(buffer_size);
	else
		return convert_resource_desc(convert_resource_type(target), 1, internal_format, width, height, depth);
}

void reshade::opengl::device_impl::wait_idle()
{
	glFinish();
}

void reshade::opengl::device_impl::draw(uint32_t vertices, uint32_t instances, uint32_t first_vertex, uint32_t first_instance)
{
	if (instances <= 1)
	{
		glDrawArrays(GL_TRIANGLES, first_vertex, vertices);
	}
	else
	{
		glDrawArraysInstancedBaseInstance(GL_TRIANGLES, first_vertex, vertices, instances, first_instance);
	}
}
void reshade::opengl::device_impl::draw_indexed(uint32_t indices, uint32_t instances, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance)
{
	if (instances <= 1)
	{
		glDrawElementsBaseVertex(GL_TRIANGLES, indices, GL_UNSIGNED_INT, reinterpret_cast<void *>(static_cast<uintptr_t>(first_index)), vertex_offset);
	}
	else
	{
		glDrawElementsInstancedBaseVertexBaseInstance(GL_TRIANGLES, indices, GL_UNSIGNED_INT, reinterpret_cast<void *>(static_cast<uintptr_t>(first_index)), instances, vertex_offset, first_instance);
	}
}

void reshade::opengl::device_impl::copy_resource(resource_handle source, resource_handle destination)
{
	assert(source.handle != 0 && destination.handle != 0);

	const resource_desc source_desc = get_resource_desc(source);
	const GLenum source_target = source.handle >> 40;
	const GLuint source_object = source.handle & 0xFFFFFFFF;
	const resource_desc destination_desc = get_resource_desc(destination);
	const GLuint destination_target = destination.handle >> 40;
	const GLuint destination_object = destination.handle & 0xFFFFFFFF;

	if (source_target != GL_FRAMEBUFFER_DEFAULT && destination_target != GL_FRAMEBUFFER_DEFAULT)
	{
		glCopyImageSubData(source_object, source_target, 0, 0, 0, 0, destination_object, destination_target, 0, 0, 0, 0, destination_desc.width, destination_desc.height, destination_desc.depth_or_layers);
	}
	else
	{
		if (_copy_fbo[0] == 0 || _copy_fbo[1] == 0)
		{
			glGenFramebuffers(2, _copy_fbo);
		}

		GLint prev_read_fbo = 0;
		GLint prev_draw_fbo = 0;
		glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read_fbo);
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_draw_fbo);

		const GLenum source_attachment = is_depth_stencil_format(source_desc.format, GL_DEPTH) ? GL_DEPTH_ATTACHMENT : GL_COLOR_ATTACHMENT0;
		switch (source_target)
		{
		default:
			assert(false);
			return;
		case GL_TEXTURE:
		case GL_TEXTURE_BUFFER:
		case GL_TEXTURE_1D:
		case GL_TEXTURE_1D_ARRAY:
		case GL_TEXTURE_2D:
		case GL_TEXTURE_2D_ARRAY:
		case GL_TEXTURE_2D_MULTISAMPLE:
		case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
		case GL_TEXTURE_RECTANGLE:
			glBindFramebuffer(GL_READ_FRAMEBUFFER, _copy_fbo[0]);
			glFramebufferTexture(GL_READ_FRAMEBUFFER, source_attachment, source_object, 0);
			assert(glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
			break;
		case GL_RENDERBUFFER:
			glBindFramebuffer(GL_READ_FRAMEBUFFER, _copy_fbo[0]);
			glFramebufferRenderbuffer(GL_READ_FRAMEBUFFER, source_attachment, GL_RENDERBUFFER, source_object);
			assert(glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
			break;
		case GL_FRAMEBUFFER_DEFAULT:
			assert(source_object == source_attachment);
			glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
			break;
		}

		const GLenum destination_attachment = is_depth_stencil_format(destination_desc.format, GL_DEPTH) ? GL_DEPTH_ATTACHMENT : GL_COLOR_ATTACHMENT0;
		switch (destination_target)
		{
		default:
			assert(false);
			return;
		case GL_TEXTURE:
		case GL_TEXTURE_BUFFER:
		case GL_TEXTURE_1D:
		case GL_TEXTURE_1D_ARRAY:
		case GL_TEXTURE_2D:
		case GL_TEXTURE_2D_ARRAY:
		case GL_TEXTURE_2D_MULTISAMPLE:
		case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
		case GL_TEXTURE_RECTANGLE:
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _copy_fbo[1]);
			glFramebufferTexture(GL_DRAW_FRAMEBUFFER, destination_attachment, destination_object, 0);
			assert(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
			break;
		case GL_RENDERBUFFER:
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _copy_fbo[1]);
			glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, destination_attachment, GL_RENDERBUFFER, destination_object);
			assert(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
			break;
		case GL_FRAMEBUFFER_DEFAULT:
			assert(destination_object == destination_attachment);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
			break;
		}

		assert(source_attachment == destination_attachment);
		glBlitFramebuffer(
			0, 0, source_desc.width, source_desc.height,
			0, destination_desc.height, destination_desc.width, 0,
			source_attachment == GL_DEPTH_ATTACHMENT ? GL_DEPTH_BUFFER_BIT : GL_COLOR_BUFFER_BIT, GL_NEAREST);

		glBindFramebuffer(GL_READ_FRAMEBUFFER, prev_read_fbo);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev_draw_fbo);
	}
}

void reshade::opengl::device_impl::clear_depth_stencil_view(resource_view_handle dsv, uint32_t clear_flags, float depth, uint8_t stencil)
{
	assert((dsv.handle >> 40) == GL_DEPTH_ATTACHMENT);
	const GLuint fbo = dsv.handle & 0xFFFFFFFF;
	GLint stencil_value = stencil;

	if (gl3wProcs.gl.ClearNamedFramebufferfv != nullptr)
	{
		switch (clear_flags)
		{
		case 0x1:
			glClearNamedFramebufferfv(fbo, GL_DEPTH, 0, &depth);
			break;
		case 0x2:
			glClearNamedFramebufferiv(fbo, GL_STENCIL, 0, &stencil_value);
			break;
		case 0x3:
			glClearNamedFramebufferfi(fbo, GL_DEPTH_STENCIL, 0, depth, stencil_value);
			break;
		}
	}
	else
	{
		GLint prev_binding = 0;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_binding);
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);

		switch (clear_flags)
		{
		case 0x1:
			glClearBufferfv(GL_DEPTH, 0, &depth);
			break;
		case 0x2:
			glClearBufferiv(GL_STENCIL, 0, &stencil_value);
			break;
		case 0x3:
			glClearBufferfi(GL_DEPTH_STENCIL, 0, depth, stencil_value);
			break;
		}

		glBindFramebuffer(GL_FRAMEBUFFER, prev_binding);
	}
}
void reshade::opengl::device_impl::clear_render_target_view(resource_view_handle rtv, const float color[4])
{
	assert((rtv.handle >> 40) >= GL_COLOR_ATTACHMENT0 && (rtv.handle >> 40) <= GL_COLOR_ATTACHMENT31);
	const GLuint fbo = rtv.handle & 0xFFFFFFFF;
	const GLuint drawbuffer = (rtv.handle >> 40) - GL_COLOR_ATTACHMENT0;

	if (gl3wProcs.gl.ClearNamedFramebufferfv != nullptr)
	{
		glClearNamedFramebufferfv(fbo, GL_COLOR, drawbuffer, color);
	}
	else
	{
		GLint prev_binding = 0;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_binding);
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glClearBufferfv(GL_COLOR, drawbuffer, color);
		glBindFramebuffer(GL_FRAMEBUFFER, prev_binding);
	}
}
