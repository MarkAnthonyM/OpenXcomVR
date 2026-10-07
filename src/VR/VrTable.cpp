#include "VrGL.h" // first: GL extension headers
#include "VrTable.h"
#include "VrRoom.h"
#include "VrBoard.h"
#include "../Engine/Game.h"
#include "../Engine/State.h"
#include "../Engine/Screen.h"
#include "../Engine/Surface.h"
#include "../Engine/SurfaceSet.h"
#include "../Engine/Sound.h"
#include "../Engine/Font.h"
#include "../Engine/Language.h"
#include "../Engine/Logger.h"
#include "../Engine/Options.h"
#include "../Engine/InteractiveSurface.h"
#include "../Interface/Text.h"
#include "../Interface/Cursor.h"
#include "../Interface/FpsCounter.h"
#include "../Mod/Mod.h"
#include "../Mod/RuleItem.h"
#include "../Mod/RuleInventory.h"
#include "../Mod/RuleInterface.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/BattleItem.h"
#include "../Savegame/Tile.h"
#include "../Battlescape/BattlescapeState.h"
#include "../Battlescape/BattlescapeGame.h"
#include "../Battlescape/TileEngine.h"
#include "../Battlescape/Inventory.h"
#include "../Battlescape/UnitInfoState.h"
#include "../Battlescape/MiniMapView.h"
#include "../Battlescape/Map.h"
#include "../Battlescape/Camera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <map>
#include <cmath>
#include <algorithm>
#include <sstream>

namespace OpenXcom
{
namespace VR
{

static const float TRAVEL = 0.0055f;       // button stroke
static const float CAP_PROUD = 0.0065f;    // how far a raised cap stands above the table
static const float CAVITY = 0.045f;        // depth of a button well
static const int ID_TABLE = 1, ID_RIM = 2, ID_BUTTON = 100;

static inline uint32_t rgba(const SDL_Color &c, uint8_t a = 255)
{
	return (uint32_t)c.r | ((uint32_t)c.g << 8) | ((uint32_t)c.b << 16) | ((uint32_t)a << 24);
}
static float smooth01(float e0, float e1, float x)
{
	float t = glm::clamp((x - e0) / (e1 - e0), 0.f, 1.f);
	return t * t * (3.f - 2.f * t);
}

// ------------------------------------------------------------------ data

struct Btn
{
	std::string name;
	glm::ivec4 px;              // x, y, w, h inside the icon panel
	glm::vec4 rect{0.f};        // world xz rect of the well
	float open = 0.f, wait = 0.f, delay = 0.f;
	float depress = 0.f;
	bool latched = false;
	float laserTimer = 0.f;
	bool visible = true;
	bool target = false;
	int pressHand = -1;
};

struct ItemView
{
	BattleItem *item = nullptr;
	glm::vec4 foot{0.f};        // footprint in inventory pixels (x, y, w, h)
	glm::vec2 sprite{0.f};      // sprite top-left in inventory pixels
	const Surface *frame = nullptr;
};

struct Held
{
	BattleItem *item = nullptr;
	int hand = -1;              // 0/1 hands, 2 = laser/mouse
	glm::vec3 offset{0.f};      // sprite origin minus grab point (world)
	glm::vec3 origin{0.f};      // current sprite origin (world)
	glm::vec2 footDelta{0.f};   // footprint top-left minus sprite top-left (pixels)
	glm::vec2 footSize{0.f};
};

/// One inventory section (hand, belt, ground, ...) as laid out on the tray. The game's
/// layout is tall; on the tray the sections are repacked into a short row, so each one
/// has its own offset from game inventory pixels to tray pixels.
struct InvSection
{
	const RuleInventory *r = nullptr;
	glm::vec4 game{0.f};        // slot area in game inventory pixels (x, y, w, h)
	glm::vec2 off{0.f};         // tray pixels = game pixels + off
	glm::vec2 labelAt{0.f};     // label top-left in tray pixels
	int labelRow = 0, labelW = 0;
};

struct Wall
{
	glm::vec3 center, right, normal;
	float w, h;
	Texture tex;
	std::vector<uint32_t> px;
	int tw = 0, th = 0;
	bool live = false;
	float timer = 0.f;
};

struct Table::Impl
{
	RoomLayout L;
	float sy = 0.875f;          // table surface height
	glm::vec4 table{0.f};       // whole playing surface (xz)
	glm::vec4 map{0.f};
	glm::vec4 bay{0.f};         // icon panel area (xz)
	float bayScale = 0.005f;    // meters per icon panel pixel
	glm::vec4 tray{0.f};
	glm::vec4 invBox{0.f, 0.f, 320.f, 200.f}; // inventory pixels shown (x0, y0, x1, y1)
	float invScale = 0.0016f;
	glm::vec2 invOrigin{0.f};   // world xz of invBox's top-left
	std::vector<Btn> btns;
	std::vector<glm::vec4> holes;

	Game *game = nullptr;
	Board *board = nullptr;
	BattlescapeState *bs = nullptr;
	SavedBattleGame *battle = nullptr;
	bool inBattle = false, bsTop = false;
	glm::ivec4 icons{0, 144, 320, 56};

	Mesh quadUp, quadFront, box, cavity;
	Texture panelTex;
	bool panelValid = false;
	std::vector<uint32_t> panelPx;
	Texture gridTex;
	SavedBattleGame *gridFor = nullptr;
	std::map<const Surface*, std::unique_ptr<Mesh>> itemMeshes;
	std::vector<ItemView> items;
	BattleUnit *invUnit = nullptr;
	std::vector<InvSection> secs;
	Texture labelTex;
	static const int GROUND_COLS = 5;
	Held held;
	int hoverItem = -1;
	double time = 0.0;

	Texture warnTex;
	float warnTimer = 0.f;

	// taps
	bool touching[2] = {false, false};
	BattleUnit *headInside[2] = {nullptr, nullptr};
	float cooldown[2] = {0.f, 0.f};
	std::string lastEvent;

	// wall screens
	Wall walls[3];
	UnitInfoState *stats = nullptr;
	BattleUnit *statsUnit = nullptr;
	MiniMapView *minimap = nullptr;
	SavedBattleGame *minimapFor = nullptr;

	glm::vec3 bayWorld(float px, float py, float y) const { return {bay.x + px * bayScale, y, bay.y + py * bayScale}; }
	// The weapon buttons sit at opposite ends of the game's panel. On the table the right-hand one
	// moves next to the left-hand one (both close to the player); the panel picture and the button
	// wells are shifted to match, while clicks still go to the game's real button rectangles.
	int moveFrom = 0, moveTo = 0, insertAt = -1; // panel columns [moveFrom, moveTo) go to insertAt
	int visX(int c) const
	{
		if (insertAt < 0) return c;
		int w = moveTo - moveFrom;
		if (c >= moveFrom && c < moveTo) return insertAt + (c - moveFrom);
		if (c >= insertAt && c < moveFrom) return c + w;
		return c;
	}
	void updateRemap();
	const InvSection *section(const RuleInventory *r) const
	{
		for (const InvSection &s : secs) if (s.r == r) return &s;
		return nullptr;
	}
	glm::vec2 offOf(const RuleInventory *r) const { const InvSection *s = section(r); return s ? s->off : glm::vec2(0.f); }
	bool locate(glm::vec2 footTL, glm::vec2 footSize, const RuleInventory *&slot, int &x, int &y) const;
	glm::vec2 invToWorld(glm::vec2 p) const { return invOrigin + (p - glm::vec2(invBox.x, invBox.y)) * invScale; }
	glm::vec2 worldToInv(glm::vec2 w) const { return (w - invOrigin) / invScale + glm::vec2(invBox.x, invBox.y); }
	float itemY(int i) const { return sy + 0.024f + 0.003f * (float)std::sin(time * 2.0 + i * 1.7); }
	float capTop(const Btn &b) const
	{
		float rise = smooth01(0.4f, 1.f, b.open);
		return glm::mix(sy - CAVITY + 0.004f, sy + CAP_PROUD, rise) - b.depress;
	}
	bool usable(const Btn &b) const { return inBattle && bsTop && b.visible && b.open > 0.97f; }

