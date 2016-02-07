// license:BSD-3-Clause
// copyright-holders:Miodrag Milanovic, Dario Manesku, Branimir Karadzic
//============================================================
//
//  drawbgfx.c - BGFX drawer
//
//============================================================
#define __STDC_LIMIT_MACROS
#define __STDC_FORMAT_MACROS
#define __STDC_CONSTANT_MACROS

#if defined(SDLMAME_WIN32) || defined(OSD_WINDOWS)
// standard windows headers
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#if defined(SDLMAME_WIN32)
#if (SDLMAME_SDL2)
#include <SDL2/SDL_syswm.h>
#else
#include <SDL/SDL_syswm.h>
#endif
#endif
#else
#include "sdlinc.h"
#endif

// MAMEOS headers
#include "emu.h"
#include "window.h"

#include <bgfx/bgfxplatform.h>
#include <bgfx/bgfx.h>
#include <bx/fpumath.h>
#include <bx/readerwriter.h>

//============================================================
//  DEBUGGING
//============================================================

//============================================================
//  CONSTANTS
//============================================================

//============================================================
//  MACROS
//============================================================

//============================================================
//  INLINES
//============================================================


//============================================================
//  TYPES
//============================================================


/* sdl_info is the information about SDL for the current screen */
class renderer_bgfx : public osd_renderer
{
public:
	renderer_bgfx(osd_window *w)
	: osd_renderer(w, FLAG_NONE), m_blittimer(0),
		m_blit_dim(0, 0),
		m_last_hofs(0), m_last_vofs(0),
		m_last_blit_time(0), m_last_blit_pixels(0)
	{}

	virtual int create() override;
	virtual int draw(const int update) override;
#ifdef OSD_SDL
	virtual int xy_to_render_target(const int x, const int y, int *xt, int *yt) override;
#else
	virtual void save() override { }
	virtual void record() override { }
	virtual void toggle_fsfx() override { }
#endif
	virtual void destroy() override;
	virtual render_primitive_list *get_primitives() override
	{
#ifdef OSD_WINDOWS
		RECT client;
		GetClientRect(window().m_hwnd, &client);
		window().target()->set_bounds(rect_width(&client), rect_height(&client), window().aspect());
		return &window().target()->get_primitives();
#else
		osd_dim nd = window().blit_surface_size();
		if (nd != m_blit_dim)
		{
			m_blit_dim = nd;
			notify_changed();
		}
		window().target()->set_bounds(m_blit_dim.width(), m_blit_dim.height(), window().aspect());
		return &window().target()->get_primitives();
#endif
	}

	// void render_quad(texture_info *texture, const render_primitive *prim, const int x, const int y);

	//texture_info *texture_find(const render_primitive &prim, const quad_setup_data &setup);
	//texture_info *texture_update(const render_primitive &prim);

	INT32           m_blittimer;

	//simple_list<texture_info>  m_texlist;                // list of active textures

	osd_dim         m_blit_dim;
	float           m_last_hofs;
	float           m_last_vofs;

	// Stats
	INT64           m_last_blit_time;
	INT64           m_last_blit_pixels;

	bgfx::ProgramHandle m_progQuad;
	bgfx::ProgramHandle m_progQuadTexture;
	bgfx::ProgramHandle m_progLine;
	bgfx::UniformHandle m_s_texColor;

	// Original display_mode
};


//============================================================
//  PROTOTYPES
//============================================================

// core functions
static void drawbgfx_exit(void);

//============================================================
//  drawnone_create
//============================================================

static osd_renderer *drawbgfx_create(osd_window *window)
{
	return global_alloc(renderer_bgfx(window));
}

//============================================================
//  drawbgfx_init
//============================================================

int drawbgfx_init(running_machine &machine, osd_draw_callbacks *callbacks)
{
	// fill in the callbacks
	//memset(callbacks, 0, sizeof(*callbacks));
	callbacks->exit = drawbgfx_exit;
	callbacks->create = drawbgfx_create;

	return 0;
}

//============================================================
//  drawbgfx_exit
//============================================================

