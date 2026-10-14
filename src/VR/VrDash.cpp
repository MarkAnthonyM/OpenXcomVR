#include "VrDash.h"
#include "../Engine/Game.h"
#include "../Engine/Font.h"
#include "../Engine/Language.h"
#include "../Engine/Palette.h"
#include "../Interface/Text.h"
#include "../Mod/Mod.h"
#include "../Mod/RuleResearch.h"
#include "../Mod/RuleManufacture.h"
#include "../Mod/RuleCraft.h"
#include "../Mod/RuleUfo.h"
#include "../Mod/RuleRegion.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/Base.h"
#include "../Savegame/ResearchProject.h"
#include "../Savegame/Production.h"
#include "../Savegame/Craft.h"
#include "../Savegame/Ufo.h"
#include "../Savegame/Region.h"
#include <algorithm>
#include <sstream>

namespace OpenXcom
{
namespace VR
{
namespace Dash
{

namespace
{

inline uint32_t C(int r, int g, int b) { return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r; }

// the geoscape's look: deep blue panels, gold headings, pale text
const uint32_t BG = C(8, 14, 34), BAND = C(20, 34, 72), LINE = C(40, 70, 120);
const uint32_t GOLD = C(255, 214, 90), TEXT = C(220, 228, 240), DIM = C(130, 150, 180);
const uint32_t GREEN = C(70, 220, 90), CYAN = C(90, 220, 255), AMBER = C(255, 170, 50), RED = C(240, 70, 60);

/// A picture to draw on with the game's fonts.
struct Canvas
{
	Game *g;
	int W, H;
	std::vector<uint32_t> &px;
	Font *big = nullptr, *small = nullptr;
	SDL_Color *pal = nullptr;

