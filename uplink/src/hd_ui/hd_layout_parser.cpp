/*
 * HD_UI Layout Parser Implementation
 */

#include "hd_layout_parser.h"
#include <tinyxml2.h>
#include <cstdio>
#include <map>

namespace HDUI {

// ============================================================================
// LayoutParser
// ============================================================================

LayoutParser::LayoutParser()
    : layoutWidth(1920)
    , layoutHeight(1080)
{
}

LayoutParser::~LayoutParser() {
}

void LayoutParser::SetAssetBasePath(const std::string& path) {
    assetBasePath = path;
}

std::unique_ptr<UIObject> LayoutParser::LoadLayout(const std::string& filepath) {
    tinyxml2::XMLDocument doc;
    
    if (doc.LoadFile(filepath.c_str()) != tinyxml2::XML_SUCCESS) {
        fprintf(stderr, "HD_UI: Failed to load layout: %s\n", filepath.c_str());
        return nullptr;
    }
    
    tinyxml2::XMLElement* root = doc.FirstChildElement("layout");
    if (!root) {
        fprintf(stderr, "HD_UI: No <layout> root element in: %s\n", filepath.c_str());
        return nullptr;
    }
    
    // Get layout dimensions
    layoutWidth = GetIntAttr(root, "w", 1920);
    layoutHeight = GetIntAttr(root, "h", 1080);
    
    // Create root container
    auto layout = std::unique_ptr<UIContainer>(new UIContainer());
    layout->name = GetStringAttr(root, "name", "layout");
    layout->w = (float)layoutWidth;
    layout->h = (float)layoutHeight;
    
    // Parse all child elements
    for (tinyxml2::XMLElement* child = root->FirstChildElement();
         child != nullptr;
         child = child->NextSiblingElement())
    {
        auto childObj = ParseElement(child);
        if (childObj) {
            layout->AddChild(std::move(childObj));
        }
    }
    
    printf("HD_UI: Loaded layout '%s' (%dx%d)\n", 
           layout->name.c_str(), layoutWidth, layoutHeight);
    
    return layout;
}

std::unique_ptr<UIObject> LayoutParser::ParseLayout(const std::string& xml) {
    tinyxml2::XMLDocument doc;
    
    if (doc.Parse(xml.c_str()) != tinyxml2::XML_SUCCESS) {
        fprintf(stderr, "HD_UI: Failed to parse XML layout\n");
        return nullptr;
    }
    
    tinyxml2::XMLElement* root = doc.FirstChildElement("layout");
    if (!root) {
        return nullptr;
    }
    
    layoutWidth = GetIntAttr(root, "w", 1920);
    layoutHeight = GetIntAttr(root, "h", 1080);
    
    auto layout = std::unique_ptr<UIContainer>(new UIContainer());
    layout->name = GetStringAttr(root, "name", "layout");
    layout->w = (float)layoutWidth;
    layout->h = (float)layoutHeight;
    
    for (tinyxml2::XMLElement* child = root->FirstChildElement();
         child != nullptr;
         child = child->NextSiblingElement())
    {
        auto childObj = ParseElement(child);
        if (childObj) {
            layout->AddChild(std::move(childObj));
        }
    }
    
    return layout;
}

std::unique_ptr<UIObject> LayoutParser::ParseElement(tinyxml2::XMLElement* elem) {
    if (!elem) return nullptr;
    
    const char* tagName = elem->Name();
    std::unique_ptr<UIObject> obj;
    
    if (strcmp(tagName, "Container") == 0) {
        obj = ParseContainer(elem);
    } else if (strcmp(tagName, "Gradient") == 0) {
        obj = ParseGradient(elem);
    } else if (strcmp(tagName, "Rectangle") == 0) {
        obj = ParseRectangle(elem);
    } else if (strcmp(tagName, "Line") == 0) {
        obj = ParseLine(elem);
    } else if (strcmp(tagName, "TextPoint") == 0) {
        obj = ParseTextPoint(elem);
    } else if (strcmp(tagName, "Image") == 0) {
        obj = ParseImage(elem);
    } else if (strcmp(tagName, "ButtonStandard") == 0) {
        obj = ParseButtonStandard(elem);
    } else if (strcmp(tagName, "ButtonTextField") == 0) {
        obj = ParseButtonTextField(elem);
    } else if (strcmp(tagName, "GradStop") == 0) {
        // GradStop is handled by ParseGradient parent
        return nullptr;
    } else {
        fprintf(stderr, "HD_UI: Unknown element type: %s\n", tagName);
        // Try parsing as generic container
        obj = ParseContainer(elem);
    }
    
    return obj;
}

std::unique_ptr<UIObject> LayoutParser::ParseContainer(tinyxml2::XMLElement* elem) {
    auto obj = std::unique_ptr<UIContainer>(new UIContainer());
    ParseCommonAttributes(elem, obj.get());
    
    // Parse children
    for (tinyxml2::XMLElement* child = elem->FirstChildElement();
         child != nullptr;
         child = child->NextSiblingElement())
    {
        auto childObj = ParseElement(child);
        if (childObj) {
            obj->AddChild(std::move(childObj));
        }
    }
    
    return obj;
}

std::unique_ptr<UIObject> LayoutParser::ParseGradient(tinyxml2::XMLElement* elem) {
    auto obj = std::unique_ptr<UIGradient>(new UIGradient());
    ParseCommonAttributes(elem, obj.get());
    obj->subtype = GetStringAttr(elem, "subtype", "gradVertical");
    
    // Parse GradStop children
    for (tinyxml2::XMLElement* child = elem->FirstChildElement("GradStop");
         child != nullptr;
         child = child->NextSiblingElement("GradStop"))
    {
        UIGradient::GradStop stop;
        stop.location = GetFloatAttr(child, "location", 0.0f);
        stop.fillColor = GetStringAttr(child, "fillColor", "000000");
        obj->stops.push_back(stop);
    }
    
    // Parse other children
    for (tinyxml2::XMLElement* child = elem->FirstChildElement();
         child != nullptr;
         child = child->NextSiblingElement())
    {
        if (strcmp(child->Name(), "GradStop") != 0) {
            auto childObj = ParseElement(child);
            if (childObj) {
                obj->AddChild(std::move(childObj));
            }
        }
    }
    
    return obj;
}

std::unique_ptr<UIObject> LayoutParser::ParseRectangle(tinyxml2::XMLElement* elem) {
    auto obj = std::unique_ptr<UIRectangle>(new UIRectangle());
    ParseCommonAttributes(elem, obj.get());
    
    obj->subtype = GetStringAttr(elem, "subtype", "filled");
    obj->fillColor = GetStringAttr(elem, "fillColor", "FFFFFF");
    obj->fillAlpha = GetFloatAttr(elem, "fillA", 1.0f);
    obj->strokeColor = GetStringAttr(elem, "strokeColor", "FFFFFF");
    obj->strokeWidth = GetFloatAttr(elem, "strokeW", 1.0f);
    
    return obj;
}

std::unique_ptr<UIObject> LayoutParser::ParseLine(tinyxml2::XMLElement* elem) {
    auto obj = std::unique_ptr<UILine>(new UILine());
    ParseCommonAttributes(elem, obj.get());
    
    obj->x1 = GetFloatAttr(elem, "x1", 0.0f);
    obj->y1 = GetFloatAttr(elem, "y1", 0.0f);
    obj->x2 = GetFloatAttr(elem, "x2", 0.0f);
    obj->y2 = GetFloatAttr(elem, "y2", 0.0f);
    obj->strokeColor = GetStringAttr(elem, "strokeColor", "FFFFFF");
    obj->strokeWidth = GetFloatAttr(elem, "strokeW", 1.0f);
    
    return obj;
}

std::unique_ptr<UIObject> LayoutParser::ParseTextPoint(tinyxml2::XMLElement* elem) {
    auto obj = std::unique_ptr<UITextPoint>(new UITextPoint());
    ParseCommonAttributes(elem, obj.get());
    
    obj->text = GetStringAttr(elem, "text", "");
    obj->font = GetStringAttr(elem, "font", "AeroMaticsRegular");
    obj->fontSize = GetIntAttr(elem, "size", 18);
    obj->fillColor = GetStringAttr(elem, "fillColor", "FFFFFF");
    
    std::string align = GetStringAttr(elem, "align", "left");
    if (align == "center") obj->align = TextAlign::Center;
    else if (align == "right") obj->align = TextAlign::Right;
    else obj->align = TextAlign::Left;
    
    return obj;
}

std::unique_ptr<UIObject> LayoutParser::ParseImage(tinyxml2::XMLElement* elem) {
    auto obj = std::unique_ptr<UIImage>(new UIImage());
    ParseCommonAttributes(elem, obj.get());
    
    obj->file = GetStringAttr(elem, "file", "");
    
    // Load the bitmap if we have an asset path
    if (!obj->file.empty() && !assetBasePath.empty()) {
        obj->LoadBitmap(assetBasePath);
    }
    
    return obj;
}

std::unique_ptr<UIObject> LayoutParser::ParseButtonStandard(tinyxml2::XMLElement* elem) {
    auto obj = std::unique_ptr<UIButtonStandard>(new UIButtonStandard());
    ParseCommonAttributes(elem, obj.get());
    
    // Parse children (gfxStandard, gfxHover, gfxClick, etc.)
    for (tinyxml2::XMLElement* child = elem->FirstChildElement();
         child != nullptr;
         child = child->NextSiblingElement())
    {
        auto childObj = ParseElement(child);
        if (childObj) {
            obj->AddChild(std::move(childObj));
        }
    }
    
    return obj;
}

std::unique_ptr<UIObject> LayoutParser::ParseButtonTextField(tinyxml2::XMLElement* elem) {
    auto obj = std::unique_ptr<UIButtonTextField>(new UIButtonTextField());
    ParseCommonAttributes(elem, obj.get());
    
    // Parse children
    for (tinyxml2::XMLElement* child = elem->FirstChildElement();
         child != nullptr;
         child = child->NextSiblingElement())
    {
        auto childObj = ParseElement(child);
        if (childObj) {
            obj->AddChild(std::move(childObj));
        }
    }
    
    return obj;
}

void LayoutParser::ParseCommonAttributes(tinyxml2::XMLElement* elem, UIObject* obj) {
    obj->name = GetStringAttr(elem, "name", "");
    obj->x = GetFloatAttr(elem, "x", 0.0f);
    obj->y = GetFloatAttr(elem, "y", 0.0f);
    obj->w = GetFloatAttr(elem, "w", 0.0f);
    obj->h = GetFloatAttr(elem, "h", 0.0f);
    obj->alpha = GetFloatAttr(elem, "alpha", 1.0f);
    
    std::string scalingStr = GetStringAttr(elem, "scalingType", "scaleNormal");
    if (scalingStr == "scaleByHeight") obj->scalingType = ScalingType::ScaleByHeight;
    else if (scalingStr == "scaleByWidth") obj->scalingType = ScalingType::ScaleByWidth;
    else obj->scalingType = ScalingType::ScaleNormal;
    
    std::string arrangeStr = GetStringAttr(elem, "arrangeType", "arrangeFree");
    if (arrangeStr == "arrangeHorizontal") obj->arrangeType = ArrangeType::ArrangeHorizontal;
    else if (arrangeStr == "arrangeVertical") obj->arrangeType = ArrangeType::ArrangeVertical;
    else obj->arrangeType = ArrangeType::ArrangeFree;
    
    obj->arrangePadding = GetFloatAttr(elem, "arrangePadding", 0.0f);
}

float LayoutParser::GetFloatAttr(tinyxml2::XMLElement* elem, const char* name, float def) {
    float val = def;
    elem->QueryFloatAttribute(name, &val);
    return val;
}

int LayoutParser::GetIntAttr(tinyxml2::XMLElement* elem, const char* name, int def) {
    int val = def;
    elem->QueryIntAttribute(name, &val);
    return val;
}

std::string LayoutParser::GetStringAttr(tinyxml2::XMLElement* elem, const char* name, const char* def) {
    const char* val = elem->Attribute(name);
    return val ? std::string(val) : std::string(def);
}

// ============================================================================
// LayoutManager
// ============================================================================

LayoutManager& LayoutManager::GetInstance() {
    static LayoutManager instance;
    return instance;
}

LayoutManager::LayoutManager() {
}

LayoutManager::~LayoutManager() {
    ClearCache();
}

void LayoutManager::SetLayoutDirectory(const std::string& dir) {
    layoutDir = dir;
}

void LayoutManager::SetAssetDirectory(const std::string& dir) {
    assetDir = dir;
}

UIObject* LayoutManager::GetLayout(const std::string& name) {
    // Check cache
    auto it = cache.find(name);
    if (it != cache.end()) {
        return it->second.get();
    }
    
    // Load layout
    std::string path = layoutDir + "/" + name;
    if (name.find(".xml") == std::string::npos) {
        path += ".xml";
    }
    
    LayoutParser parser;
    parser.SetAssetBasePath(assetDir);
    
    auto layout = parser.LoadLayout(path);
    if (!layout) {
        return nullptr;
    }
    
    UIObject* ptr = layout.get();
    cache[name] = std::move(layout);
    
    return ptr;
}

void LayoutManager::ClearCache() {
    cache.clear();
}

} // namespace HDUI
