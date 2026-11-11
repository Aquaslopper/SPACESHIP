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

// The stage paints the sprite tables at one and a half times the size they
// give, so a figure made of rectangles stands about as tall as a picture.
double SpriteCell(const Simulation& sim) { return sim.World().spritePx * 1.5; }

double OnDeck(const Simulation& sim, std::size_t rows) {
  return kCrewDeckY - static_cast<double>(rows) * SpriteCell(sim);
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
                                   double ox, double oy, double cell) {
  std::vector<SpritePixel> pixels;
  const double px = cell > 0.0 ? cell : SpriteCell(sim);
  for (std::size_t r = 0; r < frame.rows.size(); ++r) {
    const std::string& row = frame.rows[r];
    for (std::size_t q = 0; q < row.size(); ++q) {
      const char ch = row[q];
      if (ch == '.') { continue; }
      SpritePixel pixel;
      const float x = static_cast<float>(ox + static_cast<double>(q) * px);
      const float y = static_cast<float>(oy + static_cast<double>(r) * px);
      const float size = static_cast<float>(px);
      pixel.box = Area{x, y, size, size};
      pixel.colour = PixelColour(sim, crew, look, ch);
      pixels.push_back(pixel);
    }
  }
  return pixels;
}

CrewScene BuildCrewScene(const Simulation& sim, const CrewMember& crew) {
  const Catalog& art = sim.World();
  CrewScene scene;
  scene.portrait = crew.id + "-portrait";
  const double cell = SpriteCell(sim);
  const OrderSheet* sheet = sim.SheetOf(crew);
  const bool atDoc = sim.FindOngoing("doc-" + crew.id) != nullptr;
  const bool doc = (sheet != nullptr && sheet->Has("doc")) || atDoc;
  const bool schooling = sheet != nullptr && (sheet->Has("learnOn") || sheet->Has("learnSt"));

  CrewLook look = LookForCrew(sim, crew);
  // `side` is the side of one cell; zero takes the stage's own.
  const auto prop = [&](const std::string& name, double x, double y, double side = 0.0) {
    auto it = art.props.find(name);
    if (it == art.props.end()) { return; }
    scene.propNames.push_back(name);
    for (SpritePixel& pixel : Rasterise(sim, crew, look, it->second, x, y, side)) {
      scene.pixels.push_back(pixel);
    }
  };
  // Where a grid of cells starts when its middle is at `middle`.
  const auto leftOf = [&](const std::vector<std::string>& rows, double middle = kCrewFigureX) {
    const std::size_t cols = rows.empty() ? 0 : rows.front().size();
    return middle - static_cast<double>(cols) * cell / 2.0;
  };
  // The middles of the bare wall left and right of the door.
  const double wallLeft = kCrewFigureX - (kCrewDoorW + kCrewBareWall) / 2.0;
  const double wallRight = kCrewFigureX + (kCrewDoorW + kCrewBareWall) / 2.0;
  // The figure as a picture: "<id>-<pose>", feet on the deck line, wearing the
  // colour the rectangles would wear.
  const auto picture = [&](const std::string& pose) {
    scene.figureArt.push_back(
        ScenePicture{crew.id + "-" + pose, kCrewFigureX, kCrewDeckY, look.cloth});
  };

  if (doc) {
    look.pose = "lie";
    scene.pose = "lie";
    const SpriteFrame& bed = art.props.at("bed");
    const double bedX = leftOf(bed.rows);
    const double bedY = OnDeck(sim, bed.rows.size());
    // The sign hangs on the wall beside the door. It goes up first: a picture
    // of the patient brings its own cot and replaces the one here.
    if (art.props.count("cross") != 0) {
      prop("cross", leftOf(art.props.at("cross").rows, wallLeft), bedY - 21.0 * cell);
    }
    scene.figureFrom = scene.pixels.size();
    prop("bed", bedX, bedY);
    scene.figureX = bedX + cell;
    scene.figureY = bedY - static_cast<double>(art.lie.rows.size()) * cell + 3.0 * cell;
    for (SpritePixel& pixel :
         Rasterise(sim, crew, look, art.lie, scene.figureX, scene.figureY)) {
      scene.pixels.push_back(pixel);
    }
    picture("lie");
    scene.signature = crew.id + "|lie|" + look.cloth;
    return scene;
  }

  if (schooling) {
    prop("desk", kCrewFigureX - 24.0 * cell, OnDeck(sim, art.props.at("desk").rows.size()));
  }

  const std::string jobId = sheet != nullptr && sheet->hasJob ? sheet->job : crew.job;
  if (!jobId.empty()) {
    const std::string tool = sim.World().jobTool.count(jobId) != 0
                                 ? sim.World().jobTool.at(jobId)
                                 : std::string();
    if (sheet != nullptr && sheet->hasJob && sheet->job != crew.job) {
      prop("crate", kCrewFigureX - 19.5 * cell, OnDeck(sim, art.props.at("crate").rows.size()));
    }
    if (!tool.empty() && art.props.count(tool) != 0) {
      // The tool leans on the bare wall, its foot where the wall meets the floor.
      prop(tool, leftOf(art.props.at(tool).rows, wallRight),
           OnDeck(sim, art.props.at(tool).rows.size()) - 3.5 * cell);
    }
  } else if (!schooling) {
    // Nobody wants him: sat on the deck, zzz overhead.
    look.pose = "sit";
    scene.pose = "sit";
    // The zzz is kept small and quiet: at the size its table gives, in a soft
    // brown, above the head and far enough to the right to clear a high bun.
    const std::size_t zzzFrom = scene.pixels.size();
    prop("zzz", kCrewFigureX + 42.0, kCrewDeckY - 204.0, art.spritePx);
    for (std::size_t i = zzzFrom; i < scene.pixels.size(); ++i) {
      scene.pixels[i].colour = "#a89476";
    }
    scene.figureFrom = scene.pixels.size();
    scene.figureX = leftOf(art.sit.rows);
    scene.figureY = OnDeck(sim, art.sit.rows.size());
    for (SpritePixel& pixel :
         Rasterise(sim, crew, look, art.sit, scene.figureX, scene.figureY)) {
      scene.pixels.push_back(pixel);
    }
    // A sitting picture if there is one; otherwise the standing one dozes.
    picture("sit");
    picture("stand");
    scene.signature = crew.id + "|sit|" + look.cloth;
    return scene;
  }

  const bool hatted = !look.hatKind.empty();
  const std::vector<std::string> rows = FigureRows(sim, crew, hatted);
  SpriteFrame figure;
  figure.rows = rows;
  scene.figureFrom = scene.pixels.size();
  scene.figureX = leftOf(rows);
  scene.figureY = OnDeck(sim, rows.size());
  for (SpritePixel& pixel :
       Rasterise(sim, crew, look, figure, scene.figureX, scene.figureY)) {
    scene.pixels.push_back(pixel);
  }
  if (hatted && art.hats.count(look.hatKind) != 0) {
    // The hat goes on last, over the hair.
    for (SpritePixel& pixel : Rasterise(sim, crew, look, art.hats.at(look.hatKind),
                                       scene.figureX + 2.0 * cell,
                                       scene.figureY + 3.0 * cell)) {
      scene.pixels.push_back(pixel);
    }
  }
  picture("stand");
  scene.signature = crew.id + "|stand|" + look.cloth + "|" +
                    (look.hatKind.empty() ? "-" : look.hatKind) + "|" + crew.hairStyle;
  for (const std::string& name : scene.propNames) { scene.signature += "|" + name; }
  (void)FirstName;
  return scene;
}

}  // namespace ship