	Canvas(Game *game, std::vector<uint32_t> &out, int w, int h) : g(game), W(w), H(h), px(out)
	{
		px.assign((size_t)W * H, BG);
		big = g->getMod()->getFont("FONT_BIG", false);
		small = g->getMod()->getFont("FONT_SMALL", false);
		if (Palette *p = g->getMod()->getPalette("PAL_GEOSCAPE", false)) pal = p->getColors();
	}
	bool ok() const { return big && small; }
	std::string tr(const std::string &id) const { return g->getLanguage()->getString(id); }
	void rect(int x, int y, int w, int h, uint32_t c)
	{
		for (int yy = std::max(0, y); yy < std::min(H, y + h); ++yy)
			for (int xx = std::max(0, x); xx < std::min(W, x + w); ++xx) px[(size_t)yy * W + xx] = c;
	}
	/// Text in a box w wide starting at x (cut off at its edge); align 0 left, 1 centre, 2 right.
	void text(int x, int y, int w, const std::string &s, uint32_t color, bool isBig = false, int align = 0)
	{
		if (!ok() || w <= 0 || s.empty()) return;
		int h = isBig ? 16 : 9;
		Text t(w, h, 0, 0);
		t.initText(big, small, g->getLanguage());
		if (pal) t.setPalette(pal);
		if (isBig) t.setBig(); else t.setSmall();
		t.setAlign(align == 1 ? ALIGN_CENTER : align == 2 ? ALIGN_RIGHT : ALIGN_LEFT);
		t.setColor(1);
		t.setText(s);
		t.draw();
		for (int yy = 0; yy < h; ++yy)
			for (int xx = 0; xx < w; ++xx)
			{
				int v = t.getPixel(xx, yy);
				if (!v || x + xx < 0 || y + yy < 0 || x + xx >= W || y + yy >= H) continue;
				px[(size_t)(y + yy) * W + x + xx] = v <= 2 ? color : 0xFF000000u;
			}
	}
	void title(const std::string &s)
	{
		rect(0, 0, W, 19, BAND);
		rect(0, 19, W, 1, LINE);
		text(6, 2, W - 12, s, GOLD, true);
	}
	/// A bar: value of max, filled in color over a dark track.
	void bar(int x, int y, int w, int h, int value, int max, uint32_t color)
	{
		rect(x, y, w, h, C(18, 24, 44));
		if (max > 0) rect(x, y, w * std::max(0, std::min(value, max)) / max, h, color);
	}
};

std::string num(int v) { return std::to_string(v); }

uint32_t progressColor(const std::string &id)
{
	if (id == "STR_POOR") return RED;
	if (id == "STR_AVERAGE") return AMBER;
	if (id == "STR_GOOD") return GREEN;
	if (id == "STR_EXCELLENT") return CYAN;
	return DIM;
}

uint32_t craftColor(const std::string &status)
{
	if (status == "STR_READY") return GREEN;
	if (status == "STR_OUT") return CYAN;
	return AMBER; // repairs, refuelling, rearming
}

}

bool research(Game *game, std::vector<uint32_t> &px, int W, int H)
{
	SavedGame *sg = game ? game->getSavedGame() : nullptr;
	if (!sg) return false;
	Canvas cv(game, px, W, H);
	if (!cv.ok()) return false;
	cv.title(cv.tr("STR_CURRENT_RESEARCH"));
	cv.text(196, 6, 40, "STAFF", DIM, false, 2);
	cv.text(244, 6, W - 248, "PROGRESS", DIM);
	int y = 24;
	bool any = false;
	for (Base *b : *sg->getBases())
	{
		if (b->getResearch().empty()) continue;
		any = true;
		if (y + 10 > H) break;
		cv.text(4, y, 180, b->getName(game->getLanguage()), GOLD);
		cv.text(W - 154, y, 150, "Free scientists " + num(b->getAvailableScientists()) + "  Labs " + num(b->getFreeLaboratories()), DIM, false, 2);
		y += 11;
		for (const ResearchProject *rp : b->getResearch())
		{
			if (y + 9 > H) { cv.text(10, H - 9, 100, "...", DIM); break; }
			cv.text(10, y, 196, cv.tr(rp->getRules()->getName()), TEXT);
			cv.text(206, y, 30, num(rp->getAssigned()), TEXT, false, 2);
			std::string pr = rp->getResearchProgress();
			cv.text(244, y, W - 248, cv.tr(pr), progressColor(pr));
			y += 10;
		}
		y += 3;
	}
	if (!any) cv.text(0, H / 2 - 4, W, "No research in progress", DIM, false, 1);
	return true;
}

bool production(Game *game, std::vector<uint32_t> &px, int W, int H)
{
	SavedGame *sg = game ? game->getSavedGame() : nullptr;
	if (!sg) return false;
	Canvas cv(game, px, W, H);
	if (!cv.ok()) return false;
	cv.title(cv.tr("STR_CURRENT_PRODUCTION"));
	cv.text(176, 6, 44, "MADE", DIM, false, 2);
	cv.text(226, 6, 44, "THIS ONE", DIM);
	cv.text(272, 6, W - 274, "LEFT", DIM, false, 2);
	int y = 24;
	bool any = false;
	for (Base *b : *sg->getBases())
	{
		if (b->getProductions().empty()) continue;
		any = true;
		if (y + 10 > H) break;
		cv.text(4, y, 180, b->getName(game->getLanguage()), GOLD);
		cv.text(W - 154, y, 150, "Free engineers " + num(b->getAvailableEngineers()) + "  Space " + num(b->getFreeWorkshops()), DIM, false, 2);
		y += 11;
		for (const Production *p : b->getProductions())
		{
			if (y + 9 > H) { cv.text(10, H - 9, 100, "...", DIM); break; }
			const RuleManufacture *r = p->getRules();
			cv.text(10, y, 140, cv.tr(r->getName()), TEXT);
			cv.text(150, y, 22, num(p->getAssignedEngineers()), TEXT, false, 2);
			std::string made = num(p->getAmountProduced()) + "/" + (p->getInfiniteAmount() ? std::string("inf") : num(p->getAmountTotal()));
			cv.text(176, y, 44, made, TEXT, false, 2);
			// the bar: the unit being made now (time spent on it out of its build time)
			int t = r->getManufactureTime();
			int intoUnit = t > 0 ? p->getTimeSpent() - p->getAmountProduced() * t : 0;
			cv.bar(226, y + 2, 44, 4, intoUnit, t, CYAN);
			std::string left = "-";
			if (p->getInfiniteAmount()) left = "inf";
			else if (p->getAssignedEngineers() > 0)
			{
				int timeLeft = p->getAmountTotal() * t - p->getTimeSpent();
				int hours = (timeLeft + p->getAssignedEngineers() - 1) / p->getAssignedEngineers();
				left = num(hours / 24) + "d " + num(hours % 24) + "h";
			}
			cv.text(272, y, W - 274, left, TEXT, false, 2);
			y += 10;
		}
		y += 3;
	}
	if (!any) cv.text(0, H / 2 - 4, W, "No production in progress", DIM, false, 1);
	return true;
}

bool crafts(Game *game, std::vector<uint32_t> &px, int W, int H)
{
	SavedGame *sg = game ? game->getSavedGame() : nullptr;
	if (!sg) return false;
	Canvas cv(game, px, W, H);
	if (!cv.ok()) return false;
	Language *lang = game->getLanguage();
	cv.title(cv.tr("STR_CRAFT"));
	cv.text(110, 6, 60, "STATUS", DIM);
	cv.text(178, 6, 40, "FUEL", DIM);
	cv.text(222, 6, 40, "HULL", DIM);
	cv.text(266, 6, 50, "HEADING TO", DIM);
	int y = 24;
	bool any = false;
	for (Base *b : *sg->getBases())
	{
		if (b->getCrafts()->empty()) continue;
		any = true;
		if (y + 10 > H) break;
		cv.text(4, y, 200, b->getName(lang), GOLD);
		y += 11;
		for (Craft *c : *b->getCrafts())
		{
			if (y + 9 > H) { cv.text(10, H - 9, 100, "...", DIM); break; }
			cv.text(10, y, 98, c->getName(lang), TEXT);
			std::string st = c->getStatus();
			std::string word = cv.tr(st);
			if (c->isInDogfight()) word = "INTERCEPTING";
			else if (st == "STR_OUT" && c->getLowFuel()) word = "LOW FUEL";
			cv.text(110, y, 66, word, c->isInDogfight() ? RED : craftColor(st));
			int fuel = c->getFuelPercentage();
			cv.bar(178, y + 2, 38, 4, fuel, 100, fuel < 25 ? RED : fuel < 50 ? AMBER : GREEN);
			int hull = 100 - c->getDamagePercentage();
			cv.bar(222, y + 2, 38, 4, hull, 100, hull < 34 ? RED : hull < 67 ? AMBER : GREEN);
			std::string dest;
			if (st == "STR_OUT" && c->getDestination())
				dest = c->getDestination() == (Target*)b ? std::string("Base") : c->getDestination()->getName(lang);
			cv.text(266, y, W - 268, dest, CYAN);
			y += 10;
		}
		y += 3;
	}
	if (!any) cv.text(0, H / 2 - 4, W, "No craft", DIM, false, 1);
	return true;
}

bool ufos(Game *game, std::vector<uint32_t> &px, int W, int H)
{
	SavedGame *sg = game ? game->getSavedGame() : nullptr;
	if (!sg) return false;
	Canvas cv(game, px, W, H);
	if (!cv.ok()) return false;
	Language *lang = game->getLanguage();
	cv.title(cv.tr("STR_UFO_ACTIVITY_IN_AREAS"));
	const int split = W / 2;
	cv.rect(split, 22, 1, H - 24, LINE);

	// left: this month's UFO activity by area (the game's graph data), busiest first
	cv.text(4, 22, split - 8, "THIS MONTH", DIM);
	struct Row { std::string name; int alien, xcom; };
	std::vector<Row> rows;
	int top = 1;
	for (Region *r : *sg->getRegions())
	{
		int a = r->getActivityAlien().empty() ? 0 : r->getActivityAlien().back();
		int x = r->getActivityXcom().empty() ? 0 : r->getActivityXcom().back();
		rows.push_back({cv.tr(r->getRules()->getType()), a, x});
		top = std::max(top, a);
	}
	std::stable_sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) { return a.alien > b.alien; });
	int y = 32;
	for (const Row &r : rows)
	{
		if (y + 8 > H) break;
		cv.text(4, y, 74, r.name, r.alien > 0 ? TEXT : DIM);
		cv.bar(80, y + 1, split - 112, 5, r.alien, top, RED);
		cv.text(split - 30, y, 26, num(r.alien), r.alien > 0 ? TEXT : DIM, false, 2);
		y += 8;
	}