static void drawbgfx_exit(void)
{
}

//============================================================
//  renderer_bgfx::create
//============================================================
bgfx::ProgramHandle loadProgram(const char* _vsName, const char* _fsName);
int renderer_bgfx::create()
{
	// create renderer

#ifdef OSD_WINDOWS
	RECT client;
	GetClientRect(window().m_hwnd, &client);

	bgfx::winSetHwnd(window().m_hwnd);
	bgfx::init();
	bgfx::reset(rect_width(&client), rect_height(&client), BGFX_RESET_VSYNC);
#else
	osd_dim d = window().get_size();
	m_blittimer = 3;

	bgfx::sdlSetWindow(window().sdl_window());
	bgfx::init();
	bgfx::reset(d.width(), d.height(), BGFX_RESET_VSYNC);
#endif

	// Enable debug text.
	bgfx::setDebug(BGFX_DEBUG_TEXT); //BGFX_DEBUG_STATS
	// Create program from shaders.
	m_progQuad = loadProgram("vs_quad", "fs_quad");
	m_progQuadTexture = loadProgram("vs_quad_texture", "fs_quad_texture");
	m_progLine = loadProgram("vs_line", "fs_line");
	m_s_texColor = bgfx::createUniform("s_texColor", bgfx::UniformType::Int1);

	osd_printf_verbose("Leave drawsdl2_window_create\n");
	return 0;
}

//============================================================
//  drawbgfx_window_destroy
//============================================================

void renderer_bgfx::destroy()
{
	// free the memory in the window

	// destroy_all_textures();
	// 
	bgfx::destroyUniform(m_s_texColor);
	// Cleanup.
	bgfx::destroyProgram(m_progQuad);
	bgfx::destroyProgram(m_progQuadTexture);
	bgfx::destroyProgram(m_progLine);

	// Shutdown bgfx.
	bgfx::shutdown();
}


//============================================================
//  drawsdl_xy_to_render_target
//============================================================

#ifdef OSD_SDL
int renderer_bgfx::xy_to_render_target(int x, int y, int *xt, int *yt)
{
	*xt = x - m_last_hofs;
	*yt = y - m_last_vofs;
	if (*xt<0 || *xt >= m_blit_dim.width())
		return 0;
	if (*yt<0 || *yt >= m_blit_dim.height())
		return 0;
	return 1;
}
#endif

static const bgfx::Memory* loadMem(bx::FileReaderI* _reader, const char* _filePath)
{
	if (bx::open(_reader, _filePath))
	{
		uint32_t size = (uint32_t)bx::getSize(_reader);
		const bgfx::Memory* mem = bgfx::alloc(size + 1);
		bx::read(_reader, mem->data, size);
		bx::close(_reader);
		mem->data[mem->size - 1] = '\0';
		return mem;
	}

	return NULL;
}
static bgfx::ShaderHandle loadShader(bx::FileReaderI* _reader, const char* _name)
{
	char filePath[512];

	const char* shaderPath = "shaders/dx9/";

	switch (bgfx::getRendererType())
	{
	case bgfx::RendererType::Direct3D11:
	case bgfx::RendererType::Direct3D12:
		shaderPath = "shaders/dx11/";
		break;

	case bgfx::RendererType::OpenGL:
		shaderPath = "shaders/glsl/";
		break;

	case bgfx::RendererType::Metal:
		shaderPath = "shaders/metal/";
		break;

	case bgfx::RendererType::OpenGLES:
		shaderPath = "shaders/gles/";
		break;

	default:
		break;
	}

	strcpy(filePath, shaderPath);
	strcat(filePath, _name);
	strcat(filePath, ".bin");

	return bgfx::createShader(loadMem(_reader, filePath));
}

bgfx::ProgramHandle loadProgram(bx::FileReaderI* _reader, const char* _vsName, const char* _fsName)
{
	bgfx::ShaderHandle vsh = loadShader(_reader, _vsName);
	bgfx::ShaderHandle fsh = BGFX_INVALID_HANDLE;
	if (NULL != _fsName)
	{
		fsh = loadShader(_reader, _fsName);
	}

	return bgfx::createProgram(vsh, fsh, true /* destroy shaders when program is destroyed */);
}
static auto s_fileReader = new bx::CrtFileReader;