	void layoutButtons();
	void syncBattle(TableContext &ctx);
	void updateButtons(TableContext &ctx, const Hands &hands, float dt);
	void pressButton(int i, int hand, TableContext &ctx);
	void buildGrid(TableContext &ctx);
	void listItems(TableContext &ctx);
	const Mesh *itemMesh(const Surface *frame);
	glm::vec3 itemOrigin(int i) const;
	void updateItems(TableContext &ctx, const Hands &hands);
	void grab(int i, int hand, const glm::vec3 &at);
	void drop(TableContext &ctx);
	void warn(TableContext &ctx, const std::string &id);
	void updateTaps(TableContext &ctx, const Hands &hands, float dt);
	void updateWalls(TableContext &ctx, float dt);
	void drawWall(const Shader &sh, const Wall &w) const;
};

Table::Table() : _p(new Impl) {}
Table::~Table()
{
	delete _p->stats;
	delete _p->minimap;
}

// ------------------------------------------------------------------ layout

void Table::init(const RoomLayout &layout)
{
	Impl &p = *_p;
	p.L = layout;
	glm::vec3 T = layout.tableCenter;
	float hx = layout.tableSize.x * 0.5f, hz = layout.tableSize.y * 0.5f;
	p.sy = T.y - 0.025f;
	p.table = {T.x - hx, T.z - hz, T.x + hx, T.z + hz};
	// The near strip of the table holds the controls, sized for hands and grouped in front of the
	// player (who starts at x = 0): the inventory tray left of centre, the button bay right of it,
	// both about as deep as the bay. The map gets the rest of the table.
	const float BAY_PX = 0.002f;                 // 2 mm per panel pixel: a main button is ~6.4 x 3.2 cm
	const float bd0 = 56.f * BAY_PX;
	const float STRIP = bd0 + 0.03f;
	float stripFar = p.table.w - STRIP;
	p.map = {p.table.x, p.table.y, p.table.z, stripFar - 0.01f};
	float bcz = (stripFar + p.table.w) * 0.5f;
	float bx0 = 0.01f, bx1 = std::min(bx0 + 320.f * BAY_PX, p.table.z - 0.03f);
	p.bayScale = std::min(BAY_PX, (bx1 - bx0) / 320.f);
	float bw = 320.f * p.bayScale, bd = 56.f * p.bayScale;
	p.bay = {bx0, bcz - bd * 0.5f, bx0 + bw, bcz + bd * 0.5f};
	// inventory tray: as deep as the bay, up to 70 cm wide, ending just left of centre
	p.tray = {std::max(p.table.x + 0.03f, -0.72f), bcz - bd0 * 0.5f - 0.004f, -0.02f, bcz + bd0 * 0.5f + 0.004f};

	// default panel layout (BattlescapeState), replaced by the live one in a battle
	struct D { const char *n; int x, y, w, h; };
	static const D defs[] = {
		{"unitUp", 48, 0, 32, 16}, {"unitDown", 48, 16, 32, 16}, {"mapUp", 80, 0, 32, 16}, {"mapDown", 80, 16, 32, 16},
		{"showMap", 112, 0, 32, 16}, {"kneel", 112, 16, 32, 16}, {"inventory", 144, 0, 32, 16}, {"center", 144, 16, 32, 16},
		{"nextSoldier", 176, 0, 32, 16}, {"nextStop", 176, 16, 32, 16}, {"showLayers", 208, 0, 32, 16}, {"help", 208, 16, 32, 16},
		{"endTurn", 240, 0, 32, 16}, {"abort", 240, 16, 32, 16},
		{"reserveNone", 60, 33, 17, 11}, {"reserveSnap", 78, 33, 17, 11}, {"reserveAimed", 60, 45, 17, 11}, {"reserveAuto", 78, 45, 17, 11},
		{"reserveKneel", 96, 33, 10, 23}, {"zeroTUs", 49, 33, 10, 23}, {"leftHand", 8, 4, 32, 48}, {"rightHand", 280, 4, 32, 48},
	};
	for (const D &d : defs)
	{
		Btn b;
		b.name = d.n;
		b.px = {d.x, d.y, d.w, d.h};
		p.btns.push_back(b);
	}
	p.layoutButtons();

	// meshes
	MeshData q;
	q.addQuad({-0.5f, 0, 0.5f}, {0.5f, 0, 0.5f}, {0.5f, 0, -0.5f}, {-0.5f, 0, -0.5f}, glm::vec4(1), MAT_PLAIN, {0, 1}, {1, 1}, {1, 0}, {0, 0});
	p.quadUp.upload(q);
	MeshData f;
	f.addQuad({-0.5f, -0.5f, 0}, {0.5f, -0.5f, 0}, {0.5f, 0.5f, 0}, {-0.5f, 0.5f, 0}, glm::vec4(1), MAT_PLAIN, {0, 1}, {1, 1}, {1, 0}, {0, 0});
	p.quadFront.upload(f);
	MeshData b;
	b.addBox({-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}, glm::vec4(1), MAT_GLOSSY);
	p.box.upload(b);
	// a well: four walls and a floor, faces pointing inward, open at the top (y 0 .. -1)
	MeshData c;
	glm::vec4 dark(0.05f, 0.055f, 0.065f, 1.f);
	c.addQuad({-0.5f, -1, 0.5f}, {0.5f, -1, 0.5f}, {0.5f, -1, -0.5f}, {-0.5f, -1, -0.5f}, dark, MAT_PLAIN);
	c.addQuad({-0.5f, -1, -0.5f}, {0.5f, -1, -0.5f}, {0.5f, 0, -0.5f}, {-0.5f, 0, -0.5f}, dark, MAT_PLAIN);
	c.addQuad({0.5f, -1, 0.5f}, {-0.5f, -1, 0.5f}, {-0.5f, 0, 0.5f}, {0.5f, 0, 0.5f}, dark, MAT_PLAIN);
	c.addQuad({-0.5f, -1, 0.5f}, {-0.5f, -1, -0.5f}, {-0.5f, 0, -0.5f}, {-0.5f, 0, 0.5f}, dark, MAT_PLAIN);
	c.addQuad({0.5f, -1, -0.5f}, {0.5f, -1, 0.5f}, {0.5f, 0, 0.5f}, {0.5f, 0, -0.5f}, dark, MAT_PLAIN);
	p.cavity.upload(c);

	// wall screens, in front of the decorative displays the room already has
	p.walls[0] = {{-4.972f, 1.8f, -3.2f}, {0, 0, -1}, {1, 0, 0}, 2.08f, 1.3f};      // left: soldier stats
	p.walls[1] = {{4.972f, 1.8f, -3.2f}, {0, 0, 1}, {-1, 0, 0}, 1.3f, 1.3f};        // right: minimap
	p.walls[2] = {{-2.15f, 2.25f, -6.972f}, {1, 0, 0}, {0, 0, 1}, 1.6f, 1.0f};      // back: squad roster
}

void Table::Impl::updateRemap()
{
	const Btn *L = nullptr, *R = nullptr;
	for (const Btn &b : btns)
	{
		if (b.name == "leftHand") L = &b;
		if (b.name == "rightHand") R = &b;
	}
	insertAt = -1;
	if (!L || !R) return;
	int from = std::max(0, R->px.x - 4), to = std::min(icons.z, R->px.x + R->px.z + 4);
	int at = L->px.x + L->px.z + 4;
	// only when the right hand really is to the right of everything else in the way
	if (at >= from || to <= from) return;
	for (const Btn &b : btns)
		if (&b != R && b.px.x < to && b.px.x + b.px.z > from) return; // something else shares those columns
	moveFrom = from; moveTo = to; insertAt = at;
}

void Table::Impl::layoutButtons()
{
	updateRemap();
	float cx = (bay.x + bay.z) * 0.5f;
	float halfW = (bay.z - bay.x) * 0.5f;
	for (Btn &b : btns)
	{
		float vx = (float)visX(b.px.x);
		glm::vec3 a = bayWorld(vx, (float)b.px.y, sy);
		glm::vec3 c = bayWorld(vx + b.px.z, (float)(b.px.y + b.px.w), sy);
		const float m = 0.0025f; // a little frame between neighbouring wells
		b.rect = {a.x + m, a.z + m, c.x - m, c.z - m};
		// hatches open as a wave from the middle outward
		b.delay = 0.45f * std::fabs((b.rect.x + b.rect.z) * 0.5f - cx) / halfW;
	}
}

glm::vec4 Table::mapRect() const { return _p->map; }
float Table::surfaceY() const { return _p->sy; }
const std::vector<glm::vec4> &Table::holes() const { return _p->holes; }

// ------------------------------------------------------------------ battle state

void Table::Impl::syncBattle(TableContext &ctx)
{
	game = ctx.game;
	board = ctx.board;
	bs = board ? board->battleState() : nullptr;
	battle = board ? board->battle() : nullptr;
	inBattle = bs && battle;
	bsTop = inBattle && !game->getStates().empty() && game->getStates().back() == (State*)bs;
	if (!inBattle) return;

	// live panel layout and visibility
	InteractiveSurface *ic = bs->vrIcons();
	icons = {ic->getX(), ic->getY(), ic->getWidth(), ic->getHeight()};
	bool moved = false;
	for (const auto &pair : bs->vrButtons())
	{
		for (Btn &b : btns)
		{
			if (b.name != pair.first || !pair.second) continue;
			glm::ivec4 r(pair.second->getX() - icons.x, pair.second->getY() - icons.y, pair.second->getWidth(), pair.second->getHeight());
			if (r != b.px) { b.px = r; moved = true; }
			b.visible = pair.second->getVisible();
		}
	}
	if (moved) layoutButtons();

	// copy the icon panel out of the game frame while the battlescape is on top
	if (bsTop && ctx.pixels && ctx.surfW >= icons.x + icons.z && ctx.surfH >= icons.y + icons.w)
	{
		panelPx.resize((size_t)icons.z * icons.w);
		for (int y = 0; y < icons.w; ++y)
		{
			const uint32_t *src = &(*ctx.pixels)[(size_t)(icons.y + y) * ctx.surfW + icons.x];
			uint32_t *dst = &panelPx[(size_t)y * icons.z];
			for (int x = 0; x < icons.z; ++x) dst[visX(x)] = src[x];
		}
		if (!panelTex.valid() || panelTex.width() != icons.z) panelTex.create(icons.z, icons.w, false, true);
		panelTex.update(panelPx.data(), icons.z, icons.w);
		panelValid = true;
	}
}

// ------------------------------------------------------------------ buttons

void Table::Impl::pressButton(int i, int hand, TableContext &ctx)
{
	Btn &b = btns[i];
	if (!usable(b)) return;
	int gx = icons.x + b.px.x + b.px.z / 2, gy = icons.y + b.px.y + b.px.w / 2;
	ctx.clickScreen(gx, gy, SDL_BUTTON_LEFT);
	if (hand >= 0 && hand < 2) ctx.haptic(hand, 0.7f, 0.025f);
	if (battle)
		if (Sound *s = game->getMod()->getSoundByDepth(battle->getDepth(), Mod::BUTTON_PRESS)) s->play();
	lastEvent = "button " + b.name;
	Log(LOG_INFO) << "[VR] table button pressed: " << b.name;
}

void Table::Impl::updateButtons(TableContext &ctx, const Hands &hands, float dt)
{
	holes.clear();
	for (size_t i = 0; i < btns.size(); ++i)
	{
		Btn &b = btns[i];
		bool want = inBattle && b.visible;
		if (want && !b.target) b.wait = b.delay;
		b.target = want;
		if (want)
		{
			if (b.wait > 0.f) b.wait -= dt;
			else b.open = std::min(1.f, b.open + dt / 1.1f);
		}
		else
		{
			b.open = std::max(0.f, b.open - dt / 0.6f);
		}
		if (b.open > 0.01f) holes.push_back(b.rect);

		// how far the tracked fingers push the cap down (the cap follows instantly, springs back)
		float rest = sy + CAP_PROUD;
		float want_d = 0.f;
		int byHand = -1;
		if (b.open > 0.97f)
		{
			for (int h = 0; h < 2; ++h)
			{
				const HandPose &P = hands.pose[h];
				if (!P.valid || P.ghost) continue;
				for (int j = 3; j < HAND_JOINTS; ++j)
				{
					glm::vec3 c = P.raw[j];
					float r = P.radius[j];
					if (c.x < b.rect.x - r * 0.3f || c.x > b.rect.z + r * 0.3f || c.z < b.rect.y - r * 0.3f || c.z > b.rect.w + r * 0.3f) continue;
					float bottom = c.y - r;
					if (bottom >= rest || c.y < rest - 0.06f) continue;
					float d = rest - bottom;
					if (d > want_d) { want_d = d; byHand = h; }
				}
			}
		}
		if (b.laserTimer > 0.f) { b.laserTimer -= dt; want_d = TRAVEL; }
		want_d = std::min(want_d, TRAVEL);
		if (want_d > b.depress) { b.depress = want_d; if (byHand >= 0) b.pressHand = byHand; }
		else b.depress = std::max(want_d, b.depress - 0.12f * dt);

		// click near the bottom of the stroke, release half way back up
		if (!b.latched && b.depress > TRAVEL * 0.62f)
		{
			b.latched = true;
			if (b.laserTimer <= 0.f) pressButton((int)i, b.pressHand, ctx);
		}
		else if (b.latched && b.depress < TRAVEL * 0.3f)
		{
			b.latched = false;
			if (b.pressHand >= 0) ctx.haptic(b.pressHand, 0.25f, 0.012f);
			b.pressHand = -1;
		}
	}
}

// ------------------------------------------------------------------ inventory

void Table::Impl::buildGrid(TableContext &ctx)
{
	gridFor = battle;
	secs.clear();
	if (!bs) return;
	Mod *mod = ctx.game->getMod();
	SDL_Color *pal = bs->getPalette();
	// the game's own inventory widget draws the grid (labels are drawn separately below)
	Inventory inv(ctx.game, 320, 200, 0, 0, false);
	inv.setPalette(pal);
	inv.drawGrid();
	Surface off(320, 200);
	off.setPalette(pal);
	off.clear();
	inv.blit(off.getSurface());
	std::vector<uint32_t> px(320 * 200);
	for (int y = 0; y < 200; ++y)
		for (int x = 0; x < 320; ++x)
		{
			Uint8 c = off.getPixel(x, y);
			px[(size_t)y * 320 + x] = c ? rgba(pal[c]) : 0u;
		}
	gridTex.create(320, 200, true, true);
	gridTex.update(px.data(), 320, 200);

	// sections and their slot areas, in game inventory pixels
	for (const std::string &name : mod->getInvsList())
	{
		const RuleInventory *r = mod->getInventory(name, true);
		if (!r) continue;
		InvSection sec;
		sec.r = r;
		if (r->getType() == INV_HAND) sec.game = {(float)r->getX(), (float)r->getY(), 32.f, 48.f};
		else if (r->getType() == INV_GROUND) sec.game = {(float)r->getX(), (float)r->getY(), GROUND_COLS * 16.f, 48.f};
		else
		{
			glm::vec4 a(1e9f, 1e9f, -1e9f, -1e9f);
			for (const RuleSlot &sl : *r->getSlots())
			{
				float x = (float)(r->getX() + sl.x * RuleInventory::SLOT_W), y = (float)(r->getY() + sl.y * RuleInventory::SLOT_H);
				a = {std::min(a.x, x), std::min(a.y, y), std::max(a.z, x + 16.f), std::max(a.w, y + 16.f)};
			}
			if (a.x > a.z) continue;
			sec.game = {a.x, a.y, a.z - a.x, a.w - a.y};
		}
		secs.push_back(sec);
	}

	// labels in the game's slot-name style, one row each in a small atlas
	const int LW = 96, LH = 9;
	Text text(LW, LH, 0, 0);
	text.setPalette(pal);
	text.initText(mod->getFont("FONT_BIG"), mod->getFont("FONT_SMALL"), ctx.game->getLanguage());
	if (RuleInterface *ri = mod->getInterface("inventory", false))
		if (const Element *el = ri->getElement("textSlots")) text.setColor(el->color);
	text.setHighContrast(true);
	std::vector<uint32_t> lpx((size_t)LW * LH * std::max<size_t>(1, secs.size()), 0u);
	for (size_t i = 0; i < secs.size(); ++i)
	{
		text.clear();
		text.setText(ctx.game->getLanguage()->getString(secs[i].r->getId()).arg(1).arg(1));
		text.draw();
		secs[i].labelW = std::min(LW, text.getTextWidth());
		secs[i].labelRow = (int)i;
		for (int y = 0; y < LH; ++y)
			for (int x = 0; x < LW; ++x)
			{
				Uint8 c = text.getPixel(x, y);
				if (c) lpx[(i * LH + y) * LW + x] = rgba(pal[c]);
			}
	}
	labelTex.create(LW, LH * std::max<int>(1, (int)secs.size()), true, true);
	labelTex.update(lpx.data(), LW, LH * std::max<int>(1, (int)secs.size()));

	// Repack into one short row: tall sections get a column each, sections one slot high are
	// stacked two to a column. Order follows the game's left-to-right layout, ground last.
	const float GAP = 8.f, ROWH = LH + 48.f; // gap wide enough that neighbouring labels read apart
	std::vector<int> order;
	for (int i = 0; i < (int)secs.size(); ++i) order.push_back(i);
	std::stable_sort(order.begin(), order.end(), [&](int a, int b)
	{
		bool ga = secs[a].r->getType() == INV_GROUND, gb = secs[b].r->getType() == INV_GROUND;
		if (ga != gb) return gb;
		if (secs[a].game.x != secs[b].game.x) return secs[a].game.x < secs[b].game.x;
		return secs[a].game.y < secs[b].game.y;
	});
	struct Col { std::vector<int> members; float w = 0.f, h = 0.f; bool shortCol = false; };
	std::vector<Col> cols;
	int openShort = -1;
	for (int i : order)
	{
		InvSection &sc = secs[i];
		float bw = std::max(sc.game.z, (float)sc.labelW), bh = LH + sc.game.w;
		bool isShort = sc.game.w <= 16.f && sc.r->getType() != INV_GROUND;
		if (isShort && openShort >= 0 && cols[openShort].h + bh <= ROWH + 0.5f)
		{
			Col &c = cols[openShort];
			c.members.push_back(i);
			c.w = std::max(c.w, bw);
			c.h += bh;
			continue;
		}
		Col c;
		c.members.push_back(i);
		c.w = bw;
		c.h = bh;
		c.shortCol = isShort;
		cols.push_back(c);
		if (isShort) openShort = (int)cols.size() - 1;
	}
	// The tray ends just left of centre, so the last column is the one nearest the player:
	// put the columns in reverse, hands (used most) nearest, the ground farthest.
	std::reverse(cols.begin(), cols.end());
	float x = 0.f;
	for (Col &c : cols)
	{
		// stacked short sections: the one higher up in the game goes on top
		std::sort(c.members.begin(), c.members.end(), [&](int a, int b) { return secs[a].game.y < secs[b].game.y; });
		float y = 0.f;
		for (int i : c.members)
		{
			InvSection &sc = secs[i];
			sc.labelAt = {x, y};
			sc.off = glm::vec2(x, y + LH) - glm::vec2(sc.game.x, sc.game.y);
			y += LH + sc.game.w;
		}
		x += c.w + GAP;
	}
	float totalW = std::max(1.f, x - GAP), totalH = ROWH;
	invBox = {0.f, 0.f, totalW, totalH};
	// fit into the tray, kept against its centre side (nearest the player)
	float tw = tray.z - tray.x, td = tray.w - tray.y;
	invScale = std::min(tw / totalW, td / totalH);
	float uw = totalW * invScale, ud = totalH * invScale;
	invOrigin = {tray.z - uw, tray.y + (td - ud) * 0.5f};
	Log(LOG_INFO) << "[VR] inventory tray: " << secs.size() << " sections, " << totalW << "x" << totalH
		<< " px, cell " << (16.f * invScale * 100.f) << " cm";
}

bool Table::Impl::locate(glm::vec2 footTL, glm::vec2 footSize, const RuleInventory *&slot, int &x, int &y) const
{
	// probe the centre of the first cell, then the middle of the footprint
	glm::vec2 probes[2] = {footTL + glm::vec2(8.f), footTL + footSize * 0.5f};
	for (const glm::vec2 &pr : probes)
		for (const InvSection &sc : secs)
		{
			glm::vec2 tl = glm::vec2(sc.game.x, sc.game.y) + sc.off;
			if (pr.x < tl.x || pr.y < tl.y || pr.x >= tl.x + sc.game.z || pr.y >= tl.y + sc.game.w) continue;
			glm::vec2 g = pr - sc.off;
			int px = (int)std::floor(g.x), py = (int)std::floor(g.y);
			if (sc.r->getType() == INV_GROUND) { slot = sc.r; x = 0; y = 0; return true; }
			if (sc.r->checkSlotInPosition(&px, &py)) { slot = sc.r; x = px; y = py; return true; }
		}
	return false;
}

void Table::Impl::listItems(TableContext &ctx)
{
	items.clear();
	invUnit = nullptr;
	if (!inBattle) return;
	BattleUnit *u = battle->getSelectedUnit();
	if (!u || u->isOut() || u->getFaction() != FACTION_PLAYER) return;
	invUnit = u;
	SurfaceSet *set = ctx.game->getMod()->getSurfaceSet("BIGOBS.PCK");
	int anim = (int)(time * 8.0) % 8;
	for (BattleItem *it : *u->getInventory())
	{
		const RuleInventory *slot = it->getSlot();
		if (!slot) continue;
		ItemView v;
		v.item = it;
		const RuleItem *r = it->getRules();
		glm::vec2 o = offOf(slot); // game inventory pixels -> tray pixels
		if (slot->getType() == INV_HAND)
		{
			v.foot = {slot->getX() + o.x, slot->getY() + o.y, 32.f, 48.f};
			v.sprite = {slot->getX() + r->getHandSpriteOffX() + o.x, slot->getY() + r->getHandSpriteOffY() + o.y};
		}
		else
		{
			float x = (float)(slot->getX() + it->getSlotX() * RuleInventory::SLOT_W) + o.x, y = (float)(slot->getY() + it->getSlotY() * RuleInventory::SLOT_H) + o.y;
			v.foot = {x, y, (float)(r->getInventoryWidth() * 16), (float)(r->getInventoryHeight() * 16)};
			v.sprite = {x, y};
		}
		v.frame = it->getBigSprite(set, battle, anim);
		items.push_back(v);
	}
	// things on the floor under the soldier, packed into the ground area (what fits)
	const RuleInventory *ground = ctx.game->getMod()->getInventoryGround();
	if (ground && u->getTile())
	{
		glm::vec2 o = offOf(ground);
		int x = 0;
		for (BattleItem *it : *u->getTile()->getInventory())
		{
			int w = it->getRules()->getInventoryWidth();
			if (it->getRules()->getInventoryHeight() > 3) continue;
			if (x + w > GROUND_COLS) break;
			ItemView v;
			v.item = it;
			float gx = (float)(ground->getX() + x * 16) + o.x, gy = (float)ground->getY() + o.y;
			v.foot = {gx, gy, (float)(w * 16), (float)(it->getRules()->getInventoryHeight() * 16)};
			v.sprite = {gx, gy};
			v.frame = it->getBigSprite(set, battle, anim);
			items.push_back(v);
			x += w;
		}
	}
}

/// A voxel model of an item sprite: every opaque pixel becomes a little column,
/// thicker towards the middle of the silhouette.
const Mesh *Table::Impl::itemMesh(const Surface *frame)
{
	if (!frame || !bs) return nullptr;
	auto it = itemMeshes.find(frame);
	if (it != itemMeshes.end()) return it->second.get();
	int W = frame->getWidth(), H = frame->getHeight();
	SDL_Color *pal = bs->getPalette();
	std::vector<int> d(W * H, 0);
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x)
			d[y * W + x] = frame->getPixel(x, y) ? 99 : 0;
	// chessboard distance to the outline (two passes)
	for (int pass = 0; pass < 2; ++pass)
		for (int y = 0; y < H; ++y)
			for (int x = 0; x < W; ++x)
			{
				int xx = pass ? W - 1 - x : x, yy = pass ? H - 1 - y : y;
				int &v = d[yy * W + xx];
				if (!v) continue;
				int best = v;
				for (int dy = -1; dy <= 1; ++dy)
					for (int dx = -1; dx <= 1; ++dx)
					{
						int nx = xx + dx, ny = yy + dy;
						int n = (nx < 0 || ny < 0 || nx >= W || ny >= H) ? 0 : d[ny * W + nx];
						best = std::min(best, n + 1);
					}
				v = best;
			}
	auto height = [&](int x, int y) -> float
	{
		if (x < 0 || y < 0 || x >= W || y >= H) return 0.f;
		int v = d[y * W + x];
		return v ? 1.f + 0.8f * (float)std::min(v - 1, 3) : 0.f;
	};
	MeshData m;
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x)
		{
			Uint8 c = frame->getPixel(x, y);
			if (!c) continue;
			SDL_Color k = pal[c];
			glm::vec4 col(k.r / 255.f, k.g / 255.f, k.b / 255.f, 1.f);
			glm::vec4 side = col * glm::vec4(0.72f, 0.72f, 0.72f, 1.f);
			float h = height(x, y);
			float X0 = (float)x, X1 = x + 1.f, Z0 = (float)y, Z1 = y + 1.f;
			m.addQuad({X0, h, Z1}, {X1, h, Z1}, {X1, h, Z0}, {X0, h, Z0}, col, MAT_PLAIN);
			// walls where the neighbour is lower
			float n;
			if ((n = height(x, y + 1)) < h) m.addQuad({X0, n, Z1}, {X1, n, Z1}, {X1, h, Z1}, {X0, h, Z1}, side, MAT_PLAIN);
			if ((n = height(x, y - 1)) < h) m.addQuad({X1, n, Z0}, {X0, n, Z0}, {X0, h, Z0}, {X1, h, Z0}, side, MAT_PLAIN);
			if ((n = height(x + 1, y)) < h) m.addQuad({X1, n, Z1}, {X1, n, Z0}, {X1, h, Z0}, {X1, h, Z1}, side, MAT_PLAIN);
			if ((n = height(x - 1, y)) < h) m.addQuad({X0, n, Z0}, {X0, n, Z1}, {X0, h, Z1}, {X0, h, Z0}, side, MAT_PLAIN);
		}
	auto mesh = std::make_unique<Mesh>();
	mesh->upload(m);
	const Mesh *ptr = mesh.get();
	itemMeshes[frame] = std::move(mesh);
	return ptr;
}

