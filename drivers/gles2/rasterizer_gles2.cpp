/*************************************************************************/
/*  rasterizer_gles2.cpp                                                 */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2018 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2018 Godot Engine contributors (cf. AUTHORS.md)    */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/
#include "rasterizer_gles2.h"

#include "gl_context/context_gl.h"
#include "os/os.h"
#include "project_settings.h"
#include <string.h>

#define _EXT_DEBUG_OUTPUT_SYNCHRONOUS_ARB 0x8242
#define _EXT_DEBUG_NEXT_LOGGED_MESSAGE_LENGTH_ARB 0x8243
#define _EXT_DEBUG_CALLBACK_FUNCTION_ARB 0x8244
#define _EXT_DEBUG_CALLBACK_USER_PARAM_ARB 0x8245
#define _EXT_DEBUG_SOURCE_API_ARB 0x8246
#define _EXT_DEBUG_SOURCE_WINDOW_SYSTEM_ARB 0x8247
#define _EXT_DEBUG_SOURCE_SHADER_COMPILER_ARB 0x8248
#define _EXT_DEBUG_SOURCE_THIRD_PARTY_ARB 0x8249
#define _EXT_DEBUG_SOURCE_APPLICATION_ARB 0x824A
#define _EXT_DEBUG_SOURCE_OTHER_ARB 0x824B
#define _EXT_DEBUG_TYPE_ERROR_ARB 0x824C
#define _EXT_DEBUG_TYPE_DEPRECATED_BEHAVIOR_ARB 0x824D
#define _EXT_DEBUG_TYPE_UNDEFINED_BEHAVIOR_ARB 0x824E
#define _EXT_DEBUG_TYPE_PORTABILITY_ARB 0x824F
#define _EXT_DEBUG_TYPE_PERFORMANCE_ARB 0x8250
#define _EXT_DEBUG_TYPE_OTHER_ARB 0x8251
#define _EXT_MAX_DEBUG_MESSAGE_LENGTH_ARB 0x9143
#define _EXT_MAX_DEBUG_LOGGED_MESSAGES_ARB 0x9144
#define _EXT_DEBUG_LOGGED_MESSAGES_ARB 0x9145
#define _EXT_DEBUG_SEVERITY_HIGH_ARB 0x9146
#define _EXT_DEBUG_SEVERITY_MEDIUM_ARB 0x9147
#define _EXT_DEBUG_SEVERITY_LOW_ARB 0x9148
#define _EXT_DEBUG_OUTPUT 0x92E0

#if (defined WINDOWS_ENABLED) && !(defined UWP_ENABLED)
#define GLAPIENTRY APIENTRY
#else
#define GLAPIENTRY
#endif

static void GLAPIENTRY _gl_debug_print(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *message, const GLvoid *userParam) {

	if (type == _EXT_DEBUG_TYPE_OTHER_ARB)
		return;

	if (type == _EXT_DEBUG_TYPE_PERFORMANCE_ARB)
		return; //these are ultimately annoying, so removing for now

	char debSource[256], debType[256], debSev[256];
	if (source == _EXT_DEBUG_SOURCE_API_ARB)
		strcpy(debSource, "OpenGL");
	else if (source == _EXT_DEBUG_SOURCE_WINDOW_SYSTEM_ARB)
		strcpy(debSource, "Windows");
	else if (source == _EXT_DEBUG_SOURCE_SHADER_COMPILER_ARB)
		strcpy(debSource, "Shader Compiler");
	else if (source == _EXT_DEBUG_SOURCE_THIRD_PARTY_ARB)
		strcpy(debSource, "Third Party");
	else if (source == _EXT_DEBUG_SOURCE_APPLICATION_ARB)
		strcpy(debSource, "Application");
	else if (source == _EXT_DEBUG_SOURCE_OTHER_ARB)
		strcpy(debSource, "Other");

	if (type == _EXT_DEBUG_TYPE_ERROR_ARB)
		strcpy(debType, "Error");
	else if (type == _EXT_DEBUG_TYPE_DEPRECATED_BEHAVIOR_ARB)
		strcpy(debType, "Deprecated behavior");
	else if (type == _EXT_DEBUG_TYPE_UNDEFINED_BEHAVIOR_ARB)
		strcpy(debType, "Undefined behavior");
	else if (type == _EXT_DEBUG_TYPE_PORTABILITY_ARB)
		strcpy(debType, "Portability");
	else if (type == _EXT_DEBUG_TYPE_PERFORMANCE_ARB)
		strcpy(debType, "Performance");
	else if (type == _EXT_DEBUG_TYPE_OTHER_ARB)
		strcpy(debType, "Other");

	if (severity == _EXT_DEBUG_SEVERITY_HIGH_ARB)
		strcpy(debSev, "High");
	else if (severity == _EXT_DEBUG_SEVERITY_MEDIUM_ARB)
		strcpy(debSev, "Medium");
	else if (severity == _EXT_DEBUG_SEVERITY_LOW_ARB)
		strcpy(debSev, "Low");

	String output = String() + "GL ERROR: Source: " + debSource + "\tType: " + debType + "\tID: " + itos(id) + "\tSeverity: " + debSev + "\tMessage: " + message;

	ERR_PRINTS(output);
}

