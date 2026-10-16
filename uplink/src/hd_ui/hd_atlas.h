#ifndef _included_hd_atlas_h
#define _included_hd_atlas_h

#include <allegro5/allegro.h>
#include <map>
#include <memory>
#include <string>

namespace HDUI {
struct SubTexture {
    std::string name;
    int x=0,y=0,width=0,height=0;
    ALLEGRO_BITMAP* subBitmap=nullptr;
    // The deleter retains the parent image until the last subtexture is freed.
    std::shared_ptr<ALLEGRO_BITMAP> texture;
};
class TextureAtlas {
public:
    TextureAtlas();
    ~TextureAtlas();
    TextureAtlas(const TextureAtlas&) = delete;
    TextureAtlas& operator=(const TextureAtlas&) = delete;
    bool Load(const std::string& xmlPath);
    ALLEGRO_BITMAP* GetTexture(const std::string& name);
    std::shared_ptr<ALLEGRO_BITMAP> AcquireTexture(const std::string& name) const;
    bool HasTexture(const std::string& name) const;
    const std::string& GetImagePath() const { return imagePath; }
private:
    std::shared_ptr<ALLEGRO_BITMAP> atlasImage;
    std::string imagePath;
    std::map<std::string,SubTexture> subTextures;
};
class AtlasManager {
public:
    static AtlasManager& GetInstance();
    void SetBasePath(const std::string& path);
    bool LoadAtlas(const std::string& name);
    // Raw access is borrowed until ClearAll; UI images use AcquireTexture.
    ALLEGRO_BITMAP* GetTexture(const std::string& name);
    std::shared_ptr<ALLEGRO_BITMAP> AcquireTexture(const std::string& name) const;
    void ClearAll();
private:
    AtlasManager();
    ~AtlasManager();
    AtlasManager(const AtlasManager&) = delete;
    AtlasManager& operator=(const AtlasManager&) = delete;
    std::string basePath;
    std::map<std::string,std::unique_ptr<TextureAtlas>> atlases;
    std::map<std::string,std::shared_ptr<ALLEGRO_BITMAP>> textures;
};
} // namespace HDUI
#endif
