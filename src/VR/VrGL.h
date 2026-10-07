#pragma once
/*
 * OXCE VR tabletop - small OpenGL helper layer.
 *
 * The engine creates a legacy/compatibility GL context through SDL 1.2.
 * We load every post-1.1 entry point we need ourselves (prefixed "vgl")
 * so we never collide with the engine's own OpenGL.cpp loader.
 */
#define NO_SDL_GLEXT
#include <SDL_opengl.h>
#include "khr/GL/glext.h"
#include <string>
#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

#ifndef GL_FRAMEBUFFER_SRGB
#define GL_FRAMEBUFFER_SRGB 0x8DB9
#endif
#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif

namespace OpenXcom
{
namespace VR
{

#define VGL_FUNCS(X) \
	X(PFNGLGENBUFFERSPROC, GenBuffers) \
	X(PFNGLDELETEBUFFERSPROC, DeleteBuffers) \
	X(PFNGLBINDBUFFERPROC, BindBuffer) \
	X(PFNGLBUFFERDATAPROC, BufferData) \
	X(PFNGLBUFFERSUBDATAPROC, BufferSubData) \
	X(PFNGLGENVERTEXARRAYSPROC, GenVertexArrays) \
	X(PFNGLDELETEVERTEXARRAYSPROC, DeleteVertexArrays) \
	X(PFNGLBINDVERTEXARRAYPROC, BindVertexArray) \
	X(PFNGLVERTEXATTRIBPOINTERPROC, VertexAttribPointer) \
	X(PFNGLENABLEVERTEXATTRIBARRAYPROC, EnableVertexAttribArray) \
	X(PFNGLCREATESHADERPROC, CreateShader) \
	X(PFNGLDELETESHADERPROC, DeleteShader) \
	X(PFNGLSHADERSOURCEPROC, ShaderSource) \
	X(PFNGLCOMPILESHADERPROC, CompileShader) \
	X(PFNGLGETSHADERIVPROC, GetShaderiv) \
	X(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog) \
	X(PFNGLCREATEPROGRAMPROC, CreateProgram) \
	X(PFNGLDELETEPROGRAMPROC, DeleteProgram) \
	X(PFNGLATTACHSHADERPROC, AttachShader) \
	X(PFNGLBINDATTRIBLOCATIONPROC, BindAttribLocation) \
	X(PFNGLLINKPROGRAMPROC, LinkProgram) \
	X(PFNGLGETPROGRAMIVPROC, GetProgramiv) \
	X(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog) \
	X(PFNGLUSEPROGRAMPROC, UseProgram) \
	X(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation) \
	X(PFNGLUNIFORM1IPROC, Uniform1i) \
	X(PFNGLUNIFORM1FPROC, Uniform1f) \
	X(PFNGLUNIFORM2FPROC, Uniform2f) \
	X(PFNGLUNIFORM3FPROC, Uniform3f) \
	X(PFNGLUNIFORM4FPROC, Uniform4f) \
	X(PFNGLUNIFORM3FVPROC, Uniform3fv) \
	X(PFNGLUNIFORMMATRIX4FVPROC, UniformMatrix4fv) \
	X(PFNGLACTIVETEXTUREPROC, ActiveTexture) \
	X(PFNGLGENERATEMIPMAPPROC, GenerateMipmap) \
	X(PFNGLGENFRAMEBUFFERSPROC, GenFramebuffers) \
	X(PFNGLDELETEFRAMEBUFFERSPROC, DeleteFramebuffers) \
	X(PFNGLBINDFRAMEBUFFERPROC, BindFramebuffer) \
	X(PFNGLFRAMEBUFFERTEXTURE2DPROC, FramebufferTexture2D) \
	X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, FramebufferRenderbuffer) \
	X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, CheckFramebufferStatus) \
	X(PFNGLGENRENDERBUFFERSPROC, GenRenderbuffers) \
	X(PFNGLDELETERENDERBUFFERSPROC, DeleteRenderbuffers) \
	X(PFNGLBINDRENDERBUFFERPROC, BindRenderbuffer) \
	X(PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC, RenderbufferStorageMultisample) \
	X(PFNGLBLITFRAMEBUFFERPROC, BlitFramebuffer)

struct GLFuncs
{
#define VGL_DECL(type, name) type name = nullptr;
	VGL_FUNCS(VGL_DECL)
#undef VGL_DECL
};

extern GLFuncs gl;

/// Loads all entry points. Needs a current GL context. Returns false (and logs) if any is missing.
bool loadGL();

