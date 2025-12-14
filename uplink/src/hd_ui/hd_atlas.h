/*
 * HD_UI Texture Atlas
 * Loads sprite sheets and provides sub-texture regions
 */

#ifndef _included_hd_atlas_h
#define _included_hd_atlas_h
#include <vector>
#include <memory>

#include <string>
#include <map>
#include <allegro5/allegro.h>

namespace HDUI {

// Sub-texture region within an atlas
struct SubTexture {
    std::string name;
    int x, y, width, height;
    ALLEGRO_BITMAP* subBitmap; // Created from parent
};

// Texture Atlas - loads a sprite sheet and sub-texture definitions
class TextureAtlas {
public:
    TextureAtlas();
    ~TextureAtlas();
    
    // Load atlas from XML definition file
    bool Load(const std::string& xmlPath);
    
    // Get a sub-texture by name
    ALLEGRO_BITMAP* GetTexture(const std::string& name);
    
    // Check if texture exists
    bool HasTexture(const std::string& name) const;
    
    // Get atlas image path
    const std::string& GetImagePath() const { return imagePath; }
    
private:
    ALLEGRO_BITMAP* atlasImage;
    std::string imagePath;
    std::map<std::string, SubTexture> subTextures;
};

// Global atlas manager - caches loaded atlases
class AtlasManager {
public:
    static AtlasManager& GetInstance();
    
    // Set base path for atlases
    void SetBasePath(const std::string& path);
    
    // Load an atlas
    bool LoadAtlas(const std::string& name);
    
    // Get texture by name (searches all loaded atlases)
    ALLEGRO_BITMAP* GetTexture(const std::string& name);
    
    // Clear all loaded atlases
    void ClearAll();
    
private:
    AtlasManager();
    ~AtlasManager();
    
    std::string basePath;
    std::vector<std::unique_ptr<TextureAtlas>> atlases;
};

} // namespace HDUI

#endif // _included_hd_atlas_h
