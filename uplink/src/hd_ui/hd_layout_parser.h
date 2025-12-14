/*
 * HD_UI Layout Parser
 * Parses XML layout files into UIObject trees using TinyXML2
 */

#ifndef _included_hd_layout_parser_h
#define _included_hd_layout_parser_h

#include "hd_ui_object.h"
#include <string>
#include <memory>
#include <map>


// Forward declare tinyxml2 types
namespace tinyxml2 {
    class XMLElement;
    class XMLDocument;
}

namespace HDUI {

class LayoutParser {
public:
    LayoutParser();
    ~LayoutParser();
    
    // Load a layout from XML file
    std::unique_ptr<UIObject> LoadLayout(const std::string& filepath);
    
    // Parse a layout from XML string
    std::unique_ptr<UIObject> ParseLayout(const std::string& xml);
    
    // Set base path for finding assets (images, fonts)
    void SetAssetBasePath(const std::string& path);
    
    // Get layout dimensions
    int GetLayoutWidth() const { return layoutWidth; }
    int GetLayoutHeight() const { return layoutHeight; }
    
private:
    // Parse element tree recursively
    std::unique_ptr<UIObject> ParseElement(tinyxml2::XMLElement* elem);
    
    // Element type parsers
    std::unique_ptr<UIObject> ParseContainer(tinyxml2::XMLElement* elem);
    std::unique_ptr<UIObject> ParseGradient(tinyxml2::XMLElement* elem);
    std::unique_ptr<UIObject> ParseRectangle(tinyxml2::XMLElement* elem);
    std::unique_ptr<UIObject> ParseLine(tinyxml2::XMLElement* elem);
    std::unique_ptr<UIObject> ParseTextPoint(tinyxml2::XMLElement* elem);
    std::unique_ptr<UIObject> ParseImage(tinyxml2::XMLElement* elem);
    std::unique_ptr<UIObject> ParseButtonStandard(tinyxml2::XMLElement* elem);
    std::unique_ptr<UIObject> ParseButtonTextField(tinyxml2::XMLElement* elem);
    
    // Parse common attributes into UIObject
    void ParseCommonAttributes(tinyxml2::XMLElement* elem, UIObject* obj);
    
    // Helper to read attributes with defaults
    float GetFloatAttr(tinyxml2::XMLElement* elem, const char* name, float def = 0.0f);
    int GetIntAttr(tinyxml2::XMLElement* elem, const char* name, int def = 0);
    std::string GetStringAttr(tinyxml2::XMLElement* elem, const char* name, const char* def = "");
    
    std::string assetBasePath;
    int layoutWidth;
    int layoutHeight;
};

// Layout manager - caches loaded layouts
class LayoutManager {
public:
    static LayoutManager& GetInstance();
    
    // Load and cache a layout
    UIObject* GetLayout(const std::string& name);
    
    // Set base directory for layouts
    void SetLayoutDirectory(const std::string& dir);
    void SetAssetDirectory(const std::string& dir);
    
    // Clear all cached layouts
    void ClearCache();
    
private:
    LayoutManager();
    ~LayoutManager();
    
    std::string layoutDir;
    std::string assetDir;
    std::map<std::string, std::unique_ptr<UIObject>> cache;
};

} // namespace HDUI

#endif // _included_hd_layout_parser_h