glm::vec3 Table::Impl::itemOrigin(int i) const
{
	glm::vec2 w = invToWorld(items[i].sprite);
	return {w.x, itemY(i), w.y};
}

void Table::Impl::grab(int i, int hand, const glm::vec3 &at)
{
	held.item = items[i].item;
	held.hand = hand;
	held.origin = itemOrigin(i);
	held.offset = held.origin - at;
	held.footDelta = glm::vec2(items[i].foot.x, items[i].foot.y) - items[i].sprite;
	held.footSize = glm::vec2(items[i].foot.z, items[i].foot.w);
	lastEvent = "picked up " + held.item->getRules()->getType();
}

void Table::Impl::warn(TableContext &ctx, const std::string &id)
{
	Mod *mod = ctx.game->getMod();
	Font *big = mod->getFont("FONT_BIG", false), *small = mod->getFont("FONT_SMALL", false);
	if (!big || !small || !bs) return;
	Text t(320, 10, 0, 0);
	t.initText(big, small, ctx.game->getLanguage());
	t.setPalette(bs->getPalette());
	t.setColor(1);
	t.setAlign(ALIGN_CENTER);
	t.setText(ctx.game->getLanguage()->getString(id));
	t.draw();
	std::vector<uint32_t> px(320 * 10);
	for (int y = 0; y < 10; ++y)
		for (int x = 0; x < 320; ++x)
		{
			int v = t.getPixel(x, y);
			px[(size_t)y * 320 + x] = v == 0 ? 0u : v <= 2 ? 0xFF4060FFu : 0xC0000010u;
		}
	warnTex.create(320, 10, true, true);
	warnTex.update(px.data(), 320, 10);
	warnTimer = 2.5f;
	lastEvent = "warning " + id;
}

