#include "hd_atlas.h"
#include "hd_allegro5.h"
#include "hd_layout_parser.h"
#include <tinyxml2.h>
#include <cstring>
#include <fstream>
#include <iterator>

namespace HDUI {
TextureAtlas::TextureAtlas() {}
TextureAtlas::~TextureAtlas() {}
bool TextureAtlas::Load(const std::string& xmlPath) {
    std::ifstream file(xmlPath.c_str(),std::ios::binary);
    if (!file) return false;
    const std::string bytes((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
    if (file.bad()) return false;
    const std::string xml=LayoutParser::SanitizeXML(bytes);
    tinyxml2::XMLDocument document;
    if (document.Parse(xml.data(),xml.size())!=tinyxml2::XML_SUCCESS) return false;
    tinyxml2::XMLElement* root=document.FirstChildElement("TextureAtlas");
    if (!root || root!=document.FirstChildElement() || root->NextSiblingElement()) return false;
    const char* path=root->Attribute("imagePath");
    if (!path || !*path) return false;
    const size_t slash=xmlPath.find_last_of("/\\");
    const std::string directory=slash==std::string::npos ? "" : xmlPath.substr(0,slash+1);
    const std::string candidatePath=path[0]=='/' ? path : directory+path;
    ALLEGRO_BITMAP* loaded=Allegro5System::LoadBitmap(candidatePath.c_str());
    if (!loaded) return false;
    std::shared_ptr<ALLEGRO_BITMAP> image(loaded,al_destroy_bitmap);
    const int imageWidth=al_get_bitmap_width(image.get()),imageHeight=al_get_bitmap_height(image.get());
    std::map<std::string,SubTexture> definitions;
    for (tinyxml2::XMLElement* element=root->FirstChildElement();element;element=element->NextSiblingElement()) {
        if (std::strcmp(element->Name(),"SubTexture")) return false;
        const char* name=element->Attribute("name");
        if (!name || !*name || definitions.find(name)!=definitions.end()) return false;
        SubTexture sub;
        sub.name=name;
        if (element->QueryIntAttribute("x",&sub.x)!=tinyxml2::XML_SUCCESS ||
            element->QueryIntAttribute("y",&sub.y)!=tinyxml2::XML_SUCCESS ||
            element->QueryIntAttribute("width",&sub.width)!=tinyxml2::XML_SUCCESS ||
            element->QueryIntAttribute("height",&sub.height)!=tinyxml2::XML_SUCCESS) return false;
        if (sub.x<0 || sub.y<0 || sub.width<=0 || sub.height<=0 ||
            sub.x>imageWidth || sub.y>imageHeight || sub.width>imageWidth-sub.x || sub.height>imageHeight-sub.y) return false;
        ALLEGRO_BITMAP* bitmap=al_create_sub_bitmap(image.get(),sub.x,sub.y,sub.width,sub.height);
        if (!bitmap) return false;
        sub.texture=std::shared_ptr<ALLEGRO_BITMAP>(bitmap,[image](ALLEGRO_BITMAP* texture) { al_destroy_bitmap(texture); });
        sub.subBitmap=bitmap;
        definitions.emplace(sub.name,std::move(sub));
    }
    // Commit only a complete atlas. Existing image users retain their handles.
    subTextures.swap(definitions);
    atlasImage.swap(image);
    imagePath=candidatePath;
    return true;
}
std::shared_ptr<ALLEGRO_BITMAP> TextureAtlas::AcquireTexture(const std::string& name) const {
    const auto found=subTextures.find(name);
    return found==subTextures.end() ? std::shared_ptr<ALLEGRO_BITMAP>() : found->second.texture;
}
ALLEGRO_BITMAP* TextureAtlas::GetTexture(const std::string& name) {
    const auto found=subTextures.find(name);
    return found==subTextures.end() ? nullptr : found->second.subBitmap;
}
bool TextureAtlas::HasTexture(const std::string& name) const { return subTextures.find(name)!=subTextures.end(); }
AtlasManager& AtlasManager::GetInstance() { static AtlasManager manager; return manager; }
AtlasManager::AtlasManager() {}
AtlasManager::~AtlasManager() { ClearAll(); }
void AtlasManager::SetBasePath(const std::string& path) {
    std::string normalized=path;
    if (!normalized.empty() && normalized.back()!='/' && normalized.back()!='\\') normalized+='/';
    if (basePath!=normalized) { ClearAll(); basePath=normalized; }
}
bool AtlasManager::LoadAtlas(const std::string& name) {
    const std::string path=!name.empty() && name[0]=='/' ? name : basePath+name;
    if (atlases.find(path)!=atlases.end()) return true;
    std::unique_ptr<TextureAtlas> atlas(new TextureAtlas());
    if (!atlas->Load(path)) return false;
    atlases.emplace(path,std::move(atlas));
    return true;
}
std::shared_ptr<ALLEGRO_BITMAP> AtlasManager::AcquireTexture(const std::string& name) const {
    const auto cached=textures.find(name);
    if (cached!=textures.end()) return cached->second;
    for (const auto& atlas : atlases) {
        std::shared_ptr<ALLEGRO_BITMAP> texture=atlas.second->AcquireTexture(name);
        if (texture) return texture;
    }
    return std::shared_ptr<ALLEGRO_BITMAP>();
}
ALLEGRO_BITMAP* AtlasManager::GetTexture(const std::string& name) {
    const auto cached=textures.find(name);
    if (cached!=textures.end()) return cached->second.get();
    std::shared_ptr<ALLEGRO_BITMAP> texture=AcquireTexture(name);
    if (!texture) return nullptr;
    ALLEGRO_BITMAP* bitmap=texture.get(); textures.emplace(name,std::move(texture)); return bitmap;
}
void AtlasManager::ClearAll() { textures.clear(); atlases.clear(); }
} // namespace HDUI
