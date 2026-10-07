#include "VrGL.h"
#include "../Engine/Logger.h"
#include <SDL.h>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#ifndef GL_VERTEX_ARRAY_BINDING
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#endif
#ifndef GL_FRAMEBUFFER_BINDING
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#endif

namespace OpenXcom
{
namespace VR
{

GLFuncs gl;

bool loadGL()
{
	bool ok = true;
#define VGL_LOAD(type, name) \
	gl.name = (type)SDL_GL_GetProcAddress("gl" #name); \
	if (!gl.name) gl.name = (type)SDL_GL_GetProcAddress("gl" #name "EXT"); \
	if (!gl.name) { Log(LOG_ERROR) << "[VR] missing GL function gl" #name; ok = false; }
	VGL_FUNCS(VGL_LOAD)
#undef VGL_LOAD
	if (ok)
	{
		Log(LOG_INFO) << "[VR] GL_VERSION: " << (const char*)glGetString(GL_VERSION)
			<< " / GL_RENDERER: " << (const char*)glGetString(GL_RENDERER);
	}
	return ok;
}

void logGLErrors(const char *where)
{
	GLenum err;
	int n = 0;
	while ((err = glGetError()) != GL_NO_ERROR && n++ < 8)
	{
		Log(LOG_WARNING) << "[VR] GL error 0x" << std::hex << err << std::dec << " at " << where;
	}
}

// ---------------------------------------------------------------- MeshData

void MeshData::addQuad(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c, const glm::vec3 &d,
	const glm::vec4 &color, float material, glm::vec2 uva, glm::vec2 uvb, glm::vec2 uvc, glm::vec2 uvd)
{
	glm::vec3 n = glm::normalize(glm::cross(b - a, d - a));
	uint32_t base = (uint32_t)verts.size();
	verts.push_back({a, n, uva, color, material});
	verts.push_back({b, n, uvb, color, material});
	verts.push_back({c, n, uvc, color, material});
	verts.push_back({d, n, uvd, color, material});
	indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

void MeshData::addBox(const glm::vec3 &mn, const glm::vec3 &mx, const glm::vec4 &color, float material, bool inward)
{
	glm::vec3 p[8] = {
		{mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mx.y, mn.z}, {mn.x, mx.y, mn.z},
		{mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z},
	};
	// faces, CCW from outside
	int f[6][4] = {
		{4, 5, 6, 7}, // +z
		{1, 0, 3, 2}, // -z
		{5, 1, 2, 6}, // +x
		{0, 4, 7, 3}, // -x
		{7, 6, 2, 3}, // +y
		{0, 1, 5, 4}, // -y
	};
	for (auto &q : f)
	{
		if (inward)
			addQuad(p[q[3]], p[q[2]], p[q[1]], p[q[0]], color, material);
		else
			addQuad(p[q[0]], p[q[1]], p[q[2]], p[q[3]], color, material);
	}
}

void MeshData::addBox(const glm::mat4 &xf, const glm::vec3 &mn, const glm::vec3 &mx, const glm::vec4 &color, float material)
{
	MeshData tmp;
	tmp.addBox(mn, mx, color, material);
	append(tmp, xf);
}

void MeshData::addCylinder(const glm::vec3 &base, float radius, float height, int segments, const glm::vec4 &color, float material, bool caps)
{
	uint32_t start = (uint32_t)verts.size();
	for (int i = 0; i <= segments; ++i)
	{
		float a = (float)i / segments * 6.2831853f;
		glm::vec3 n(std::cos(a), 0, std::sin(a));
		verts.push_back({base + n * radius, n, {(float)i / segments, 0}, color, material});
		verts.push_back({base + n * radius + glm::vec3(0, height, 0), n, {(float)i / segments, 1}, color, material});
	}
	for (int i = 0; i < segments; ++i)
	{
		uint32_t a = start + i * 2;
		indices.insert(indices.end(), {a, a + 1, a + 3, a, a + 3, a + 2});
	}
	if (caps)
	{
		for (int top = 0; top < 2; ++top)
		{
			glm::vec3 n(0, top ? 1.f : -1.f, 0);
			glm::vec3 c = base + glm::vec3(0, top ? height : 0.f, 0);
			uint32_t ci = addVertex({c, n, {0.5f, 0.5f}, color, material});
			uint32_t first = (uint32_t)verts.size();
			for (int i = 0; i <= segments; ++i)
			{
				float a = (float)i / segments * 6.2831853f;
				verts.push_back({c + glm::vec3(std::cos(a), 0, std::sin(a)) * radius, n, {0, 0}, color, material});
			}
			for (int i = 0; i < segments; ++i)
			{
				if (top) indices.insert(indices.end(), {ci, first + i + 1, first + i});
				else indices.insert(indices.end(), {ci, first + i, first + i + 1});
			}
		}
	}
}

void MeshData::addSphere(const glm::vec3 &center, float radius, int slices, int stacks, const glm::vec4 &color, float material)
{
	uint32_t start = (uint32_t)verts.size();
	for (int j = 0; j <= stacks; ++j)
	{
		float v = (float)j / stacks;
		float phi = v * 3.14159265f;
		for (int i = 0; i <= slices; ++i)
		{
			float u = (float)i / slices;
			float th = u * 6.2831853f;
			glm::vec3 n(std::sin(phi) * std::cos(th), std::cos(phi), std::sin(phi) * std::sin(th));
			verts.push_back({center + n * radius, n, {u, v}, color, material});
		}
	}
	for (int j = 0; j < stacks; ++j)
		for (int i = 0; i < slices; ++i)
		{
			uint32_t a = start + j * (slices + 1) + i;
			uint32_t b = a + slices + 1;
			indices.insert(indices.end(), {a, a + 1, b, a + 1, b + 1, b});
		}
}

void MeshData::append(const MeshData &other, const glm::mat4 &xf)
{
	uint32_t base = (uint32_t)verts.size();
	glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(xf)));
	for (const auto &v : other.verts)
	{
		Vertex w = v;
		w.pos = glm::vec3(xf * glm::vec4(v.pos, 1));
		w.normal = glm::normalize(nm * v.normal);
		verts.push_back(w);
	}
	for (auto i : other.indices) indices.push_back(base + i);
}

// ---------------------------------------------------------------- Mesh

Mesh::~Mesh() { release(); }

void Mesh::release()
{
	if (_vao && gl.DeleteVertexArrays) gl.DeleteVertexArrays(1, &_vao);
	if (_vbo && gl.DeleteBuffers) gl.DeleteBuffers(1, &_vbo);
	if (_ibo && gl.DeleteBuffers) gl.DeleteBuffers(1, &_ibo);
	_vao = _vbo = _ibo = 0;
	_count = 0;
	_vcap = _icap = 0;
}

void Mesh::upload(const MeshData &data, bool dynamic)
{
	if (!_vao)
	{
		gl.GenVertexArrays(1, &_vao);
		gl.GenBuffers(1, &_vbo);
		gl.GenBuffers(1, &_ibo);
		gl.BindVertexArray(_vao);
		gl.BindBuffer(GL_ARRAY_BUFFER, _vbo);
		gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, _ibo);
		GLsizei stride = sizeof(Vertex);
		gl.EnableVertexAttribArray(0);
		gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, pos));
		gl.EnableVertexAttribArray(1);
		gl.VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, normal));
		gl.EnableVertexAttribArray(2);
		gl.VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, uv));
		gl.EnableVertexAttribArray(3);
		gl.VertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, color));
		gl.EnableVertexAttribArray(4);
		gl.VertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, material));
	}
	gl.BindVertexArray(_vao);
	gl.BindBuffer(GL_ARRAY_BUFFER, _vbo);
	size_t vbytes = data.verts.size() * sizeof(Vertex);
	size_t ibytes = data.indices.size() * sizeof(uint32_t);
	GLenum usage = dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
	if (vbytes > _vcap || !dynamic)
	{
		gl.BufferData(GL_ARRAY_BUFFER, vbytes, data.verts.data(), usage);
		_vcap = vbytes;
	}
	else if (vbytes)
	{
		gl.BufferSubData(GL_ARRAY_BUFFER, 0, vbytes, data.verts.data());
	}
	gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, _ibo);
	if (ibytes > _icap || !dynamic)
	{
		gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, ibytes, data.indices.data(), usage);
		_icap = ibytes;
	}
	else if (ibytes)
	{
		gl.BufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, ibytes, data.indices.data());
	}
	_count = (GLsizei)data.indices.size();
	gl.BindVertexArray(0);
}