/// Moves (or swaps) an item with the same rules as the inventory screen.
void Table::Impl::drop(TableContext &ctx)
{
	BattleItem *a = held.item;
	held.item = nullptr;
	held.hand = -1;
	if (!a || !invUnit || !bsTop || battle->getSide() != FACTION_PLAYER || bs->getBattleGame()->isBusy()) return;
	bool stillThere = false;
	for (const ItemView &v : items) if (v.item == a) stillThere = true;
	if (!stillThere) return;

	// where did it land?
	glm::vec2 spriteTL = worldToInv(glm::vec2(held.origin.x, held.origin.z));
	glm::vec2 footTL = spriteTL + held.footDelta;
	const RuleInventory *slot = nullptr;
	int x = 0, y = 0;
	if (!locate(footTL, held.footSize, slot, x, y))
	{
		std::ostringstream ss;
		ss << "dropped outside the grid (" << footTL.x << "," << footTL.y << ")";
		lastEvent = ss.str();
		return;
	}

	BattleUnit *u = invUnit;
	const RuleItem *ra = a->getRules();
	if (ra->isFixed() || !ra->canBePlacedIntoInventorySection(slot)) { warn(ctx, "STR_CANNOT_PLACE_ITEM_INTO_THIS_SECTION"); return; }
	const RuleInventory *from = a->getSlot();
	int fx = a->getSlotX(), fy = a->getSlotY();
	if (slot == from && (slot->getType() == INV_HAND || (x == fx && y == fy))) return;
	if (slot->getType() == INV_GROUND) { x = 0; y = 0; }
	else if (!slot->fitItemInSlot(ra, x, y)) { warn(ctx, "STR_NOT_ENOUGH_SPACE"); return; }

	auto overlapping = [&](BattleItem *item, const RuleInventory *s, int sx, int sy, BattleItem *ignore1, BattleItem *ignore2)
	{
		std::vector<BattleItem*> out;
		if (s->getType() == INV_GROUND) return out;
		for (BattleItem *bi : *u->getInventory())
			if (bi != ignore1 && bi != ignore2 && bi->getSlot() == s && bi->occupiesSlot(sx, sy, item)) out.push_back(bi);
		return out;
	};
	TileEngine *te = battle->getTileEngine();
	const RuleInventory *ground = ctx.game->getMod()->getInventoryGround();
	auto done = [&]()
	{
		if (Sound *s = ctx.game->getMod()->getSoundByDepth(battle->getDepth(), Mod::ITEM_DROP)) s->play();
		bs->updateSoldierInfo(false);
	};

	std::vector<BattleItem*> occ = overlapping(a, slot, x, y, a, nullptr);
	if (occ.empty())
	{
		if (!u->spendTimeUnits(a->getMoveToCost(slot))) { warn(ctx, "STR_NOT_ENOUGH_TIME_UNITS"); return; }
		te->itemMoveInventory(u->getTile(), u, a, slot, x, y);
		lastEvent = "moved " + ra->getType() + " to " + slot->getId();
		done();
		return;
	}
	// swap with the one item in the way: it goes where ours came from, or to the floor when it was in a hand
	if (occ.size() != 1 || occ[0]->getRules()->isFixed()) { warn(ctx, "STR_NOT_ENOUGH_SPACE"); return; }
	BattleItem *b = occ[0];
	const RuleItem *rb = b->getRules();
	bool backFits = from && rb->canBePlacedIntoInventorySection(from)
		&& (from->getType() == INV_GROUND || (from->fitItemInSlot(rb, fx, fy) && overlapping(b, from, fx, fy, a, b).empty()));
	const RuleInventory *bDest = backFits ? from : ground;
	int bx = backFits ? fx : 0, by = backFits ? fy : 0;
	if (!backFits && slot->getType() != INV_HAND) { warn(ctx, "STR_NOT_ENOUGH_SPACE"); return; }
	if (!u->spendTimeUnits(a->getMoveToCost(slot) + b->getMoveToCost(bDest))) { warn(ctx, "STR_NOT_ENOUGH_TIME_UNITS"); return; }
	te->itemMoveInventory(u->getTile(), u, b, bDest, bx, by);
	te->itemMoveInventory(u->getTile(), u, a, slot, x, y);
	lastEvent = "swapped " + ra->getType() + " with " + rb->getType();
	done();
}

