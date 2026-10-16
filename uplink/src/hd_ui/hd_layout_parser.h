#ifndef _included_hd_layout_parser_h
#define _included_hd_layout_parser_h

#include "hd_ui_object.h"
#include <map>
#include <memory>
#include <string>

namespace tinyxml2 { class XMLElement; }
namespace HDUI {
class LayoutParser {
public:
    LayoutParser();
    ~LayoutParser();
    std::unique_ptr<UIObject> LoadLayout(const std::string& filepath);
    std::unique_ptr<UIObject> ParseLayout(const std::string& xml);
    void SetAssetBasePath(const std::string& path);
    int GetLayoutWidth() const { return layoutWidth; }
    int GetLayoutHeight() const { return layoutHeight; }
    const std::string& GetError() const { return error; }
    // Preserve valid UTF-8, transcode invalid bytes as Windows-1252, and
    // replace characters forbidden by XML 1.0 with U+FFFD before TinyXML.
    static std::string SanitizeXML(const std::string& bytes);
private:
    std::unique_ptr<UIObject> ParseElement(tinyxml2::XMLElement* elem);
    void ParseCommonAttributes(tinyxml2::XMLElement* elem,UIObject* object);
    float GetFloatAttr(tinyxml2::XMLElement* elem,const char* name,float def=0);
    int GetIntAttr(tinyxml2::XMLElement* elem,const char* name,int def=0);
    bool GetBoolAttr(tinyxml2::XMLElement* elem,const char* name,bool def=true);
    std::string GetStringAttr(tinyxml2::XMLElement* elem,const char* name,const char* def="");
    std::string assetBasePath,error;
    int layoutWidth,layoutHeight;
};
class LayoutManager {
public:
    static LayoutManager& GetInstance();
    UIObject* GetLayout(const std::string& name);
    // Transfer the complete tree out of the cache to a composed input root.
    std::unique_ptr<UIObject> TakeLayout(const std::string& name);
    void SetLayoutDirectory(const std::string& dir);
    void SetAssetDirectory(const std::string& dir);
    void ClearCache();
private:
    LayoutManager();
    ~LayoutManager();
    LayoutManager(const LayoutManager&) = delete;
    LayoutManager& operator=(const LayoutManager&) = delete;
    std::string layoutDir,assetDir;
    std::map<std::string,std::unique_ptr<UIObject>> cache;
};
} // namespace HDUI
#endif