bgfx::ProgramHandle loadProgram(const char* _vsName, const char* _fsName)
{
	
	return loadProgram(s_fileReader, _vsName, _fsName);
}
//============================================================
//  drawbgfx_window_draw
//============================================================

struct PosColorTexCoord0Vertex
{
	float m_x;
	float m_y;
	float m_z;
	uint32_t m_rgba;
	float m_u;
	float m_v;

	static void init()
	{
		ms_decl.begin()
			.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
			.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
			.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
			.end();
	}

	static bgfx::VertexDecl ms_decl;
};
bgfx::VertexDecl PosColorTexCoord0Vertex::ms_decl;

void screenQuad(float _x1
	, float _y1
	, float _x2
	, float _y2
	, uint32_t _abgr
	, render_quad_texuv uv
	)
{
	if (bgfx::checkAvailTransientVertexBuffer(6, PosColorTexCoord0Vertex::ms_decl))
	{
		bgfx::TransientVertexBuffer vb;
		bgfx::allocTransientVertexBuffer(&vb, 6, PosColorTexCoord0Vertex::ms_decl);
		PosColorTexCoord0Vertex* vertex = (PosColorTexCoord0Vertex*)vb.data;

		const float minx = _x1;
		const float miny = _y1;
		const float maxx = _x2;
		const float maxy = _y2;
		const float zz = 0.0f;

		vertex[0].m_x = minx;
		vertex[0].m_y = miny;
		vertex[0].m_z = zz;
		vertex[0].m_rgba = _abgr;
		vertex[0].m_u = uv.tl.u;
		vertex[0].m_v = uv.tl.v;

		vertex[1].m_x = maxx;
		vertex[1].m_y = miny;
		vertex[1].m_z = zz;
		vertex[1].m_rgba = _abgr;
		vertex[1].m_u = uv.tr.u;
		vertex[1].m_v = uv.tr.v;

		vertex[2].m_x = maxx;
		vertex[2].m_y = maxy;
		vertex[2].m_z = zz;
		vertex[2].m_rgba = _abgr;
		vertex[2].m_u = uv.br.u;
		vertex[2].m_v = uv.br.v;

		vertex[3].m_x = maxx;
		vertex[3].m_y = maxy;
		vertex[3].m_z = zz;
		vertex[3].m_rgba = _abgr;
		vertex[3].m_u = uv.br.u;
		vertex[3].m_v = uv.br.v;

		vertex[4].m_x = minx;
		vertex[4].m_y = maxy;
		vertex[4].m_z = zz;
		vertex[4].m_rgba = _abgr;
		vertex[4].m_u = uv.bl.u;
		vertex[4].m_v = uv.bl.v;

		vertex[5].m_x = minx;
		vertex[5].m_y = miny;
		vertex[5].m_z = zz;
		vertex[5].m_rgba = _abgr;
		vertex[5].m_u = uv.tl.u;
		vertex[5].m_v = uv.tl.v;
		bgfx::setVertexBuffer(&vb);
	}
}


struct PosColorVertex
{
	float m_x;
	float m_y;
	uint32_t m_abgr;

	static void init()
	{
		ms_decl
			.begin()
			.add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
			.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
			.end();
	}

	static bgfx::VertexDecl ms_decl;
};
bgfx::VertexDecl PosColorVertex::ms_decl;

#define MAX_TEMP_COORDS 100