void Table::Impl::updateItems(TableContext &ctx, const Hands &hands)
{
	if (inBattle && gridFor != battle) buildGrid(ctx);
	listItems(ctx);
	if (held.item)
	{
		bool present = false;
		for (const ItemView &v : items) if (v.item == held.item) present = true;
		if (!present || !inBattle) { held.item = nullptr; held.hand = -1; }
	}
	if (!inBattle) return;
	for (int h = 0; h < 2; ++h)
	{
		const HandPose &P = hands.pose[h];
		if (!P.valid || P.ghost)
		{
			if (held.hand == h) drop(ctx);
			continue;
		}
		if (held.hand == h)
		{
			// follow while pinched; on release keep the last pinched position (opening fingers move the midpoint)
			if (P.pinch) held.origin = P.pinchPoint + held.offset;
			else drop(ctx);
			continue;
		}
		if (P.pinchStarted && !held.item)
		{
			// the item under the pinch, if it is close to its float height
			int best = -1;
			float bestD = 1e9f;
			for (int i = 0; i < (int)items.size(); ++i)
			{
				glm::vec2 a = invToWorld({items[i].foot.x, items[i].foot.y});
				glm::vec2 b = invToWorld({items[i].foot.x + items[i].foot.z, items[i].foot.y + items[i].foot.w});
				const float m = 0.012f;
				glm::vec3 pp = P.pinchPoint;
				if (pp.x < a.x - m || pp.x > b.x + m || pp.z < a.y - m || pp.z > b.y + m) continue;
				if (pp.y < sy - 0.01f || pp.y > sy + 0.09f) continue;
				float d = glm::length(glm::vec2(pp.x, pp.z) - (a + b) * 0.5f);
				if (d < bestD) { bestD = d; best = i; }
			}
			if (best >= 0)
			{
				grab(best, h, P.pinchPoint);
				ctx.haptic(h, 0.35f, 0.02f);
			}
		}
	}
}

// ------------------------------------------------------------------ finger taps on the map

void Table::Impl::updateTaps(TableContext &ctx, const Hands &hands, float dt)
{
	for (int h = 0; h < 2; ++h)
	{
		cooldown[h] = std::max(0.f, cooldown[h] - dt);
		const HandPose &P = hands.pose[h];
		if (!inBattle || !P.valid || P.ghost || P.pinch || held.hand == h)
		{
			touching[h] = false;
			headInside[h] = nullptr;
			continue;
		}
		glm::vec3 tip = P.tip(F_INDEX);
		float r = P.radius[fingerTip(F_INDEX)];

		// tap a figure on the head: select it
		BattleUnit *inside = nullptr;
		bool ours = false;
		for (const Board::UnitMarker &m : board->unitMarkers())
			if (glm::length(tip - m.head) < m.radius + r) { inside = m.unit; ours = m.ours; break; }
		if (inside && inside != headInside[h] && cooldown[h] <= 0.f && P.indexExtended && ours && bsTop)
		{
			if (board->selectUnit(inside))
			{
				ctx.haptic(h, 0.5f, 0.03f);
				lastEvent = "selected a unit by tapping its head";
			}
			cooldown[h] = 0.35f;
		}
		headInside[h] = inside;
		if (inside) { touching[h] = true; continue; }

		// tap the map: same as clicking that tile
		int tx, ty, tz;
		float floorY;
		if (!board->tileUnder(tip, tx, ty, tz, floorY)) { touching[h] = false; continue; }
		float gap = tip.y - r - floorY;
		// a pointing fingertip close over the map works like the mouse: the game's cursor follows it
		if (P.indexExtended && gap < 0.08f && !ctx.handBusy[h]) board->fingerHover(tx, ty, tz);
		bool now = touching[h] ? gap < 0.012f : gap < 0.004f;
		if (now && !touching[h] && cooldown[h] <= 0.f && P.indexExtended && P.tipVelocity.y < -0.02f && bsTop && !ctx.handBusy[h])
		{
			board->clickTile(tx, ty, tz, false);
			ctx.haptic(h, 0.45f, 0.02f);
			cooldown[h] = 0.3f;
			std::ostringstream ss;
			ss << "tapped tile " << tx << "," << ty << "," << tz;
			lastEvent = ss.str();
			Log(LOG_INFO) << "[VR] " << lastEvent;
		}
		touching[h] = now;
	}
}