void Mesh::draw(GLenum mode) const
{
	if (!_count) return;
	gl.BindVertexArray(_vao);
	glDrawElements(mode, _count, GL_UNSIGNED_INT, 0);
	gl.BindVertexArray(0);
}

// ---------------------------------------------------------------- Shader

static GLuint compileStage(const char *name, GLenum type, const char *src)
{
	GLuint s = gl.CreateShader(type);
	gl.ShaderSource(s, 1, &src, nullptr);
	gl.CompileShader(s);
	GLint ok = 0;
	gl.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok)
	{
		char log[4096];
		gl.GetShaderInfoLog(s, sizeof(log), nullptr, log);
		Log(LOG_ERROR) << "[VR] shader " << name << (type == GL_VERTEX_SHADER ? " (vs)" : " (fs)") << " compile failed:\n" << log;
		gl.DeleteShader(s);
		return 0;
	}
	return s;
}

bool Shader::build(const char *name, const char *vs, const char *fs)
{
	GLuint v = compileStage(name, GL_VERTEX_SHADER, vs);
	GLuint f = compileStage(name, GL_FRAGMENT_SHADER, fs);
	if (!v || !f) return false;
	_prog = gl.CreateProgram();
	gl.AttachShader(_prog, v);
	gl.AttachShader(_prog, f);
	gl.BindAttribLocation(_prog, 0, "aPos");
	gl.BindAttribLocation(_prog, 1, "aNormal");
	gl.BindAttribLocation(_prog, 2, "aUV");
	gl.BindAttribLocation(_prog, 3, "aColor");
	gl.BindAttribLocation(_prog, 4, "aMat");
	gl.LinkProgram(_prog);
	gl.DeleteShader(v);
	gl.DeleteShader(f);
	GLint ok = 0;
	gl.GetProgramiv(_prog, GL_LINK_STATUS, &ok);
	if (!ok)
	{
		char log[4096];
		gl.GetProgramInfoLog(_prog, sizeof(log), nullptr, log);
		Log(LOG_ERROR) << "[VR] shader " << name << " link failed:\n" << log;
		return false;
	}
	return true;
}

