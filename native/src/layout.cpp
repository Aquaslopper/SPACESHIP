#include "layout.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "htautil.h"

namespace hta {
namespace {

// CSS 2.1: 10.3.3 automatic widths for normal-flow blocks. The reference sets
// `* { box-sizing:border-box }`, so an authored width already includes padding
// and border; an auto width always spans the containing block minus margins.
float marginOf(const std::string& raw, const Computed& style, float base) {
  return parsePx(raw, style.fontSize, base);
}

bool autoOf(const std::string& raw) { return isAuto(raw); }

}  // namespace

void LayoutEngine::layout(Document& doc, StyleEngine& styles, int viewportW, int viewportH) {
  styles_ = &styles;
  viewportW_ = viewportW;
  viewportH_ = viewportH;

  Node* html = doc.root;
  html->box = Node::Box();
  html->box.w = static_cast<float>(viewportW);
  html->box.h = static_cast<float>(viewportH);
  html->box.laidOut = true;
  html->box.visible = true;

  // html, body { width:100%; height:100%; overflow:hidden } in the reference.
  Node* body = doc.body;
  body->box = Node::Box();
  body->box.w = static_cast<float>(viewportW);
  body->box.h = static_cast<float>(viewportH);
  body->box.laidOut = true;
  body->box.visible = true;

  const Computed& bodyStyle = styles.style(body);
  layoutChildren(body, bodyStyle);
}

void LayoutEngine::layoutChildren(Node* node, const Computed& style) {
  Flow flow;
  flow.x = node->box.x + node->box.bdL + node->box.padL;
  flow.y = node->box.y + node->box.bdT + node->box.padT;
  flow.w = node->box.w - node->box.bdL - node->box.bdR - node->box.padL - node->box.padR;
  flow.h = node->box.h - node->box.bdT - node->box.bdB - node->box.padT - node->box.padB;
  flow.blockW = flow.w;
  if (flow.w < 0) flow.w = 0;

  float y = flow.y;                 // block flow cursor
  float inlineStart = -1;           // start of an open inline run
  float inlineBottom = y;           // bottom of floats seen in the run

  auto flushInline = [&]() {
    if (inlineStart < 0) return;
    float used = 0;
    layoutInlineRun(node, style, flow, inlineStart, used);
    y = std::max(y, inlineStart + used);
    inlineStart = -1;
  };

  for (Node* child : node->children) {
    const Computed& cs = styles_->style(child);
    if (cs.display == "none") {
      child->box = Node::Box();
      continue;
    }
    if (child->type == NodeType::Text) {
      if (inlineStart < 0) inlineStart = y;
      continue;
    }
    if (cs.position == "absolute" || cs.position == "fixed") {
      continue;  // resolved after the flow has settled
    }
    bool inlineLevel = isInlineLevel(cs) || isReplaced(child);
    if (inlineLevel && cs.floatDir == "none") {
      if (inlineStart < 0) inlineStart = y;
      continue;
    }
    flushInline();
    Flow childFlow = flow;
    childFlow.y = y;
    if (cs.floatDir == "left" || cs.floatDir == "right") {
      // Rails: floated cells sit on the current line and do not advance it.
      layoutElement(child, cs, childFlow, false);
      child->box.y = y;
      float marginL = marginOf(cs.rawMarginL, cs, flow.w);
      float marginR = marginOf(cs.rawMarginR, cs, flow.w);
      if (cs.floatDir == "left") {
        child->box.x = flow.x + marginL;
      } else {
        child->box.x = flow.x + flow.w - child->box.w - marginR;
      }
      inlineBottom = std::max(inlineBottom, child->box.y + child->box.h);
      if (inlineStart < 0) inlineStart = y;
      continue;
    }
    layoutElement(child, cs, childFlow, false);
    float marginT = marginOf(cs.rawMarginT, cs, flow.w);
    float marginB = marginOf(cs.rawMarginB, cs, flow.w);
    child->box.y = y + marginT;
    y = std::max(y, child->box.y + child->box.h + marginB);
  }
  flushInline();
  y = std::max(y, inlineBottom);

  // Absolutely positioned children are anchored to this node's padding box,
  // which is only final now that in-flow heights are known.
  for (Node* child : node->children) {
    const Computed& cs = styles_->style(child);
    if (cs.display == "none" || child->type != NodeType::Element) continue;
    if (cs.position != "absolute" && cs.position != "fixed") continue;
    Flow abs;
    abs.x = node->box.x + node->box.bdL;
    abs.y = node->box.y + node->box.bdT;
    abs.w = node->box.w - node->box.bdL - node->box.bdR;
    abs.h = node->box.h - node->box.bdT - node->box.bdB;
    abs.blockW = abs.w;
    layoutElement(child, cs, abs, false);
  }

  // Scroll extents (used by #feedwrap / #content / #taskbar and by the script).
  float maxBottom = node->box.y + node->box.h;
  float maxRight = node->box.x + node->box.w;
  for (Node* child : node->children) {
    const Computed& cs = styles_->style(child);
    if (cs.display == "none") continue;
    maxBottom = std::max(maxBottom, child->box.y + child->box.h);
    maxRight = std::max(maxRight, child->box.x + child->box.w);
  }
  node->box.scrollH = maxBottom - (node->box.y + node->box.bdT) + node->box.padB;
  node->box.scrollW = maxRight - (node->box.x + node->box.bdL) + node->box.padR;
}

// Lays out one element. `shrinkToFit` is used for inline-blocks (their width is
// decided by their content when no width is authored).
void LayoutEngine::layoutElement(Node* node, const Computed& style, const Flow& flow,
                                 bool shrinkToFit) {
  Node::Box box;
  box.laidOut = true;
  box.visible = style.visibility != "hidden";
  box.lineHeight = style.lineHeight;
  box.padL = parsePx(style.rawPadL, style.fontSize, flow.blockW);
  box.padR = parsePx(style.rawPadR, style.fontSize, flow.blockW);
  box.padT = parsePx(style.rawPadT, style.fontSize, flow.blockW);
  box.padB = parsePx(style.rawPadB, style.fontSize, flow.blockW);
  box.bdL = style.borderL.width;
  box.bdR = style.borderR.width;
  box.bdT = style.borderT.width;
  box.bdB = style.borderB.width;

  float marginL = marginOf(style.rawMarginL, style, flow.blockW);
  float marginR = marginOf(style.rawMarginR, style, flow.blockW);
  bool widthAuto = autoOf(style.rawWidth);
  bool heightAuto = autoOf(style.rawHeight);
  bool absolute = style.position == "absolute" || style.position == "fixed";

  if (absolute) {
    bool hasLeft = !autoOf(style.rawLeft), hasRight = !autoOf(style.rawRight);
    bool hasTop = !autoOf(style.rawTop), hasBottom = !autoOf(style.rawBottom);
    float left = hasLeft ? parsePx(style.rawLeft, style.fontSize, flow.w) : 0;
    float right = hasRight ? parsePx(style.rawRight, style.fontSize, flow.w) : 0;
    float top = hasTop ? parsePx(style.rawTop, style.fontSize, flow.h) : 0;
    float bottom = hasBottom ? parsePx(style.rawBottom, style.fontSize, flow.h) : 0;

    if (!widthAuto) {
      box.w = parsePx(style.rawWidth, style.fontSize, flow.w) + marginL + marginR;
    } else if (hasLeft && hasRight) {
      box.w = flow.w - left - right - marginL - marginR;
    } else {
      box.w = 0;  // shrink-to-fit after content is known
    }
    if (!heightAuto) {
      box.h = parsePx(style.rawHeight, style.fontSize, flow.h);
    } else if (hasTop && hasBottom) {
      box.h = flow.h - top - bottom;
    } else {
      box.h = 0;
    }
    box.x = flow.x + left + marginL;
    if (!hasLeft && hasRight) box.x = flow.x + flow.w - right - (box.w - marginL - marginR) - marginR;
    box.y = flow.y + top;
    node->box = box;
    if (!isReplaced(node)) {
      layoutChildren(node, style);
      if (widthAuto && !(hasLeft && hasRight)) {
        // shrink-to-fit: widest child or text run
        float widest = 0;
        for (Node* c : node->children) {
          const Computed& cs = styles_->style(c);
          if (cs.display == "none") continue;
          widest = std::max(widest, c->box.w + c->box.x - (node->box.x + node->box.bdL + node->box.padL));
        }
        std::string text = node->textContent();
        if (!text.empty()) {
          std::vector<std::string> lines = wrapText(text, style, 1e6f);
          for (const std::string& line : lines)
            widest = std::max(widest, textmetric::advance(line, style.fontFamily, style.fontSize,
                                                          style.weight, style.italic, style.letterSpacing));
        }
        node->box.w = widest + node->box.padL + node->box.padR + node->box.bdL + node->box.bdR;
      }
      if (heightAuto && !(hasTop && hasBottom)) {
        float bottomMost = node->box.y + node->box.bdT + node->box.padT;
        for (Node* c : node->children) {
          const Computed& cs = styles_->style(c);
          if (cs.display == "none" || cs.position == "absolute" || cs.position == "fixed") continue;
          bottomMost = std::max(bottomMost, c->box.y + c->box.h);
        }
        std::string text = node->textContent();
        if (!text.empty()) {
          bottomMost = std::max(bottomMost, node->box.y + node->box.bdT + node->box.padT + style.lineHeight);
        }
        node->box.h = bottomMost - node->box.y + node->box.padB;
      }
    } else if (heightAuto) {
      node->box.h = 20;
    }
    return;
  }

  // Normal flow / float / inline-block.
  if (!widthAuto) {
    box.w = parsePx(style.rawWidth, style.fontSize, flow.blockW) + marginL + marginR;
  } else if (shrinkToFit || isInlineLevel(style)) {
    box.w = 0;  // measured from content below
  } else {
    box.w = flow.blockW - marginL - marginR;
  }
  std::string maxRaw = style.rawMaxW;
  if (!autoOf(maxRaw) && maxRaw != "none") {
    float maxW = parsePx(maxRaw, style.fontSize, flow.blockW);
    if (box.w > maxW) box.w = maxW;
  }
  float minW = parsePx(style.rawMinW, style.fontSize, flow.blockW);
  if (box.w < minW) box.w = minW;

  box.x = flow.x + marginL;
  if (!widthAuto) {
    // `margin:0 auto` centres a fixed-width block; #stage relies on it.
    bool leftAuto = autoOf(style.rawMarginL);
    bool rightAuto = autoOf(style.rawMarginR);
    if (leftAuto && rightAuto) {
      box.x = flow.x + std::max(0.0f, (flow.blockW - box.w) / 2.0f);
    } else if (leftAuto) {
      box.x = flow.x + std::max(0.0f, flow.blockW - box.w - marginR);
    }
  }
  box.y = flow.y;
  node->box = box;

  if (isReplaced(node)) {
    if (heightAuto) {
      if (node->tag == "input" || node->tag == "select") box.h = 20;
      else if (node->tag == "button") box.h = 22;
      else box.h = 16;
    } else {
      box.h = parsePx(style.rawHeight, style.fontSize, flow.h);
    }
    if (widthAuto && node->tag == "input") box.w = 120;
    node->box = box;
    return;
  }

  layoutChildren(node, style);

  float contentTop = node->box.y + node->box.bdT + node->box.padT;
  float contentBottom = contentTop;
  float contentRight = node->box.x + node->box.bdL + node->box.padL;
  for (Node* c : node->children) {
    const Computed& cs = styles_->style(c);
    if (cs.display == "none" || cs.position == "absolute" || cs.position == "fixed") continue;
    contentBottom = std::max(contentBottom, c->box.y + c->box.h + marginOf(cs.rawMarginB, cs, flow.h));
    contentRight = std::max(contentRight, c->box.x + c->box.w + marginOf(cs.rawMarginR, cs, flow.w));
  }
  std::string text = node->textContent();
  float innerW = node->box.w - node->box.padL - node->box.padR - node->box.bdL - node->box.bdR;
  if (!text.empty()) {
    std::vector<std::string> lines = wrapText(text, style, innerW > 1 ? innerW : 1e6f);
    float textHeight = std::max(1.0f, static_cast<float>(lines.size())) * style.lineHeight;
    contentBottom = std::max(contentBottom, contentTop + textHeight + node->box.padB);
    if ((shrinkToFit || isInlineLevel(style)) && widthAuto) {
      float widest = 0;
      for (const std::string& line : lines)
        widest = std::max(widest, textmetric::advance(line, style.fontFamily, style.fontSize,
                                                      style.weight, style.italic, style.letterSpacing));
      node->box.w = widest + node->box.padL + node->box.padR + node->box.bdL + node->box.bdR;
    }
  } else if ((shrinkToFit || isInlineLevel(style)) && widthAuto) {
    node->box.w = contentRight - node->box.x + node->box.padR;
  }

  if (!heightAuto) {
    node->box.h = parsePx(style.rawHeight, style.fontSize, flow.h);
  } else {
    node->box.h = contentBottom - node->box.y;
  }
  std::string maxHeightRaw = style.rawMaxH;
  if (!autoOf(maxHeightRaw) && maxHeightRaw != "none") {
    float maxH = parsePx(maxHeightRaw, style.fontSize, flow.h);
    if (node->box.h > maxH) node->box.h = maxH;
  }
  float minH = parsePx(style.rawMinH, style.fontSize, flow.h);
  if (node->box.h < minH) node->box.h = minH;
}

// Places an open run of text nodes and inline elements starting at `startY`.
// The reference only builds single-line runs (labels, badges, status text), so
// this is a single line box that wraps when a run is genuinely too long.
void LayoutEngine::layoutInlineRun(Node* parent, const Computed& style, const Flow& flow,
                                   float startY, float& usedHeight) {
  float lineHeight = style.lineHeight;
  float x = flow.x;
  float y = startY;
  float widest = 0;
  bool any = false;

  auto place = [&](Node* node, const Computed& cs, const std::string& text) {
    any = true;
    if (node) {
      if (node->type == NodeType::Element) {
        Flow f = flow;
        f.y = y;
        layoutElement(node, cs, f, true);
        node->box.x = x;
        node->box.y = y;
        x += node->box.w;
        lineHeight = std::max(lineHeight, node->box.h);
        return;
      }
      std::string t = text;
      if (cs.whiteSpace != "pre") {
        std::string collapsed;
        bool space = false;
        for (char c : t) {
          if (textmetric::isSpace(c)) {
            if (!space) collapsed.push_back(' ');
            space = true;
          } else {
            collapsed.push_back(c);
            space = false;
          }
        }
        t = trim(collapsed);
      }
      float w = textmetric::advance(t, cs.fontFamily, cs.fontSize, cs.weight, cs.italic,
                                    cs.letterSpacing);
      if (x + w > flow.x + flow.w && x > flow.x) {
        y += lineHeight;
        x = flow.x;
      }
      x += w;
      widest = std::max(widest, x - flow.x);
      lineHeight = std::max(lineHeight, cs.lineHeight);
      return;
    }
    (void)text;
  };

  for (Node* child : parent->children) {
    const Computed& cs = styles_->style(child);
    if (cs.display == "none") continue;
    if (child->type == NodeType::Element) {
      if (cs.position == "absolute" || cs.position == "fixed") continue;
      if (cs.floatDir != "none") continue;
      if (!isInlineLevel(cs) && !isReplaced(child)) continue;
      place(child, cs, "");
    } else {
      place(nullptr, cs, child->text);
    }
  }
  if (!any) {
    usedHeight = lineHeight;
    return;
  }
  usedHeight = y - startY + lineHeight;
}

Node* LayoutEngine::hitTest(Document& doc, float x, float y) const {
  Node* best = doc.body;
  std::function<void(Node*)> walk = [&](Node* node) {
    for (auto it = node->children.rbegin(); it != node->children.rend(); ++it) {
      Node* child = *it;
      if (child->type != NodeType::Element) continue;
      const Computed& cs = styles_->style(child);
      if (cs.display == "none") continue;
      if (!child->box.laidOut) continue;
      if (!child->box.contains(x, y)) continue;
      if (cs.pointerEventsNone) continue;
      best = child;
      walk(child);
      return;
    }
  };
  walk(doc.body);
  return best;
}

}  // namespace hta