// ------------------------------------------------------------------ wall screens

static void surfaceToRGBA(Surface *s, const SDL_Color *pal, int x0, int y0, int w, int h, std::vector<uint32_t> &out)
{
	out.resize((size_t)w * h);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			Uint8 c = s->getPixel(x0 + x, y0 + y);
			out[(size_t)y * w + x] = rgba(pal[c]);
		}
}

void Table::Impl::updateWalls(TableContext &ctx, float dt)
{
	for (Wall &w : walls) w.timer -= dt;
	if (!inBattle)
	{
		for (Wall &w : walls) w.live = false;
		if (stats) { delete stats; stats = nullptr; statsUnit = nullptr; }
		if (minimap) { delete minimap; minimap = nullptr; minimapFor = nullptr; }
		return;
	}
	Game *g = ctx.game;
	SDL_Color *bpal = bs->getPalette();

	// left: the selected soldier's stats, drawn by the game's own stats screen off-screen
	BattleUnit *sel = battle->getSelectedUnit();
	if (sel && sel->isOut()) sel = nullptr;
	if (sel != statsUnit)
	{
		delete stats;
		stats = nullptr;
		statsUnit = sel;
		walls[0].timer = 0.f;
		if (sel)
		{
			bool maximize = Options::maximizeInfoScreens;
			Options::maximizeInfoScreens = false; // never resize the real screen for this
			stats = new UnitInfoState(sel, bs, false, false);
			Options::maximizeInfoScreens = maximize;
			stats->vrMute();
		}
	}
	if (stats && walls[0].timer <= 0.f)
	{
		walls[0].timer = 0.5f;
		// init() refreshes the numbers, but also sets the real screen's palette: put it back
		SDL_Color saved[256];
		std::copy_n(g->getScreen()->getPalette(), 256, saved);
		Uint8 cursorColor = g->getCursor()->getColor();
		stats->init();
		g->getScreen()->setPalette(saved, 0, 256);
		g->getCursor()->setPalette(saved);
		g->getCursor()->setColor(cursorColor);
		g->getCursor()->draw();
		g->getFpsCounter()->setPalette(saved);
		int sw = g->getScreen()->getSurface()->w, sh = g->getScreen()->getSurface()->h;
		Surface off(sw, sh);
		off.setPalette(stats->vrPalette());
		off.clear();
		stats->vrBlitTo(&off);
		int dx = std::max(0, g->getScreen()->getDX()), dy = std::max(0, g->getScreen()->getDY());
		surfaceToRGBA(&off, stats->vrPalette(), std::min(dx, sw - 320), std::min(dy, sh - 200), std::min(320, sw), std::min(200, sh), walls[0].px);
		walls[0].tw = std::min(320, sw);
		walls[0].th = std::min(200, sh);
		walls[0].live = true;
	}
	if (!stats) walls[0].live = false;

	// right: the minimap, centred where the game camera is
	if (minimapFor != battle)
	{
		delete minimap;
		minimap = nullptr;
		minimapFor = battle;
		if (bs->getMap())
			minimap = new MiniMapView(256, 256, 0, 0, g, bs->getMap()->getCamera(), battle, bs->getMap()->reShadeMinimap(7));
	}
	if (minimap && walls[1].timer <= 0.f)
	{
		walls[1].timer = 0.15f;
		minimap->setPalette(bpal);
		minimap->animate();
		minimap->draw();
		surfaceToRGBA(minimap, bpal, 0, 0, 256, 256, walls[1].px);
		walls[1].tw = walls[1].th = 256;
		walls[1].live = true;
	}

	// back: squad roster with time units, health, energy and morale
	if (walls[2].timer <= 0.f)
	{
		walls[2].timer = 0.5f;
		const int W = 320, H = 200;
		std::vector<uint32_t> &px = walls[2].px;
		px.assign((size_t)W * H, 0xFF201008u);
		auto rect = [&](int x, int y, int w, int h, uint32_t c)
		{
			for (int yy = std::max(0, y); yy < std::min(H, y + h); ++yy)
				for (int xx = std::max(0, x); xx < std::min(W, x + w); ++xx) px[(size_t)yy * W + xx] = c;
		};
		Font *big = g->getMod()->getFont("FONT_BIG", false), *small = g->getMod()->getFont("FONT_SMALL", false);
		auto text = [&](int x, int y, const std::string &s, uint32_t color)
		{
			if (!big || !small) return;
			Text t(150, 9, 0, 0);
			t.initText(big, small, g->getLanguage());
			t.setPalette(bpal);
			t.setColor(1);
			t.setText(s);
			t.draw();
			for (int yy = 0; yy < 9; ++yy)
				for (int xx = 0; xx < 150; ++xx)
				{
					int v = t.getPixel(xx, yy);
					if (v && x + xx < W && y + yy < H) px[(size_t)(y + yy) * W + x + xx] = v <= 2 ? color : 0xFF000000u;
				}
		};
		rect(0, 0, W, 12, 0xFF402010u);
		text(4, 2, g->getLanguage()->getString("STR_SOLDIERS"), 0xFF60E0FFu);
		int row = 0;
		for (BattleUnit *u : *battle->getUnits())
		{
			if (u->getFaction() != FACTION_PLAYER || u->isOut()) continue;
			int y = 16 + row * 15;
			if (y + 14 > H) break;
			bool isSel = u == battle->getSelectedUnit();
			if (isSel) rect(0, y - 1, W, 15, 0xFF503818u);
			text(4, y + 2, u->getName(g->getLanguage()), isSel ? 0xFF60FFFFu : 0xFFE0E0E0u);
			auto bar = [&](int bx, int by, int value, int max, uint32_t c)
			{
				rect(bx, by, 60, 4, 0xFF181010u);
				if (max > 0) rect(bx, by, 60 * std::max(0, std::min(value, max)) / max, 4, c);
			};
			const UnitStats *st = u->getBaseStats();
			bar(150, y + 1, u->getTimeUnits(), st->tu, 0xFF30D040u);
			bar(220, y + 1, u->getEnergy(), st->stamina, 0xFF2090F0u);
			bar(150, y + 7, u->getHealth(), st->health, 0xFF2020E0u);
			bar(220, y + 7, u->getMorale(), 100, 0xFFE07030u);
			++row;
		}
		walls[2].tw = W;
		walls[2].th = H;
		walls[2].live = row > 0;
	}

	for (Wall &w : walls)
		if (w.live && !w.px.empty())
		{
			if (!w.tex.valid() || w.tex.width() != w.tw || w.tex.height() != w.th) w.tex.create(w.tw, w.th, true, true);
			w.tex.update(w.px.data(), w.tw, w.th);
		}
}

// ------------------------------------------------------------------ per frame

void Table::update(TableContext &ctx, const Hands &hands, float dt)
{
	Impl &p = *_p;
	p.time += dt;
	p.syncBattle(ctx);
	p.updateButtons(ctx, hands, dt);
	p.updateItems(ctx, hands);
	p.updateTaps(ctx, hands, dt);
	p.updateWalls(ctx, dt);
	if (p.warnTimer > 0.f) p.warnTimer -= dt;
}