void Shader::use() const { gl.UseProgram(_prog); }
GLint Shader::loc(const char *name) const { return gl.GetUniformLocation(_prog, name); }
void Shader::set(const char *name, int v) const { gl.Uniform1i(loc(name), v); }
void Shader::set(const char *name, float v) const { gl.Uniform1f(loc(name), v); }
void Shader::set(const char *name, const glm::vec2 &v) const { gl.Uniform2f(loc(name), v.x, v.y); }
void Shader::set(const char *name, const glm::vec3 &v) const { gl.Uniform3f(loc(name), v.x, v.y, v.z); }
void Shader::set(const char *name, const glm::vec4 &v) const { gl.Uniform4f(loc(name), v.x, v.y, v.z, v.w); }
void Shader::set(const char *name, const glm::mat4 &v) const { gl.UniformMatrix4fv(loc(name), 1, GL_FALSE, glm::value_ptr(v)); }

// ---------------------------------------------------------------- Texture

Texture::~Texture()
{
	if (_tex) glDeleteTextures(1, &_tex);
}

void Texture::create(int w, int h, bool mipmaps, bool linear)
{
	if (_tex) glDeleteTextures(1, &_tex);
	_w = w; _h = h; _mips = mipmaps;
	glGenTextures(1, &_tex);
	glBindTexture(GL_TEXTURE_2D, _tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmaps ? GL_LINEAR_MIPMAP_LINEAR : (linear ? GL_LINEAR : GL_NEAREST));
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void Texture::update(const uint32_t *rgba, int w, int h)
{
	if (!_tex || w != _w || h != _h) create(w, h, _mips);
	glBindTexture(GL_TEXTURE_2D, _tex);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	if (_mips) gl.GenerateMipmap(GL_TEXTURE_2D);
}

void Texture::bind(int unit) const
{
	gl.ActiveTexture(GL_TEXTURE0 + unit);
	glBindTexture(GL_TEXTURE_2D, _tex);
}

// ---------------------------------------------------------------- RenderTarget

RenderTarget::~RenderTarget()
{
	if (!gl.DeleteFramebuffers) return;
	if (_fbo) gl.DeleteFramebuffers(1, &_fbo);
	if (_resolveFbo) gl.DeleteFramebuffers(1, &_resolveFbo);
	if (_color) gl.DeleteRenderbuffers(1, &_color);
	if (_depth) gl.DeleteRenderbuffers(1, &_depth);
}

bool RenderTarget::create(int w, int h, int samples)
{
	_w = w; _h = h;
	gl.GenFramebuffers(1, &_fbo);
	gl.GenFramebuffers(1, &_resolveFbo);
	gl.GenRenderbuffers(1, &_color);
	gl.GenRenderbuffers(1, &_depth);
	gl.BindRenderbuffer(GL_RENDERBUFFER, _color);
	gl.RenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_SRGB8_ALPHA8, w, h);
	gl.BindRenderbuffer(GL_RENDERBUFFER, _depth);
	gl.RenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, w, h);
	gl.BindFramebuffer(GL_FRAMEBUFFER, _fbo);
	gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, _color);
	gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, _depth);
	GLenum st = gl.CheckFramebufferStatus(GL_FRAMEBUFFER);
	gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
	if (st != GL_FRAMEBUFFER_COMPLETE)
	{
		Log(LOG_ERROR) << "[VR] render target incomplete: 0x" << std::hex << st;
		return false;
	}
	return true;
}