typedef void (*DEBUGPROCARB)(GLenum source,
		GLenum type,
		GLuint id,
		GLenum severity,
		GLsizei length,
		const char *message,
		const void *userParam);

typedef void (*DebugMessageCallbackARB)(DEBUGPROCARB callback, const void *userParam);

RasterizerStorage *RasterizerGLES2::get_storage() {

	return storage;
}

RasterizerCanvas *RasterizerGLES2::get_canvas() {

	return canvas;
}

RasterizerScene *RasterizerGLES2::get_scene() {

	return scene;
}

void RasterizerGLES2::initialize() {

	if (OS::get_singleton()->is_stdout_verbose()) {
		print_line("Using GLES2 video driver");
	}

#ifdef GLAD_ENABLED
	if (!gladLoadGL()) {
		ERR_PRINT("Error initializing GLAD");
	}

// GLVersion seems to be used for both GL and GL ES, so we need different version checks for them
#ifdef OPENGL_ENABLED // OpenGL 2.1 Profile required
	if (GLVersion.major < 2) {
#else // OpenGL ES 3.0
	if (GLVersion.major < 2) {
#endif
		ERR_PRINT("Your system's graphic drivers seem not to support OpenGL 2.1 / OpenGL ES 2.0, sorry :(\n"
				  "Try a drivers update, buy a new GPU or try software rendering on Linux; Godot will now crash with a segmentation fault.");
		OS::get_singleton()->alert("Your system's graphic drivers seem not to support OpenGL 2.1 / OpenGL ES 2.0, sorry :(\n"
								   "Godot Engine will self-destruct as soon as you acknowledge this error message.",
				"Fatal error: Insufficient OpenGL / GLES driver support");
	}

#ifdef GLES_OVER_GL
	//Test GL_ARB_framebuffer_object extension
	if (!GLAD_GL_ARB_framebuffer_object) {
		//Try older GL_EXT_framebuffer_object extension
		if (GLAD_GL_EXT_framebuffer_object) {
			glIsRenderbuffer = glIsRenderbufferEXT;
			glBindRenderbuffer = glBindRenderbufferEXT;
			glDeleteRenderbuffers = glDeleteRenderbuffersEXT;
			glGenRenderbuffers = glGenRenderbuffersEXT;
			glRenderbufferStorage = glRenderbufferStorageEXT;
			glGetRenderbufferParameteriv = glGetRenderbufferParameterivEXT;
			glIsFramebuffer = glIsFramebufferEXT;
			glBindFramebuffer = glBindFramebufferEXT;
			glDeleteFramebuffers = glDeleteFramebuffersEXT;
			glGenFramebuffers = glGenFramebuffersEXT;
			glCheckFramebufferStatus = glCheckFramebufferStatusEXT;
			glFramebufferTexture1D = glFramebufferTexture1DEXT;
			glFramebufferTexture2D = glFramebufferTexture2DEXT;
			glFramebufferTexture3D = glFramebufferTexture3DEXT;
			glFramebufferRenderbuffer = glFramebufferRenderbufferEXT;
			glGetFramebufferAttachmentParameteriv = glGetFramebufferAttachmentParameterivEXT;
			glGenerateMipmap = glGenerateMipmapEXT;
		} else {
			ERR_PRINT("Your system's graphic drivers seem not to support GL_ARB(EXT)_framebuffer_object OpenGL extension, sorry :(\n"
					  "Try a drivers update, buy a new GPU or try software rendering on Linux; Godot will now crash with a segmentation fault.");
			OS::get_singleton()->alert("Your system's graphic drivers seem not to support GL_ARB(EXT)_framebuffer_object OpenGL extension, sorry :(\n"
									   "Godot Engine will self-destruct as soon as you acknowledge this error message.",
					"Fatal error: Insufficient OpenGL / GLES driver support");
		}
	}
#endif
	if (true || OS::get_singleton()->is_stdout_verbose()) {
		if (GLAD_GL_ARB_debug_output) {
			glEnable(_EXT_DEBUG_OUTPUT_SYNCHRONOUS_ARB);
			glDebugMessageCallbackARB(_gl_debug_print, NULL);
			glEnable(_EXT_DEBUG_OUTPUT);
		} else {
			print_line("OpenGL debugging not supported!");
		}
	}

#endif // GLAD_ENABLED

	// For debugging
#ifdef GLES_OVER_GL
	if (GLAD_GL_ARB_debug_output) {
		glDebugMessageControlARB(_EXT_DEBUG_SOURCE_API_ARB, _EXT_DEBUG_TYPE_ERROR_ARB, _EXT_DEBUG_SEVERITY_HIGH_ARB, 0, NULL, GL_TRUE);
		glDebugMessageControlARB(_EXT_DEBUG_SOURCE_API_ARB, _EXT_DEBUG_TYPE_DEPRECATED_BEHAVIOR_ARB, _EXT_DEBUG_SEVERITY_HIGH_ARB, 0, NULL, GL_TRUE);
		glDebugMessageControlARB(_EXT_DEBUG_SOURCE_API_ARB, _EXT_DEBUG_TYPE_UNDEFINED_BEHAVIOR_ARB, _EXT_DEBUG_SEVERITY_HIGH_ARB, 0, NULL, GL_TRUE);
		glDebugMessageControlARB(_EXT_DEBUG_SOURCE_API_ARB, _EXT_DEBUG_TYPE_PORTABILITY_ARB, _EXT_DEBUG_SEVERITY_HIGH_ARB, 0, NULL, GL_TRUE);
		glDebugMessageControlARB(_EXT_DEBUG_SOURCE_API_ARB, _EXT_DEBUG_TYPE_PERFORMANCE_ARB, _EXT_DEBUG_SEVERITY_HIGH_ARB, 0, NULL, GL_TRUE);
		glDebugMessageControlARB(_EXT_DEBUG_SOURCE_API_ARB, _EXT_DEBUG_TYPE_OTHER_ARB, _EXT_DEBUG_SEVERITY_HIGH_ARB, 0, NULL, GL_TRUE);
		/* glDebugMessageInsertARB(
			GL_DEBUG_SOURCE_API_ARB,
			GL_DEBUG_TYPE_OTHER_ARB, 1,
			GL_DEBUG_SEVERITY_HIGH_ARB, 5, "hello");
		*/
	}
#endif

	const GLubyte *renderer = glGetString(GL_RENDERER);
	print_line("OpenGL ES 2.0 Renderer: " + String((const char *)renderer));
	storage->initialize();
	canvas->initialize();
	scene->initialize();
}

void RasterizerGLES2::begin_frame(double frame_step) {
	time_total += frame_step;

	if (frame_step == 0) {
		//to avoid hiccups
		frame_step = 0.001;
	}

	// double time_roll_over = GLOBAL_GET("rendering/limits/time/time_rollover_secs");
	// if (time_total > time_roll_over)
	//	time_total = 0; //roll over every day (should be customz

	storage->frame.time[0] = time_total;
	storage->frame.time[1] = Math::fmod(time_total, 3600);
	storage->frame.time[2] = Math::fmod(time_total, 900);
	storage->frame.time[3] = Math::fmod(time_total, 60);
	storage->frame.count++;
	storage->frame.delta = frame_step;

	storage->update_dirty_resources();

	storage->info.render_final = storage->info.render;
	storage->info.render.reset();

	scene->iteration();
}

void RasterizerGLES2::set_current_render_target(RID p_render_target) {

	if (!p_render_target.is_valid() && storage->frame.current_rt && storage->frame.clear_request) {
		// pending clear request. Do that first.
		glBindFramebuffer(GL_FRAMEBUFFER, storage->frame.current_rt->fbo);
		glClearColor(storage->frame.clear_request_color.r,
				storage->frame.clear_request_color.g,
				storage->frame.clear_request_color.b,
				storage->frame.clear_request_color.a);
		glClear(GL_COLOR_BUFFER_BIT);
	}

	if (p_render_target.is_valid()) {
		RasterizerStorageGLES2::RenderTarget *rt = storage->render_target_owner.getornull(p_render_target);
		storage->frame.current_rt = rt;
		ERR_FAIL_COND(!rt);
		storage->frame.clear_request = false;

		glViewport(0, 0, rt->width, rt->height);
	} else {
		storage->frame.current_rt = NULL;
		storage->frame.clear_request = false;
		glViewport(0, 0, OS::get_singleton()->get_window_size().width, OS::get_singleton()->get_window_size().height);
		glBindFramebuffer(GL_FRAMEBUFFER, RasterizerStorageGLES2::system_fbo);
	}
}

void RasterizerGLES2::restore_render_target() {
	ERR_FAIL_COND(storage->frame.current_rt == NULL);
	RasterizerStorageGLES2::RenderTarget *rt = storage->frame.current_rt;
	glBindFramebuffer(GL_FRAMEBUFFER, rt->fbo);
	glViewport(0, 0, rt->width, rt->height);
}

void RasterizerGLES2::clear_render_target(const Color &p_color) {
	ERR_FAIL_COND(!storage->frame.current_rt);

	storage->frame.clear_request = true;
	storage->frame.clear_request_color = p_color;
}

void RasterizerGLES2::set_boot_image(const Ref<Image> &p_image, const Color &p_color, bool p_scale) {

	if (p_image.is_null() || p_image->empty())
		return;

	int window_w = OS::get_singleton()->get_video_mode(0).width;
	int window_h = OS::get_singleton()->get_video_mode(0).height;

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, window_w, window_h);
	glDisable(GL_BLEND);
	glDepthMask(GL_FALSE);
	if (OS::get_singleton()->get_window_per_pixel_transparency_enabled()) {
		glClearColor(0.0, 0.0, 0.0, 0.0);
	} else {
		glClearColor(p_color.r, p_color.g, p_color.b, 1.0);
	}
	glClear(GL_COLOR_BUFFER_BIT);

	canvas->canvas_begin();

	RID texture = storage->texture_create();
	storage->texture_allocate(texture, p_image->get_width(), p_image->get_height(), 0, p_image->get_format(), VS::TEXTURE_TYPE_2D, VS::TEXTURE_FLAG_FILTER);
	storage->texture_set_data(texture, p_image);

	Rect2 imgrect(0, 0, p_image->get_width(), p_image->get_height());
	Rect2 screenrect;

	screenrect = imgrect;
	screenrect.position += ((Size2(window_w, window_h) - screenrect.size) / 2.0).floor();

	RasterizerStorageGLES2::Texture *t = storage->texture_owner.get(texture);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, t->tex_id);
	canvas->draw_generic_textured_rect(screenrect, Rect2(0, 0, 1, 1));
	glBindTexture(GL_TEXTURE_2D, 0);
	canvas->canvas_end();

	storage->free(texture);

	end_frame(true);
}