	// right: UFOs on the radar now
	int count = 0;
	for (Ufo *u : *sg->getUfos()) if (u->getDetected() && u->getStatus() != Ufo::DESTROYED) ++count;
	cv.text(split + 6, 22, split - 10, "TRACKING " + num(count), count ? AMBER : DIM);
	y = 33;
	for (Ufo *u : *sg->getUfos())
	{
		if (!u->getDetected() || u->getStatus() == Ufo::DESTROYED) continue;
		if (y + 18 > H) { cv.text(split + 6, H - 9, 60, "...", DIM); break; }
		const char *state = u->getStatus() == Ufo::LANDED ? "LANDED" : u->getStatus() == Ufo::CRASHED ? "CRASHED" : "FLYING";
		uint32_t col = u->getStatus() == Ufo::FLYING ? AMBER : u->getStatus() == Ufo::LANDED ? RED : DIM;
		cv.text(split + 6, y, 80, u->getName(lang), TEXT);
		cv.text(split + 86, y, split - 90, state, col, false, 2);
		std::string sub = cv.tr(u->getRules()->getSize());
		if (u->getStatus() == Ufo::FLYING) sub += "  " + cv.tr(u->getAltitude());
		cv.text(split + 12, y + 8, split - 16, sub, DIM);
		y += 18;
	}
	if (!count) cv.text(split, H / 2, split, "No UFOs on radar", DIM, false, 1);
	return true;
}

}
}
}
