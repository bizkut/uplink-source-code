/*
 * HD_UI Texture Atlas Implementation
 */

#include "hd_atlas.h"
#include <tinyxml2.h>
#include <cstdio>
#include <vector>
#include <memory>

namespace HDUI {

// ============================================================================
// TextureAtlas
// ============================================================================

TextureAtlas::TextureAtlas()
    : atlasImage(nullptr)
{
}

TextureAtlas::~TextureAtlas() {
    // Destroy sub-bitmaps first
    for (auto& pair : subTextures) {
        if (pair.second.subBitmap) {
            al_destroy_bitmap(pair.second.subBitmap);
        }
    }
    subTextures.clear();
    
    // Then destroy parent atlas
    if (atlasImage) {
        al_destroy_bitmap(atlasImage);
        atlasImage = nullptr;
    }
}

bool TextureAtlas::Load(const std::string& xmlPath) {
    tinyxml2::XMLDocument doc;
    
    if (doc.LoadFile(xmlPath.c_str()) != tinyxml2::XML_SUCCESS) {
        fprintf(stderr, "HD_UI: Failed to load atlas XML: %s\n", xmlPath.c_str());
        return false;
    }
    
    tinyxml2::XMLElement* root = doc.FirstChildElement("TextureAtlas");
    if (!root) {
        fprintf(stderr, "HD_UI: No <TextureAtlas> root in: %s\n", xmlPath.c_str());
        return false;
    }
    
    // Get image path (relative to XML file)
    const char* imgPath = root->Attribute("imagePath");
    if (!imgPath) {
        fprintf(stderr, "HD_UI: No imagePath attribute in atlas: %s\n", xmlPath.c_str());
        return false;
    }
    
    // Build full path (same directory as XML)
    std::string baseDir = xmlPath;
    size_t lastSlash = baseDir.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        baseDir = baseDir.substr(0, lastSlash + 1);
    } else {
        baseDir = "";
    }
    
    imagePath = baseDir + imgPath;
    
    // Load the atlas image
    atlasImage = al_load_bitmap(imagePath.c_str());
    if (!atlasImage) {
        fprintf(stderr, "HD_UI: Failed to load atlas image: %s\n", imagePath.c_str());
        return false;
    }
    
    printf("HD_UI: Loaded atlas image: %s (%dx%d)\n", 
           imagePath.c_str(), 
           al_get_bitmap_width(atlasImage),
           al_get_bitmap_height(atlasImage));
    
    // Parse all SubTexture elements
    int count = 0;
    for (tinyxml2::XMLElement* elem = root->FirstChildElement("SubTexture");
         elem != nullptr;
         elem = elem->NextSiblingElement("SubTexture"))
    {
        SubTexture sub;
        const char* name = elem->Attribute("name");
        if (!name) continue;
        
        sub.name = name;
        elem->QueryIntAttribute("x", &sub.x);
        elem->QueryIntAttribute("y", &sub.y);
        elem->QueryIntAttribute("width", &sub.width);
        elem->QueryIntAttribute("height", &sub.height);
        
        // Create sub-bitmap for this region
        sub.subBitmap = al_create_sub_bitmap(atlasImage, 
                                              sub.x, sub.y, 
                                              sub.width, sub.height);
        
        subTextures[sub.name] = sub;
        count++;
    }
    
    printf("HD_UI: Loaded %d sub-textures from atlas\n", count);
    return true;
}

ALLEGRO_BITMAP* TextureAtlas::GetTexture(const std::string& name) {
    auto it = subTextures.find(name);
    if (it != subTextures.end()) {
        return it->second.subBitmap;
    }
    return nullptr;
}

bool TextureAtlas::HasTexture(const std::string& name) const {
    return subTextures.find(name) != subTextures.end();
}

// ============================================================================
// AtlasManager
// ============================================================================

AtlasManager& AtlasManager::GetInstance() {
    static AtlasManager instance;
    return instance;
}

AtlasManager::AtlasManager() {
}

AtlasManager::~AtlasManager() {
    ClearAll();
}

void AtlasManager::SetBasePath(const std::string& path) {
    basePath = path;
    if (!basePath.empty() && basePath.back() != '/') {
        basePath += '/';
    }
}

bool AtlasManager::LoadAtlas(const std::string& name) {
    std::string fullPath = basePath + name;
    
    auto atlas = std::unique_ptr<TextureAtlas>(new TextureAtlas());
    if (!atlas->Load(fullPath)) {
        return false;
    }
    
    atlases.push_back(std::move(atlas));
    return true;
}

ALLEGRO_BITMAP* AtlasManager::GetTexture(const std::string& name) {
    // Search all loaded atlases
    for (auto& atlas : atlases) {
        ALLEGRO_BITMAP* tex = atlas->GetTexture(name);
        if (tex) return tex;
    }
    return nullptr;
}

void AtlasManager::ClearAll() {
    atlases.clear();
}

} // namespace HDUI