void Table::colliders(std::vector<Collider> &out) const
{
	const Impl &p = *_p;
	Collider top;
	top.mn = {p.table.x, p.sy - 0.07f, p.table.y};
	top.mx = {p.table.z, p.sy, p.table.w};
	top.id = ID_TABLE;
	top.holes = p.holes;
	out.push_back(top);
	const float rim = p.L.tableRim, ry = p.L.tableCenter.y + 0.02f;
	Collider r;
	r.id = ID_RIM;
	r.mn = {p.table.x - rim, p.sy - 0.1f, p.table.y - rim}; r.mx = {p.table.x, ry, p.table.w + rim}; out.push_back(r);
	r.mn = {p.table.z, p.sy - 0.1f, p.table.y - rim}; r.mx = {p.table.z + rim, ry, p.table.w + rim}; out.push_back(r);
	r.mn = {p.table.x, p.sy - 0.1f, p.table.y - rim}; r.mx = {p.table.z, ry, p.table.y}; out.push_back(r);
	r.mn = {p.table.x, p.sy - 0.1f, p.table.w}; r.mx = {p.table.z, ry, p.table.w + rim}; out.push_back(r);
	for (size_t i = 0; i < p.btns.size(); ++i)
	{
		const Btn &b = p.btns[i];
		if (b.open < 0.98f) continue;
		Collider c;
		c.id = ID_BUTTON + (int)i;
		float top = p.capTop(b);
		c.mn = {b.rect.x + 0.002f, top - 0.03f, b.rect.y + 0.002f};
		c.mx = {b.rect.z - 0.002f, top, b.rect.w - 0.002f};
		out.push_back(c);
	}
}

// ------------------------------------------------------------------ drawing

void Table::draw(const Shader &sh) const
{
	const Impl &p = *_p;
	auto boxAt = [&](const glm::vec3 &mn, const glm::vec3 &mx)
	{
		sh.set("uModel", glm::translate(glm::mat4(1.f), (mn + mx) * 0.5f) * glm::scale(glm::mat4(1.f), mx - mn));
		p.box.draw();
	};
	auto flat = [&](const glm::vec4 &r, float y)
	{
		sh.set("uModel", glm::translate(glm::mat4(1.f), {(r.x + r.z) * 0.5f, y, (r.y + r.w) * 0.5f}) * glm::scale(glm::mat4(1.f), {r.z - r.x, 1.f, r.w - r.y}));
		p.quadUp.draw();
	};

	// divider between the map and the controls
	sh.set("uMode", 2);
	sh.set("uTint", glm::vec4(0.1f, 0.75f, 0.85f, 1.f));
	flat({p.table.x, p.map.w + 0.004f, p.table.z, p.map.w + 0.008f}, p.sy + 0.0006f);

	// ---- button bay: panel picture with holes, wells, hatches, caps
	sh.set("uHoleCount", (int)std::min<size_t>(p.holes.size(), 24));
	for (size_t i = 0; i < p.holes.size() && i < 24; ++i)
		sh.set(("uHoles[" + std::to_string(i) + "]").c_str(), p.holes[i]);
	sh.set("uHoleY", glm::vec2(p.sy - 0.002f, p.sy + 0.003f));
	if (p.panelValid && p.inBattle)
	{
		sh.set("uMode", 1);
		sh.set("uTint", glm::vec4(0.8f, 0.8f, 0.85f, 1.f));
		sh.set("uTexSize", glm::vec2((float)p.panelTex.width(), (float)p.panelTex.height()));
		p.panelTex.bind(0);
		flat(p.bay, p.sy + 0.0008f);
	}
	else
	{
		sh.set("uMode", 0);
		sh.set("uTint", glm::vec4(0.09f, 0.1f, 0.12f, 1.f));
		flat(p.bay, p.sy + 0.0008f);
	}
	sh.set("uHoleCount", 0);
	for (const Btn &b : p.btns)
	{
		glm::vec3 c((b.rect.x + b.rect.z) * 0.5f, p.sy, (b.rect.y + b.rect.w) * 0.5f);
		float w = b.rect.z - b.rect.x, d = b.rect.w - b.rect.y;
		if (b.open > 0.01f)
		{
			sh.set("uMode", 0);
			sh.set("uTint", glm::vec4(1.f));
			sh.set("uModel", glm::translate(glm::mat4(1.f), c) * glm::scale(glm::mat4(1.f), {w, CAVITY, d}));
			p.cavity.draw();
		}
		// hatches: two leaves hinged at the outer edges, swinging down into the well
		float ang = smooth01(0.f, 0.45f, b.open) * glm::radians(92.f);
		if (ang < glm::radians(91.f))
		{
			sh.set("uMode", 0);
			sh.set("uTint", glm::vec4(0.16f, 0.17f, 0.2f, 1.f));
			bool alongX = w >= d;
			for (int s = 0; s < 2; ++s)
			{
				float sign = s == 0 ? 1.f : -1.f;
				glm::vec3 hinge = alongX ? glm::vec3(s == 0 ? b.rect.x : b.rect.z, p.sy, c.z) : glm::vec3(c.x, p.sy, s == 0 ? b.rect.y : b.rect.w);
				glm::mat4 m = glm::translate(glm::mat4(1.f), hinge);
				if (alongX) m = m * glm::rotate(glm::mat4(1.f), -sign * ang, {0, 0, 1});
				else m = m * glm::rotate(glm::mat4(1.f), sign * ang, {1, 0, 0});
				glm::vec3 half = alongX ? glm::vec3(w * 0.5f, 0.003f, d) : glm::vec3(w, 0.003f, d * 0.5f);
				glm::vec3 off = alongX ? glm::vec3(sign * w * 0.25f, -0.0015f, 0.f) : glm::vec3(0.f, -0.0015f, sign * d * 0.25f);
				sh.set("uModel", m * glm::translate(glm::mat4(1.f), off) * glm::scale(glm::mat4(1.f), half));
				p.box.draw();
			}
		}
		// the cap: dark body with the button's picture on top
		if (b.open > 0.4f)
		{
			float top = p.capTop(b);
			glm::vec3 mn(b.rect.x + 0.002f, top - 0.03f, b.rect.y + 0.002f), mx(b.rect.z - 0.002f, top - 0.0005f, b.rect.w - 0.002f);
			sh.set("uMode", 0);
			sh.set("uTint", glm::vec4(0.13f, 0.14f, 0.16f, 1.f));
			boxAt(mn, mx);
			if (p.panelValid)
			{
				sh.set("uMode", 1);
				float lit = p.usable(b) ? (b.latched ? 1.35f : 1.f) : 0.45f;
				sh.set("uTint", glm::vec4(lit, lit, lit, 1.f));
				sh.set("uTexSize", glm::vec2((float)p.panelTex.width(), (float)p.panelTex.height()));
				float tw = (float)p.panelTex.width(), th = (float)p.panelTex.height();
				sh.set("uUVRect", glm::vec4(p.visX(b.px.x) / tw, b.px.y / th, b.px.z / tw, b.px.w / th));
				p.panelTex.bind(0);
				flat({mn.x, mn.z, mx.x, mx.z}, top);
				sh.set("uUVRect", glm::vec4(0.f, 0.f, 1.f, 1.f));
			}
		}
	}

	// ---- inventory tray
	sh.set("uMode", 0);
	sh.set("uTint", glm::vec4(0.07f, 0.075f, 0.09f, 1.f));
	flat(p.tray, p.sy + 0.0006f);
	if (!p.inBattle || !p.gridTex.valid()) { sh.set("uTint", glm::vec4(1.f)); return; }
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	sh.set("uMode", 4);
	sh.set("uTint", glm::vec4(1.15f, 1.15f, 1.15f, p.invUnit ? 1.f : 0.35f));
	glm::vec2 a = p.invToWorld({p.invBox.x, p.invBox.y}), bb = p.invToWorld({p.invBox.z, p.invBox.w});
	// each section's slots, cut out of the game's grid picture, then its label
	p.gridTex.bind(0);
	for (const InvSection &sc : p.secs)
	{
		glm::vec4 g = sc.game;
		sh.set("uUVRect", glm::vec4(g.x / 320.f, g.y / 200.f, g.z / 320.f, g.w / 200.f));
		glm::vec2 w0 = p.invToWorld(glm::vec2(g.x, g.y) + sc.off), w1 = p.invToWorld(glm::vec2(g.x + g.z, g.y + g.w) + sc.off);
		flat({w0.x, w0.y, w1.x, w1.y}, p.sy + 0.0012f);
	}
	if (p.labelTex.valid())
	{
		p.labelTex.bind(0);
		float lw = (float)p.labelTex.width(), lh = (float)p.labelTex.height();
		for (const InvSection &sc : p.secs)
		{
			sh.set("uUVRect", glm::vec4(0.f, sc.labelRow * 9.f / lh, sc.labelW / lw, 9.f / lh));
			glm::vec2 w0 = p.invToWorld(sc.labelAt), w1 = p.invToWorld(sc.labelAt + glm::vec2((float)sc.labelW, 9.f));
			flat({w0.x, w0.y, w1.x, w1.y}, p.sy + 0.0012f);
		}
	}
	sh.set("uUVRect", glm::vec4(0.f, 0.f, 1.f, 1.f));
	if (p.warnTimer > 0.f && p.warnTex.valid())
	{
		sh.set("uTint", glm::vec4(1.f, 1.f, 1.f, std::min(1.f, p.warnTimer)));
		p.warnTex.bind(0);
		float h = (bb.x - a.x) * 10.f / 320.f;
		flat({a.x, p.tray.y + 0.004f, bb.x, p.tray.y + 0.004f + h}, p.sy + 0.03f);
	}

	// where the held item would land
	if (p.held.item)
	{
		glm::vec2 tl = p.worldToInv({p.held.origin.x, p.held.origin.z}) + p.held.footDelta;
		const RuleInventory *slot = nullptr;
		int sx = 0, sy2 = 0;
		if (p.locate(tl, p.held.footSize, slot, sx, sy2))
		{
			glm::vec2 o = p.offOf(slot);
			glm::vec2 at = slot->getType() == INV_GROUND || slot->getType() == INV_HAND
				? glm::vec2((float)slot->getX(), (float)slot->getY()) + o
				: glm::vec2(slot->getX() + sx * 16.f, slot->getY() + sy2 * 16.f) + o;
			glm::vec2 size = slot->getType() == INV_HAND ? glm::vec2(32.f, 48.f) : p.held.footSize;
			glm::vec2 w0 = p.invToWorld(at), w1 = p.invToWorld(at + size);
			sh.set("uMode", 2);
			sh.set("uTint", glm::vec4(0.3f, 1.f, 0.6f, 0.35f));
			flat({w0.x, w0.y, w1.x, w1.y}, p.sy + 0.0016f);
		}
	}

	// a soft footprint under each floating item, so you can tell where it sits
	sh.set("uMode", 2);
	sh.set("uTint", glm::vec4(0.2f, 0.9f, 1.f, 0.12f));
	for (const ItemView &v : p.items)
	{
		if (v.item == p.held.item) continue;
		glm::vec2 a0 = p.invToWorld({v.foot.x + 1.f, v.foot.y + 1.f}), a1 = p.invToWorld({v.foot.x + v.foot.z - 1.f, v.foot.y + v.foot.w - 1.f});
		flat({a0.x, a0.y, a1.x, a1.y}, p.sy + 0.0014f);
	}
	// floating voxel items
	sh.set("uMode", 0);
	for (int i = 0; i < (int)p.items.size(); ++i)
	{
		const ItemView &v = p.items[i];
		const Mesh *m = const_cast<Impl&>(p).itemMesh(v.frame);
		if (!m) continue;
		bool isHeld = v.item == p.held.item;
		glm::vec3 o = isHeld ? p.held.origin : p.itemOrigin(i);
		float glow = isHeld ? 1.6f : (i == p.hoverItem ? 1.35f : 1.12f);
		sh.set("uTint", glm::vec4(glow, glow, glow, 0.82f));
		sh.set("uModel", glm::translate(glm::mat4(1.f), o) * glm::scale(glm::mat4(1.f), glm::vec3(p.invScale)));
		m->draw();
	}
	glDisable(GL_BLEND);
	sh.set("uTint", glm::vec4(1.f));
}