/// Standard interleaved vertex used for all VR meshes.
struct Vertex
{
	glm::vec3 pos;
	glm::vec3 normal;
	glm::vec2 uv;
	glm::vec4 color;
	float material; // material id, interpreted by the fragment shader
};

/// CPU-side mesh builder.
struct MeshData
{
	std::vector<Vertex> verts;
	std::vector<uint32_t> indices;

	void clear() { verts.clear(); indices.clear(); }
	uint32_t addVertex(const Vertex &v) { verts.push_back(v); return (uint32_t)verts.size() - 1; }
	/// Quad from 4 corners (counter-clockwise when looking at the front face).
	void addQuad(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c, const glm::vec3 &d,
		const glm::vec4 &color, float material,
		glm::vec2 uva = {0, 0}, glm::vec2 uvb = {1, 0}, glm::vec2 uvc = {1, 1}, glm::vec2 uvd = {0, 1});
	/// Axis aligned box (min/max corners), all 6 faces pointing outward.
	void addBox(const glm::vec3 &mn, const glm::vec3 &mx, const glm::vec4 &color, float material, bool inward = false);
	/// Box transformed by a matrix.
	void addBox(const glm::mat4 &xf, const glm::vec3 &mn, const glm::vec3 &mx, const glm::vec4 &color, float material);
	/// Cylinder along +Y.
	void addCylinder(const glm::vec3 &base, float radius, float height, int segments, const glm::vec4 &color, float material, bool caps = true);
	/// UV sphere.
	void addSphere(const glm::vec3 &center, float radius, int slices, int stacks, const glm::vec4 &color, float material);
	void append(const MeshData &other, const glm::mat4 &xf);
};

/// GPU mesh.
class Mesh
{
public:
	Mesh() = default;
	~Mesh();
	Mesh(const Mesh &) = delete;
	Mesh &operator=(const Mesh &) = delete;
	void upload(const MeshData &data, bool dynamic = false);
	void draw(GLenum mode = GL_TRIANGLES) const;
	bool empty() const { return _count == 0; }
	void release();
private:
	GLuint _vao = 0, _vbo = 0, _ibo = 0;
	GLsizei _count = 0;
	size_t _vcap = 0, _icap = 0;
};

class Shader
{
public:
	bool build(const char *name, const char *vs, const char *fs);
	void use() const;
	GLint loc(const char *name) const;
	void set(const char *name, int v) const;
	void set(const char *name, float v) const;
	void set(const char *name, const glm::vec2 &v) const;
	void set(const char *name, const glm::vec3 &v) const;
	void set(const char *name, const glm::vec4 &v) const;
	void set(const char *name, const glm::mat4 &v) const;
	GLuint id() const { return _prog; }
private:
	GLuint _prog = 0;
};

/// RGBA8 (sRGB-encoded content) texture.
class Texture
{
public:
	~Texture();
	void create(int w, int h, bool mipmaps, bool linear = true);
	void update(const uint32_t *rgba, int w, int h);
	void bind(int unit) const;
	int width() const { return _w; }
	int height() const { return _h; }
	GLuint id() const { return _tex; }
	bool valid() const { return _tex != 0; }
private:
	GLuint _tex = 0;
	int _w = 0, _h = 0;
	bool _mips = false;
};

/// Multisampled offscreen target that resolves into a destination texture/FBO.
class RenderTarget
{
public:
	~RenderTarget();
	bool create(int w, int h, int samples);
	void bind() const;
	/// Resolve into the given texture (must be w*h).
	void resolveTo(GLuint dstTexture) const;
	/// Resolve into the default framebuffer.
	void resolveToDefault(int dstW, int dstH) const;
	int width() const { return _w; }
	int height() const { return _h; }
private:
	GLuint _fbo = 0, _color = 0, _depth = 0, _resolveFbo = 0;
	int _w = 0, _h = 0;
};

/// A depth-only render target for shadows (sampled as a plain depth texture).
class ShadowMap
{
public:
	~ShadowMap();
	bool create(int size);
	void bind() const;
	GLuint depthTex() const { return _tex; }
	int size() const { return _size; }
	bool valid() const { return _fbo != 0; }
private:
	GLuint _fbo = 0, _tex = 0;
	int _size = 0;
};

/// Saves and restores the GL state the engine's own presenter depends on.
class GLStateGuard
{
public:
	GLStateGuard();
	~GLStateGuard();
private:
	GLint _tex2d, _activeTex, _program, _fbo, _viewport[4], _vao, _arrayBuffer, _unpackRowLength;
	GLboolean _depth, _cull, _blend, _srgb;
};

void logGLErrors(const char *where);

}
}