void drawPolygon(const float* _coords, uint32_t _numCoords, float _r, uint32_t _abgr)
{
	float tempCoords[MAX_TEMP_COORDS * 2];
	float tempNormals[MAX_TEMP_COORDS * 2];

	_numCoords = _numCoords < MAX_TEMP_COORDS ? _numCoords : MAX_TEMP_COORDS;

	for (uint32_t ii = 0, jj = _numCoords - 1; ii < _numCoords; jj = ii++)
	{
		const float* v0 = &_coords[jj * 2];
		const float* v1 = &_coords[ii * 2];
		float dx = v1[0] - v0[0];
		float dy = v1[1] - v0[1];
		float d = sqrtf(dx * dx + dy * dy);
		if (d > 0)
		{
			d = 1.0f / d;
			dx *= d;
			dy *= d;
		}

		tempNormals[jj * 2 + 0] = dy;
		tempNormals[jj * 2 + 1] = -dx;
	}

	for (uint32_t ii = 0, jj = _numCoords - 1; ii < _numCoords; jj = ii++)
	{
		float dlx0 = tempNormals[jj * 2 + 0];
		float dly0 = tempNormals[jj * 2 + 1];
		float dlx1 = tempNormals[ii * 2 + 0];
		float dly1 = tempNormals[ii * 2 + 1];
		float dmx = (dlx0 + dlx1) * 0.5f;
		float dmy = (dly0 + dly1) * 0.5f;
		float dmr2 = dmx * dmx + dmy * dmy;
		if (dmr2 > 0.000001f)
		{
			float scale = 1.0f / dmr2;
			if (scale > 10.0f)
			{
				scale = 10.0f;
			}

			dmx *= scale;
			dmy *= scale;
		}

		tempCoords[ii * 2 + 0] = _coords[ii * 2 + 0] + dmx * _r;
		tempCoords[ii * 2 + 1] = _coords[ii * 2 + 1] + dmy * _r;
	}

	uint32_t numVertices = _numCoords * 6 + (_numCoords - 2) * 3;
	if (bgfx::checkAvailTransientVertexBuffer(numVertices, PosColorVertex::ms_decl))
	{
		bgfx::TransientVertexBuffer tvb;
		bgfx::allocTransientVertexBuffer(&tvb, numVertices, PosColorVertex::ms_decl);
		uint32_t trans = _abgr & 0xffffff;

		PosColorVertex* vertex = (PosColorVertex*)tvb.data;
		for (uint32_t ii = 0, jj = _numCoords - 1; ii < _numCoords; jj = ii++)
		{
			vertex->m_x = _coords[ii * 2 + 0];
			vertex->m_y = _coords[ii * 2 + 1];
			vertex->m_abgr = _abgr;
			++vertex;

			vertex->m_x = _coords[jj * 2 + 0];
			vertex->m_y = _coords[jj * 2 + 1];
			vertex->m_abgr = _abgr;
			++vertex;

			vertex->m_x = tempCoords[jj * 2 + 0];
			vertex->m_y = tempCoords[jj * 2 + 1];
			vertex->m_abgr = trans;
			++vertex;

			vertex->m_x = tempCoords[jj * 2 + 0];
			vertex->m_y = tempCoords[jj * 2 + 1];
			vertex->m_abgr = trans;
			++vertex;

			vertex->m_x = tempCoords[ii * 2 + 0];
			vertex->m_y = tempCoords[ii * 2 + 1];
			vertex->m_abgr = trans;
			++vertex;

			vertex->m_x = _coords[ii * 2 + 0];
			vertex->m_y = _coords[ii * 2 + 1];
			vertex->m_abgr = _abgr;
			++vertex;
		}

		for (uint32_t ii = 2; ii < _numCoords; ++ii)
		{
			vertex->m_x = _coords[0];
			vertex->m_y = _coords[1];
			vertex->m_abgr = _abgr;
			++vertex;

			vertex->m_x = _coords[(ii - 1) * 2 + 0];
			vertex->m_y = _coords[(ii - 1) * 2 + 1];
			vertex->m_abgr = _abgr;
			++vertex;

			vertex->m_x = _coords[ii * 2 + 0];
			vertex->m_y = _coords[ii * 2 + 1];
			vertex->m_abgr = _abgr;
			++vertex;
		}

		bgfx::setVertexBuffer(&tvb);
	}
}

