// ============================================================================
//  CrewSprites.cpp — the character maps, resolved into coloured cells.
// ============================================================================
#include "ui/CrewSprites.h"

#include <algorithm>

namespace ship {

namespace {

std::string FirstName(const std::string& name) {
  const std::size_t space = name.find(' ');
  return space == std::string::npos ? name : name.substr(0, space);
}

}  // namespace

double OnDeck(const Simulation& sim, std::size_t rows) {
  return sim.World().floorY - static_cast<double>(rows) * sim.World().spritePx;
}

CrewLook LookForCrew(const Simulation& sim, const CrewMember& crew) {
  CrewLook look;
  const Catalog& art = sim.World();
  const OrderSheet* sheet = sim.SheetOf(crew);
  const bool atDoc = sim.FindOngoing("doc-" + crew.id) != nullptr;
  if ((sheet != nullptr && sheet->Has("doc")) || atDoc) {
    // The patient gown. Even the buckle goes quiet.
    look.cloth = "#e8e2d6";
    look.dark = "#cbb89c";
    look.btn = "#cbb89c";
    return look;
  }

  const std::string jobId = sheet != nullptr && sheet->hasJob ? sheet->job : crew.job;
  const JobDef* job = sim.FindJobDef(jobId);
  // Nobody: the reference's JOBS[0], the grey of a hand with no trade.
  look.cloth = job == nullptr || job->cloth.empty() ? "#a2947f" : job->cloth;
  look.dark = job == nullptr || job->dark.empty() ? "#857a68" : job->dark;

  auto hat = art.jobHat.find(jobId);
  if (hat != art.jobHat.end()) {
    look.hatKind = hat->second.first;
    look.hat = hat->second.second;
  }
  return look;
}

std::string PixelColour(const Simulation& sim, const CrewMember& crew,
                        const CrewLook& look, char key) {
  (void)sim;
  switch (key) {
    case 'h': return crew.hair.empty() ? "#5e3622" : crew.hair;
    case 'H': return crew.hair2.empty() ? "#804e2e" : crew.hair2;
    case 's': return crew.skin.empty() ? "#f7e4c0" : crew.skin;
    case 'e': return "#3a1c2e";
    case 'k': return "#1a1614";
    case 'b': return "#1a1614";
    case 'w': return "#faf7f2";
    case 'r': return "#b05a42";
    case 'g': return "#bfcdba";
    case 'm': return "#cbb89c";
    case 'j': return look.hat.empty() ? "#cbb89c" : look.hat;
    case 'y': return look.btn.empty() ? "#e2b63c" : look.btn;
    case 'c': return look.cloth.empty() ? "#a2947f" : look.cloth;
    case 'd': return look.dark.empty() ? "#857a68" : look.dark;
    default: return "#5c5145";
  }
}

std::vector<std::string> FigureRows(const Simulation& sim, const CrewMember& crew,
                                    bool hatted) {
  const Catalog& art = sim.World();
  std::vector<std::string> out = art.sprite.rows;
  const std::string style = crew.hairStyle.empty() ? "cowlick" : crew.hairStyle;
  auto it = art.hairStyles.find(style);
  if (it != art.hairStyles.end()) {
    for (const auto& row : it->second.rows) {
      if (row.first >= 0 && row.first < static_cast<int>(out.size())) {
        out[static_cast<std::size_t>(row.first)] = row.second;
      }
    }
  }
  // NOTHING POKES OUT OF A HELMET.
  if (hatted) {
    for (int r = 0; r < 4 && r < static_cast<int>(out.size()); ++r) {
      out[static_cast<std::size_t>(r)] = art.spriteBlank;
    }
  }
  // Row 9 rebuilt with a taller eye and a lash tick at each outer corner, then
  // SPLICED into whatever row 9 the style left behind.
  if (crew.sex == "f" && out.size() > 9) {
    const std::string& r9 = out[9];
    if (r9.size() >= 13) {
      out[9] = r9.substr(0, 5) + "eeesseee" + r9.substr(13);
    }
  }
  return out;
}

std::vector<SpritePixel> Rasterise(const Simulation& sim, const CrewMember& crew,
                                   const CrewLook& look, const SpriteFrame& frame,
                                   double ox, double oy) {
  std::vector<SpritePixel> pixels;
  const double px = sim.World().spritePx;
  for (std::size_t r = 0; r < frame.rows.size(); ++r) {
    const std::string& row = frame.rows[r];
    for (std::size_t q = 0; q < row.size(); ++q) {
      const char ch = row[q];
      if (ch == '.') { continue; }
      SpritePixel pixel;
      pixel.box = Area{ox + static_cast<double>(q) * px,
                       oy + static_cast<double>(r) * px, px, px};
      pixel.colour = PixelColour(sim, crew, look, ch);
      pixels.push_back(pixel);
    }
  }
  return pixels;
}

CrewScene BuildCrewScene(const Simulation& sim, const CrewMember& crew) {
  const Catalog& art = sim.World();
  CrewScene scene;
  const OrderSheet* sheet = sim.SheetOf(crew);
  const bool atDoc = sim.FindOngoing("doc-" + crew.id) != nullptr;
  const bool doc = (sheet != nullptr && sheet->Has("doc")) || atDoc;
  const bool schooling = sheet != nullptr && (sheet->Has("learnOn") || sheet->Has("learnSt"));

  CrewLook look = LookForCrew(sim, crew);
  const auto prop = [&](const std::string& name, double x, double y) {
    auto it = art.props.find(name);
    if (it == art.props.end()) { return; }
    scene.propNames.push_back(name);
    for (SpritePixel& pixel : Rasterise(sim, crew, look, it->second, x, y)) {
      scene.pixels.push_back(pixel);
    }
  };

  if (doc) {
    look.pose = "lie";
    scene.pose = "lie";
    prop("bed", 226, OnDeck(sim, art.props.at("bed").rows.size()));
    // The cross above the cot: no sprite of its own, three quiet bars.
    scene.propNames.push_back("cross");
    scene.pixels.push_back( SpritePixel{Area{252, 138, 3, 15}, "#b05a42"});
    scene.pixels.push_back( SpritePixel{Area{246, 144, 15, 3}, "#b05a42"});
    const double lieOn = OnDeck(sim, art.props.at("bed").rows.size()) -
                         static_cast<double>(art.lie.rows.size()) * art.spritePx + 12;
    scene.figureX = 230;
    scene.figureY = lieOn;
    for (SpritePixel& pixel : Rasterise(sim, crew, look, art.lie, 230, lieOn)) {
      scene.pixels.push_back(pixel);
    }
    scene.signature = crew.id + "|lie|" + look.cloth;
    return scene;
  }

  if (schooling) {
    prop("desk", 120, OnDeck(sim, art.props.at("desk").rows.size()));
  }

  const std::string jobId = sheet != nullptr && sheet->hasJob ? sheet->job : crew.job;
  if (!jobId.empty()) {
    const std::string tool = sim.World().jobTool.count(jobId) != 0
                                 ? sim.World().jobTool.at(jobId)
                                 : std::string();
    if (sheet != nullptr && sheet->hasJob && sheet->job != crew.job) {
      prop("crate", 170, OnDeck(sim, art.props.at("crate").rows.size()));
    }
    if (!tool.empty() && art.props.count(tool) != 0) {
      prop(tool, 312, sim.World().floorY - 14 -
                          static_cast<double>(art.props.at(tool).rows.size()) *
                              sim.World().spritePx);
    }
  } else if (!schooling) {
    // Nobody wants him: sat on the deck, zzz overhead.
    look.pose = "sit";
    scene.pose = "sit";
    scene.figureX = 244;
    scene.figureY = OnDeck(sim, art.sit.rows.size());
    for (SpritePixel& pixel : Rasterise(sim, crew, look, art.sit, 244, scene.figureY)) {
      scene.pixels.push_back(pixel);
    }
    scene.propNames.push_back("zzz");
    scene.signature = crew.id + "|sit|" + look.cloth;
    return scene;
  }

  const bool hatted = !look.hatKind.empty();
  const std::vector<std::string> rows = FigureRows(sim, crew, hatted);
  SpriteFrame figure;
  figure.rows = rows;
  scene.figureX = 244;
  scene.figureY = OnDeck(sim, rows.size());
  for (SpritePixel& pixel : Rasterise(sim, crew, look, figure, 244, scene.figureY)) {
    scene.pixels.push_back(pixel);
  }
  if (hatted && art.hats.count(look.hatKind) != 0) {
    // The hat goes on last, over the hair.
    for (SpritePixel& pixel : Rasterise(sim, crew, look, art.hats.at(look.hatKind), 252,
                                       OnDeck(sim, rows.size()) + 12)) {
      scene.pixels.push_back(pixel);
    }
  }
  scene.signature = crew.id + "|stand|" + look.cloth + "|" +
                    (look.hatKind.empty() ? "-" : look.hatKind) + "|" + crew.hairStyle;
  for (const std::string& name : scene.propNames) { scene.signature += "|" + name; }
  (void)FirstName;
  return scene;
}

}  // namespace ship