void RasterizerGLES2::blit_render_target_to_screen(RID p_render_target, const Rect2 &p_screen_rect, int p_screen) {

	ERR_FAIL_COND(storage->frame.current_rt);

	RasterizerStorageGLES2::RenderTarget *rt = storage->render_target_owner.getornull(p_render_target);
	ERR_FAIL_COND(!rt);

	canvas->state.canvas_shader.set_custom_shader(0);
	canvas->state.canvas_shader.bind();

	canvas->canvas_begin();
	glDisable(GL_BLEND);
	glBindFramebuffer(GL_FRAMEBUFFER, RasterizerStorageGLES2::system_fbo);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, rt->color);

	// TODO normals

	canvas->draw_generic_textured_rect(p_screen_rect, Rect2(0, 1, 1, -1));

	glBindTexture(GL_TEXTURE_2D, 0);
	canvas->canvas_end();
}

void RasterizerGLES2::end_frame(bool p_swap_buffers) {

	if (OS::get_singleton()->is_layered_allowed()) {
		if (OS::get_singleton()->get_window_per_pixel_transparency_enabled()) {
#if (defined WINDOWS_ENABLED) && !(defined UWP_ENABLED)
			Size2 wndsize = OS::get_singleton()->get_layered_buffer_size();
			uint8_t *data = OS::get_singleton()->get_layered_buffer_data();
			if (data) {
				glReadPixels(0, 0, wndsize.x, wndsize.y, GL_BGRA, GL_UNSIGNED_BYTE, data);
				OS::get_singleton()->swap_layered_buffer();

				return;
			}
#endif
		} else {
			//clear alpha
			glColorMask(false, false, false, true);
			glClearColor(0, 0, 0, 1);
			glClear(GL_COLOR_BUFFER_BIT);
			glColorMask(true, true, true, true);
		}
	}

	if (p_swap_buffers)
		OS::get_singleton()->swap_buffers();
	else
		glFinish();
}

void RasterizerGLES2::finalize() {
}

Rasterizer *RasterizerGLES2::_create_current() {

	return memnew(RasterizerGLES2);
}

void RasterizerGLES2::make_current() {
	_create_func = _create_current;
}

void RasterizerGLES2::register_config() {
}

RasterizerGLES2::RasterizerGLES2() {

	storage = memnew(RasterizerStorageGLES2);
	canvas = memnew(RasterizerCanvasGLES2);
	scene = memnew(RasterizerSceneGLES2);
	canvas->storage = storage;
	canvas->scene_render = scene;
	storage->canvas = canvas;
	scene->storage = storage;
	storage->scene = scene;

	time_total = 0;
}

RasterizerGLES2::~RasterizerGLES2() {

	memdelete(storage);
	memdelete(canvas);
}