void drawLine(float _x0, float _y0, float _x1, float _y1, float _r, uint32_t _abgr, float _fth = 1.0f)
{
	float dx = _x1 - _x0;
	float dy = _y1 - _y0;
	float d = sqrtf(dx * dx + dy * dy);
	if (d > 0.0001f)
	{
		d = 1.0f / d;
		dx *= d;
		dy *= d;
	}

	float nx = dy;
	float ny = -dx;
	float verts[4 * 2];
	_r -= _fth;
	_r *= 0.5f;
	if (_r < 0.01f)
	{
		_r = 0.01f;
	}

	dx *= _r;
	dy *= _r;
	nx *= _r;
	ny *= _r;

	verts[0] = _x0 - dx - nx;
	verts[1] = _y0 - dy - ny;

	verts[2] = _x0 - dx + nx;
	verts[3] = _y0 - dy + ny;

	verts[4] = _x1 + dx + nx;
	verts[5] = _y1 + dy + ny;

	verts[6] = _x1 + dx - nx;
	verts[7] = _y1 + dy - ny;

	drawPolygon(verts, 4, _fth, _abgr);
}

void initVertexDecls()
{
	PosColorTexCoord0Vertex::init();
	PosColorVertex::init();
}

static inline
uint32_t u32Color(uint8_t _r, uint8_t _g, uint8_t _b, uint8_t _a = 255)
{
	return 0
		| (uint32_t(_r) << 0)
		| (uint32_t(_g) << 8)
		| (uint32_t(_b) << 16)
		| (uint32_t(_a) << 24)
		;
}