void RenderTarget::bind() const
{
	gl.BindFramebuffer(GL_FRAMEBUFFER, _fbo);
	glViewport(0, 0, _w, _h);
}

void RenderTarget::resolveTo(GLuint dstTexture) const
{
	gl.BindFramebuffer(GL_FRAMEBUFFER, _resolveFbo);
	gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dstTexture, 0);
	gl.BindFramebuffer(GL_READ_FRAMEBUFFER, _fbo);
	gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, _resolveFbo);
	gl.BlitFramebuffer(0, 0, _w, _h, 0, 0, _w, _h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderTarget::resolveToDefault(int dstW, int dstH) const
{
	gl.BindFramebuffer(GL_READ_FRAMEBUFFER, _fbo);
	gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	gl.BlitFramebuffer(0, 0, _w, _h, 0, 0, dstW, dstH, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ---------------------------------------------------------------- GLStateGuard

ShadowMap::~ShadowMap()
{
	if (_fbo) gl.DeleteFramebuffers(1, &_fbo);
	if (_tex) glDeleteTextures(1, &_tex);
}

bool ShadowMap::create(int size)
{
	_size = size;
	glGenTextures(1, &_tex);
	glBindTexture(GL_TEXTURE_2D, _tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size, size, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);
	gl.GenFramebuffers(1, &_fbo);
	gl.BindFramebuffer(GL_FRAMEBUFFER, _fbo);
	gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, _tex, 0);
	glDrawBuffer(GL_NONE);
	glReadBuffer(GL_NONE);
	GLenum st = gl.CheckFramebufferStatus(GL_FRAMEBUFFER);
	gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
	if (st != GL_FRAMEBUFFER_COMPLETE)
	{
		Log(LOG_ERROR) << "[VR] shadow map incomplete: 0x" << std::hex << st;
		gl.DeleteFramebuffers(1, &_fbo);
		_fbo = 0;
		return false;
	}
	return true;
}

void ShadowMap::bind() const
{
	gl.BindFramebuffer(GL_FRAMEBUFFER, _fbo);
	glViewport(0, 0, _size, _size);
}

GLStateGuard::GLStateGuard()
{
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &_tex2d);
	glGetIntegerv(GL_ACTIVE_TEXTURE, &_activeTex);
	glGetIntegerv(GL_CURRENT_PROGRAM, &_program);
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &_fbo);
	glGetIntegerv(GL_VIEWPORT, _viewport);
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &_vao);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &_arrayBuffer);
	glGetIntegerv(GL_UNPACK_ROW_LENGTH, &_unpackRowLength);
	_depth = glIsEnabled(GL_DEPTH_TEST);
	_cull = glIsEnabled(GL_CULL_FACE);
	_blend = glIsEnabled(GL_BLEND);
	_srgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);
}

GLStateGuard::~GLStateGuard()
{
	gl.BindFramebuffer(GL_FRAMEBUFFER, _fbo);
	glViewport(_viewport[0], _viewport[1], _viewport[2], _viewport[3]);
	gl.UseProgram(_program);
	gl.BindVertexArray(_vao);
	gl.BindBuffer(GL_ARRAY_BUFFER, _arrayBuffer);
	gl.ActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, _tex2d);
	gl.ActiveTexture(_activeTex);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, _unpackRowLength);
	if (_depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
	if (_cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
	if (_blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
	if (_srgb) glEnable(GL_FRAMEBUFFER_SRGB); else glDisable(GL_FRAMEBUFFER_SRGB);
	glDepthMask(GL_TRUE);
}

}
}