void Table::Impl::drawWall(const Shader &sh, const Wall &w) const
{
	glm::vec3 up = glm::cross(w.normal, w.right);
	glm::mat4 m(1.f);
	m[0] = glm::vec4(w.right, 0.f);
	m[1] = glm::vec4(up, 0.f);
	m[2] = glm::vec4(w.normal, 0.f);
	m[3] = glm::vec4(w.center, 1.f);
	sh.set("uMode", 0);
	sh.set("uTint", glm::vec4(0.05f, 0.055f, 0.065f, 1.f));
	sh.set("uModel", m * glm::translate(glm::mat4(1.f), {0.f, 0.f, -0.004f}) * glm::scale(glm::mat4(1.f), {w.w + 0.08f, w.h + 0.08f, 1.f}));
	quadFront.draw();
	sh.set("uMode", 1);
	sh.set("uTint", glm::vec4(1.1f, 1.1f, 1.1f, 1.f));
	sh.set("uTexSize", glm::vec2((float)w.tw, (float)w.th));
	w.tex.bind(0);
	sh.set("uModel", m * glm::scale(glm::mat4(1.f), {w.w, w.h, 1.f}));
	quadFront.draw();
}

void Table::drawWalls(const Shader &sh) const
{
	for (const Wall &w : _p->walls)
		if (w.live && w.tex.valid()) _p->drawWall(sh, w);
	sh.set("uTint", glm::vec4(1.f));
}

// ------------------------------------------------------------------ laser / mouse

bool Table::raycast(const glm::vec3 &o, const glm::vec3 &d, TableHit &hit) const
{
	const Impl &p = *_p;
	if (std::fabs(d.y) < 1e-6f) return false;
	auto plane = [&](float y, glm::vec3 &at) -> float
	{
		float t = (y - o.y) / d.y;
		at = o + d * t;
		return t;
	};
	bool any = false;
	for (size_t i = 0; i < p.btns.size(); ++i)
	{
		const Btn &b = p.btns[i];
		if (!p.usable(b)) continue;
		glm::vec3 at;
		float t = plane(p.capTop(b), at);
		if (t <= 0.f || t >= hit.t) continue;
		if (at.x < b.rect.x || at.x > b.rect.z || at.z < b.rect.y || at.z > b.rect.w) continue;
		hit = {TableHit::BUTTON, t, (int)i, at};
		any = true;
	}
	if (p.held.item && p.held.hand == 2)
	{
		glm::vec3 at;
		float t = plane(p.sy + 0.024f, at);
		if (t > 0.f && t < hit.t && at.x > p.tray.x - 0.05f && at.x < p.tray.z + 0.05f && at.z > p.tray.y - 0.05f && at.z < p.tray.w + 0.05f)
		{
			hit = {TableHit::TRAY, t, -1, at};
			any = true;
		}
		return any;
	}
	for (int i = 0; i < (int)p.items.size(); ++i)
	{
		glm::vec3 at;
		float t = plane(p.itemY(i) + 0.002f, at);
		if (t <= 0.f || t >= hit.t) continue;
		glm::vec2 a = p.invToWorld({p.items[i].foot.x, p.items[i].foot.y});
		glm::vec2 b = p.invToWorld({p.items[i].foot.x + p.items[i].foot.z, p.items[i].foot.y + p.items[i].foot.w});
		if (at.x < a.x || at.x > b.x || at.z < a.y || at.z > b.y) continue;
		hit = {TableHit::ITEM, t, i, at};
		any = true;
	}
	return any;
}

void Table::pointerMove(const TableHit *hit)
{
	Impl &p = *_p;
	p.hoverItem = hit && hit->kind == TableHit::ITEM ? hit->index : -1;
	if (hit && p.held.item && p.held.hand == 2) p.held.origin = hit->point + p.held.offset;
}

void Table::pointerClick(const TableHit &hit, int button, TableContext &ctx)
{
	Impl &p = *_p;
	if (button != SDL_BUTTON_LEFT) return;
	if (p.held.item && p.held.hand == 2)
	{
		p.held.origin = hit.point + p.held.offset;
		p.drop(ctx);
		return;
	}
	if (hit.kind == TableHit::BUTTON && hit.index >= 0)
	{
		p.btns[hit.index].laserTimer = 0.14f;
		p.pressButton(hit.index, -1, ctx);
	}
	else if (hit.kind == TableHit::ITEM && hit.index >= 0 && hit.index < (int)p.items.size())
	{
		p.grab(hit.index, 2, hit.point);
	}
}

bool Table::itemCenter(int i, glm::vec3 &w) const
{
	const Impl &p = *_p;
	if (i < 0 || i >= (int)p.items.size()) return false;
	glm::vec2 c = p.invToWorld({p.items[i].foot.x + p.items[i].foot.z * 0.5f, p.items[i].foot.y + p.items[i].foot.w * 0.5f});
	w = {c.x, p.itemY(i), c.y};
	return true;
}

bool Table::buttonCenter(const std::string &name, glm::vec3 &w) const
{
	const Impl &p = *_p;
	for (const Btn &b : p.btns)
		if (b.name == name)
		{
			w = {(b.rect.x + b.rect.z) * 0.5f, p.sy + CAP_PROUD, (b.rect.y + b.rect.w) * 0.5f};
			return true;
		}
	return false;
}

bool Table::cellCenter(const std::string &slot, float cx, float cy, glm::vec3 &w) const
{
	const Impl &p = *_p;
	if (!p.game) return false;
	const RuleInventory *r = p.game->getMod()->getInventory(slot, false);
	if (!r) return false;
	glm::vec2 c = p.invToWorld(glm::vec2(r->getX() + cx * 16.f, r->getY() + cy * 16.f) + p.offOf(r));
	w = {c.x, p.sy + 0.024f, c.y};
	return true;
}

std::string Table::status() const
{
	const Impl &p = *_p;
	int open = 0;
	for (const Btn &b : p.btns) if (b.open > 0.97f) ++open;
	std::ostringstream ss;
	ss << "battle=" << p.inBattle << " top=" << p.bsTop << " buttonsUp=" << open << "/" << p.btns.size()
		<< " items=" << p.items.size() << " held=" << (p.held.item ? 1 : 0) << " last='" << p.lastEvent << "'";
	return ss.str();
}

}
}