int renderer_bgfx::draw(int update)
{
	initVertexDecls();

	// Set view 0 default viewport.
	int width, height;
#ifdef OSD_WINDOWS
	RECT client;
	GetClientRect(window().m_hwnd, &client);
	width = rect_width(&client);
	height = rect_height(&client);
#else
	width = m_blit_dim.width();
	height = m_blit_dim.height();
#endif
	bgfx::setViewRect(0, 0, 0, width, height);
	bgfx::reset(width, height, BGFX_RESET_VSYNC);
	// Setup view transform.
	{
		float view[16];
		bx::mtxIdentity(view);

		float left = 0.0f;
		float top = 0.0f;
		float right = width;
		float bottom = height;
		float proj[16];
		bx::mtxOrtho(proj, left, right, bottom, top, 0.0f, 100.0f);
		bgfx::setViewTransform(0, view, proj);
	}
	bgfx::setViewClear(0
		, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH
		, 0x000000ff
		, 1.0f
		, 0
		);

	// This dummy draw call is here to make sure that view 0 is cleared
	// if no other draw calls are submitted to view 0.
	bgfx::touch(0);

	window().m_primlist->acquire_lock();

	// Draw quad.
	// now draw
	for (render_primitive *prim = window().m_primlist->first(); prim != NULL; prim = prim->next())
	{
		uint64_t flags = BGFX_STATE_RGB_WRITE;
		switch (prim->flags & PRIMFLAG_BLENDMODE_MASK)
		{
			case PRIMFLAG_BLENDMODE(BLENDMODE_NONE):
				break;
			case PRIMFLAG_BLENDMODE(BLENDMODE_ALPHA):
				flags |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
				break;
			case PRIMFLAG_BLENDMODE(BLENDMODE_RGB_MULTIPLY):
				flags |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_DST_COLOR, BGFX_STATE_BLEND_ZERO);
				break;
			case PRIMFLAG_BLENDMODE(BLENDMODE_ADD):
				flags |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);
		}

		switch (prim->type)
		{
			/**
			 * Try to stay in one Begin/End block as long as possible,
			 * since entering and leaving one is most expensive..
			 */
			case render_primitive::LINE:
			
				drawLine(prim->bounds.x0, prim->bounds.y0, prim->bounds.x1, prim->bounds.y1,
					1.0f,
					u32Color(prim->color.r * 255, prim->color.g * 255, prim->color.b * 255, prim->color.a * 255),
					1.0f);
				bgfx::setState(flags);
				bgfx::submit(0, m_progLine);
				break;

			case render_primitive::QUAD:
				if (prim->texture.base == nullptr) {
					render_quad_texuv uv;
					uv.tl.u = uv.tl.v = uv.tr.u = uv.tr.v = 0;
					uv.bl.u = uv.bl.v = uv.br.u = uv.br.v = 0;					
					screenQuad(prim->bounds.x0, prim->bounds.y0,prim->bounds.x1, prim->bounds.y1,
						u32Color(prim->color.r * 255, prim->color.g * 255, prim->color.b * 255, prim->color.a * 255),uv);
					bgfx::setState(flags);
					bgfx::submit(0, m_progQuad);
				} else {
					screenQuad(prim->bounds.x0, prim->bounds.y0, prim->bounds.x1, prim->bounds.y1,
						0xFFFFFFFF,prim->texcoords);
					bgfx::TextureHandle m_texture;					
					// render based on the texture coordinates
					switch (prim->flags & PRIMFLAG_TEXFORMAT_MASK)
					{
					case PRIMFLAG_TEXFORMAT(TEXFORMAT_PALETTEA16):
					case PRIMFLAG_TEXFORMAT(TEXFORMAT_PALETTE16):
						{
							auto mem = bgfx::alloc(prim->texture.width*prim->texture.height * 4);
							
							int y, x;

							for (y = 0; y < prim->texture.height; y++)
							{
								unsigned char *pARGB32 = (unsigned char *)prim->texture.base + y*prim->texture.rowpixels*2;
								unsigned char *pRGBA8 = (unsigned char *)mem->data + y*prim->texture.width * 4;
								for (x = 0; x < prim->texture.width*2; x+=2)
								{
									pRGBA8[x*2 + 0] = prim->texture.palette[pARGB32[x + 0] + pARGB32[x + 1] * 256].r();
									pRGBA8[x*2 + 1] = prim->texture.palette[pARGB32[x + 0] + pARGB32[x + 1] * 256].g();
									pRGBA8[x*2 + 2] = prim->texture.palette[pARGB32[x + 0] + pARGB32[x + 1] * 256].b();
									pRGBA8[x*2 + 3] = prim->texture.palette[pARGB32[x + 0] + pARGB32[x + 1] * 256].a();
								}
							}
							m_texture = bgfx::createTexture2D((uint16_t)prim->texture.width
								, (uint16_t)prim->texture.height
								, 1
								, bgfx::TextureFormat::RGBA8
								, 0
								, mem
								);
						} 
						break;
						case PRIMFLAG_TEXFORMAT(TEXFORMAT_YUY16):
							break;
						case PRIMFLAG_TEXFORMAT(TEXFORMAT_RGB32):
						case PRIMFLAG_TEXFORMAT(TEXFORMAT_ARGB32):
						{
	
							auto mem = bgfx::alloc(prim->texture.width*prim->texture.height * 4);
							int y, x;

							for (y = 0; y < prim->texture.height; y++)
							{
								unsigned char *pARGB32 = (unsigned char *)prim->texture.base + y*prim->texture.rowpixels*4;
								unsigned char *pRGBA8 = (unsigned char *)mem->data + y*prim->texture.width *4;
								for (x = 0; x < prim->texture.width *4; x+=4 )
								{
									pRGBA8[x] =   pARGB32[x+2];
									pRGBA8[x+1] = pARGB32[x+1];
									pRGBA8[x+2] = pARGB32[x+0];
									pRGBA8[x + 3] = pARGB32[x + 3];
								}
							}
							m_texture = bgfx::createTexture2D((uint16_t)prim->texture.width
								, (uint16_t)prim->texture.height
								, 1
								, bgfx::TextureFormat::RGBA8
								, 0
								, mem
								);
						}
							break;

						default:
							break;
					}
					bgfx::setTexture(0, m_s_texColor, m_texture);
					bgfx::setState(flags);
					bgfx::submit(0, m_progQuadTexture);
					bgfx::destroyTexture(m_texture);
				}
				break;

			default:
				throw emu_fatalerror("Unexpected render_primitive type");
		}
	}

	window().m_primlist->release_lock();
	// Advance to next frame. Rendering thread will be kicked to
	// process submitted rendering primitives.
	bgfx::frame();

	return 0;
}
