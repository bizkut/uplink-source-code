#include "hd_layout_parser.h"
#include <tinyxml2.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

namespace HDUI {
namespace {
void AppendUTF8(std::string& output,unsigned codepoint) {
    if (codepoint<0x80) output.push_back(static_cast<char>(codepoint));
    else if (codepoint<0x800) {
        output.push_back(static_cast<char>(0xc0|(codepoint>>6)));
        output.push_back(static_cast<char>(0x80|(codepoint&63)));
    } else if (codepoint<0x10000) {
        output.push_back(static_cast<char>(0xe0|(codepoint>>12)));
        output.push_back(static_cast<char>(0x80|((codepoint>>6)&63)));
        output.push_back(static_cast<char>(0x80|(codepoint&63)));
    } else {
        output.push_back(static_cast<char>(0xf0|(codepoint>>18)));
        output.push_back(static_cast<char>(0x80|((codepoint>>12)&63)));
        output.push_back(static_cast<char>(0x80|((codepoint>>6)&63)));
        output.push_back(static_cast<char>(0x80|(codepoint&63)));
    }
}
bool XMLCharacter(unsigned codepoint) {
    return codepoint==9 || codepoint==10 || codepoint==13 ||
        (codepoint>=0x20 && codepoint<=0xd7ff) || (codepoint>=0xe000 && codepoint<=0xfffd) ||
        (codepoint>=0x10000 && codepoint<=0x10ffff);
}
std::string FontDirectory(const std::string& assets) {
    if (assets.empty()) return "uplinkHD/fonts";
    const size_t slash=assets.find_last_of("/\\");
    return slash==std::string::npos ? assets+"/../fonts" : assets.substr(0,slash)+"/fonts";
}
}
LayoutParser::LayoutParser() : layoutWidth(1920),layoutHeight(1080) {}
LayoutParser::~LayoutParser() {}
void LayoutParser::SetAssetBasePath(const std::string& path) {
    assetBasePath=path;
    while (!assetBasePath.empty() && (assetBasePath.back()=='/' || assetBasePath.back()=='\\')) assetBasePath.pop_back();
}
std::string LayoutParser::SanitizeXML(const std::string& bytes) {
    static const unsigned cp1252[32]={
        0x20ac,0xfffd,0x201a,0x0192,0x201e,0x2026,0x2020,0x2021,
        0x02c6,0x2030,0x0160,0x2039,0x0152,0xfffd,0x017d,0xfffd,
        0xfffd,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,
        0x02dc,0x2122,0x0161,0x203a,0x0153,0xfffd,0x017e,0x0178};
    std::string output;
    output.reserve(bytes.size());
    for (size_t i=0;i<bytes.size();) {
        const unsigned char first=static_cast<unsigned char>(bytes[i]);
        unsigned codepoint=first;
        size_t length=1;
        if (first>=0xc2 && first<=0xf4) {
            const size_t expected=first<0xe0 ? 2 : first<0xf0 ? 3 : 4;
            codepoint=first & (expected==2 ? 31 : expected==3 ? 15 : 7);
            bool valid=i+expected<=bytes.size();
            for (size_t n=1;valid && n<expected;++n) {
                const unsigned char next=static_cast<unsigned char>(bytes[i+n]);
                if ((next&0xc0)!=0x80) valid=false;
                else codepoint=(codepoint<<6)|(next&63);
            }
            const unsigned minimum=expected==2 ? 0x80 : expected==3 ? 0x800 : 0x10000;
            valid=valid && codepoint>=minimum && codepoint<=0x10ffff && !(codepoint>=0xd800 && codepoint<=0xdfff);
            if (valid) length=expected;
            else codepoint=first;
        }
        if (length==1 && first>=0x80 && first<=0x9f) codepoint=cp1252[first-0x80];
        if (!XMLCharacter(codepoint)) codepoint=0xfffd;
        AppendUTF8(output,codepoint);
        i+=length;
    }
    return output;
}
std::unique_ptr<UIObject> LayoutParser::LoadLayout(const std::string& filepath) {
    std::ifstream file(filepath.c_str(),std::ios::binary);
    if (!file) { error="Cannot read layout: "+filepath; return nullptr; }
    const std::string bytes((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
    if (file.bad()) { error="Cannot read layout: "+filepath; return nullptr; }
    return ParseLayout(bytes);
}
std::unique_ptr<UIObject> LayoutParser::ParseLayout(const std::string& xml) {
    error.clear();
    const std::string sanitized=SanitizeXML(xml);
    tinyxml2::XMLDocument document;
    if (document.Parse(sanitized.data(),sanitized.size())!=tinyxml2::XML_SUCCESS) {
        error=document.ErrorStr() ? document.ErrorStr() : "Invalid XML layout";
        return nullptr;
    }
    tinyxml2::XMLElement* root=document.FirstChildElement("layout");
    if (!root || root!=document.FirstChildElement() || root->NextSiblingElement()) {
        error="Expected one <layout> root"; return nullptr;
    }
    layoutWidth=GetIntAttr(root,"w",1920); layoutHeight=GetIntAttr(root,"h",1080);
    std::unique_ptr<UIContainer> layout(new UIContainer());
    ParseCommonAttributes(root,layout.get());
    if (layout->name.empty()) layout->name="layout";
    layout->w=static_cast<float>(layoutWidth); layout->h=static_cast<float>(layoutHeight);
    for (tinyxml2::XMLElement* child=root->FirstChildElement();child;child=child->NextSiblingElement()) {
        std::unique_ptr<UIObject> object=ParseElement(child);
        if (!object) return nullptr;
        if (dynamic_cast<UIGradStop*>(object.get()) || dynamic_cast<UIVertex*>(object.get())) {
            error="Data tag outside its Gradient or Poly parent"; return nullptr;
        }
        layout->AddChild(std::move(object));
    }
    layout->Update(0);
    return std::unique_ptr<UIObject>(layout.release());
}
std::unique_ptr<UIObject> LayoutParser::ParseElement(tinyxml2::XMLElement* element) {
    const char* tag=element->Name();
    std::unique_ptr<UIObject> object;
    if (!std::strcmp(tag,"Container")) object.reset(new UIContainer());
    else if (!std::strcmp(tag,"Gradient")) object.reset(new UIGradient());
    else if (!std::strcmp(tag,"GradStop")) object.reset(new UIGradStop());
    else if (!std::strcmp(tag,"Rectangle")) object.reset(new UIRectangle());
    else if (!std::strcmp(tag,"Line")) object.reset(new UILine());
    else if (!std::strcmp(tag,"Image")) object.reset(new UIImage());
    else if (!std::strcmp(tag,"TextPoint")) object.reset(new UITextPoint());
    else if (!std::strcmp(tag,"TextParagraph")) object.reset(new UITextParagraph());
    else if (!std::strcmp(tag,"Poly")) object.reset(new UIPoly());
    else if (!std::strcmp(tag,"Vertex")) object.reset(new UIVertex());
    else if (!std::strcmp(tag,"Circle")) object.reset(new UICircle());
    else if (!std::strcmp(tag,"Arc")) object.reset(new UIArc());
    else if (!std::strcmp(tag,"ClippingMask")) object.reset(new UIClippingMask());
    else if (!std::strcmp(tag,"Interactive")) object.reset(new UIInteractive());
    else if (!std::strcmp(tag,"Draggable")) object.reset(new UIDraggable());
    else if (!std::strcmp(tag,"ButtonStandard")) object.reset(new UIButtonStandard());
    else if (!std::strcmp(tag,"ButtonSelect")) object.reset(new UIButtonSelect());
    else if (!std::strcmp(tag,"ButtonTextField")) object.reset(new UIButtonTextField());
    else if (!std::strcmp(tag,"ButtonTextFieldBox")) object.reset(new UIButtonTextFieldBox());
    else if (!std::strcmp(tag,"ScrollableList")) object.reset(new UIScrollableList());
    else { error="Unknown GOLD layout tag: "; error+=tag; return nullptr; }
    ParseCommonAttributes(element,object.get());
    if (UIGradient* gradient=dynamic_cast<UIGradient*>(object.get())) gradient->subtype=GetStringAttr(element,"subtype","gradVertical");
    if (UIGradStop* stop=dynamic_cast<UIGradStop*>(object.get())) {
        stop->location=GetFloatAttr(element,"location",0);
        stop->fillColor=GetStringAttr(element,"fillColor","FFFFFF");
        stop->fillAlpha=GetFloatAttr(element,"fillA",1);
    }
    if (UIRectangle* rectangle=dynamic_cast<UIRectangle*>(object.get())) {
        rectangle->subtype=GetStringAttr(element,"subtype","filled");
        rectangle->fillColor=GetStringAttr(element,"fillColor","FFFFFF");
        rectangle->fillAlpha=GetFloatAttr(element,"fillA",1);
        rectangle->strokeColor=GetStringAttr(element,"strokeColor","FFFFFF");
        rectangle->strokeWidth=GetFloatAttr(element,"strokeW",1);
    }
    if (UILine* line=dynamic_cast<UILine*>(object.get())) {
        line->x1=GetFloatAttr(element,"x1",0); line->y1=GetFloatAttr(element,"y1",0);
        line->x2=GetFloatAttr(element,"x2",line->w); line->y2=GetFloatAttr(element,"y2",line->h);
        line->strokeColor=GetStringAttr(element,"strokeColor","FFFFFF");
        line->strokeWidth=GetFloatAttr(element,"strokeW",1);
    }
    if (UIArc* arc=dynamic_cast<UIArc*>(object.get())) {
        arc->startAngle=GetFloatAttr(element,"startAngle",-90);
        arc->endAngle=GetFloatAttr(element,"endAngle",270);
    }
    if (UITextPoint* text=dynamic_cast<UITextPoint*>(object.get())) {
        text->text=GetStringAttr(element,"text",element->GetText() ? element->GetText() : "");
        text->font=GetStringAttr(element,"font","AeroMaticsRegular");
        text->fontSize=GetIntAttr(element,"size",24);
        text->leading=GetFloatAttr(element,"leading",0);
        text->fillColor=GetStringAttr(element,"fillColor","FFFFFF");
        text->assetBasePath=FontDirectory(assetBasePath);
        const std::string alignment=GetStringAttr(element,"align","left");
        text->align=alignment=="center" ? TextAlign::Center : alignment=="right" ? TextAlign::Right : TextAlign::Left;
        if (UITextParagraph* paragraph=dynamic_cast<UITextParagraph*>(text)) paragraph->wordWrap=GetBoolAttr(element,"wordWrap",true);
    }
    if (UIImage* image=dynamic_cast<UIImage*>(object.get())) {
        image->file=GetStringAttr(element,"file");
        if (!image->file.empty()) image->LoadBitmap(assetBasePath);
    }
    if (UIDraggable* draggable=dynamic_cast<UIDraggable*>(object.get())) {
        draggable->dragHorizontal=GetBoolAttr(element,"dragHorizontal",true);
        draggable->dragVertical=GetBoolAttr(element,"dragVertical",true);
        draggable->minX=GetFloatAttr(element,"minX",0); draggable->minY=GetFloatAttr(element,"minY",0);
        draggable->maxX=GetFloatAttr(element,"maxX",0); draggable->maxY=GetFloatAttr(element,"maxY",0);
        draggable->bounded=GetBoolAttr(element,"bounded",element->Attribute("maxX") || element->Attribute("maxY"));
    }
    if (UIButtonSelect* select=dynamic_cast<UIButtonSelect*>(object.get())) select->selected=GetBoolAttr(element,"selected",true);
    if (UIButtonTextField* field=dynamic_cast<UIButtonTextField*>(object.get())) {
        field->isPassword=GetBoolAttr(element,"isPassword",GetBoolAttr(element,"password",false));
        field->maxLength=GetIntAttr(element,"maxLength",256);
        field->inputText=GetStringAttr(element,"value");
        field->caret=field->inputText.size();
    }
    if (UIScrollableList* list=dynamic_cast<UIScrollableList*>(object.get())) {
        list->scrollHorizontal=GetBoolAttr(element,"scrollHorizontal",list->arrangeType==ArrangeType::ArrangeHorizontal || list->arrangeType==ArrangeType::ArrangeTableHorizontal);
        list->scrollVertical=GetBoolAttr(element,"scrollVertical",!list->scrollHorizontal);
        list->dragScroll=GetBoolAttr(element,"dragScroll",true);
        list->wheelStep=GetFloatAttr(element,"wheelStep",30);
        list->scrollX=GetFloatAttr(element,"scrollX",0); list->scrollY=GetFloatAttr(element,"scrollY",0);
    }
    const bool data=dynamic_cast<UIGradStop*>(object.get()) || dynamic_cast<UIVertex*>(object.get());
    if (data && element->FirstChildElement()) { error="GOLD data tag cannot have children"; return nullptr; }
    for (tinyxml2::XMLElement* child=element->FirstChildElement();child;child=child->NextSiblingElement()) {
        std::unique_ptr<UIObject> parsed=ParseElement(child);
        if (!parsed) return nullptr;
        if (UIGradStop* stop=dynamic_cast<UIGradStop*>(parsed.get())) {
            UIGradient* gradient=dynamic_cast<UIGradient*>(object.get());
            if (!gradient) { error="GradStop requires a Gradient parent"; return nullptr; }
            UIGradient::GradStop value;
            value.location=stop->location; value.fillColor=stop->fillColor; value.fillAlpha=stop->fillAlpha;
            gradient->stops.push_back(value);
        }
        if (UIVertex* vertex=dynamic_cast<UIVertex*>(parsed.get())) {
            UIPoly* polygon=dynamic_cast<UIPoly*>(object.get());
            if (!polygon) { error="Vertex requires a Poly parent"; return nullptr; }
            polygon->vertices.push_back(UIPoly::Vertex{vertex->x,vertex->y});
        }
        object->AddChild(std::move(parsed));
    }
    if (UIGradient* gradient=dynamic_cast<UIGradient*>(object.get())) {
        std::stable_sort(gradient->stops.begin(),gradient->stops.end(),
            [](const UIGradient::GradStop& a,const UIGradient::GradStop& b) { return a.location<b.location; });
    }
    if (UIPoly* polygon=dynamic_cast<UIPoly*>(object.get())) polygon->PrepareVertices();
    if (UIButtonTextField* field=dynamic_cast<UIButtonTextField*>(object.get())) {
        if (!field->inputText.empty()) field->SetText(field->inputText);
    }
    return object;
}
void LayoutParser::ParseCommonAttributes(tinyxml2::XMLElement* element,UIObject* object) {
    object->name=GetStringAttr(element,"name");
    object->x=GetFloatAttr(element,"x",0); object->y=GetFloatAttr(element,"y",0);
    object->w=GetFloatAttr(element,"w",0); object->h=GetFloatAttr(element,"h",0);
    object->alpha=GetFloatAttr(element,"alpha",1);
    object->visible=GetBoolAttr(element,"visible",true); object->enabled=GetBoolAttr(element,"enabled",true);
    object->scaleX=GetFloatAttr(element,"scaleX",1); object->scaleY=GetFloatAttr(element,"scaleY",1);
    object->zOrder=GetIntAttr(element,"z",GetIntAttr(element,"zOrder",0));
    object->focusable=GetBoolAttr(element,"focusable",object->focusable);
    object->clipChildren=GetBoolAttr(element,"clipChildren",object->clipChildren);
    const std::string scale=GetStringAttr(element,"scalingType","scaleNormal");
    object->scalingType=scale=="scaleByHeight" ? ScalingType::ScaleByHeight : scale=="scaleByWidth" ? ScalingType::ScaleByWidth :
        scale=="scaleToWidth" ? ScalingType::ScaleToWidth : scale=="scaleToHeight" ? ScalingType::ScaleToHeight :
        scale=="scaleFree" ? ScalingType::ScaleFree : ScalingType::ScaleNormal;
    const std::string arrange=GetStringAttr(element,"arrangeType","arrangeFree");
    object->arrangeType=arrange=="arrangeHorizontal" ? ArrangeType::ArrangeHorizontal : arrange=="arrangeVertical" ? ArrangeType::ArrangeVertical :
        arrange=="arrangeTableHorizontal" ? ArrangeType::ArrangeTableHorizontal : arrange=="arrangeTableVertical" ? ArrangeType::ArrangeTableVertical : ArrangeType::ArrangeFree;
    object->arrangePadding=GetFloatAttr(element,"arrangePadding",2);
}
float LayoutParser::GetFloatAttr(tinyxml2::XMLElement* element,const char* name,float def) {
    float value=def;
    if (element->QueryFloatAttribute(name,&value)!=tinyxml2::XML_SUCCESS || !std::isfinite(value)) return def;
    return value;
}
int LayoutParser::GetIntAttr(tinyxml2::XMLElement* element,const char* name,int def) {
    int value=def;
    return element->QueryIntAttribute(name,&value)==tinyxml2::XML_SUCCESS ? value : def;
}
bool LayoutParser::GetBoolAttr(tinyxml2::XMLElement* element,const char* name,bool def) {
    bool value=def;
    return element->QueryBoolAttribute(name,&value)==tinyxml2::XML_SUCCESS ? value : def;
}
std::string LayoutParser::GetStringAttr(tinyxml2::XMLElement* element,const char* name,const char* def) {
    const char* value=element->Attribute(name); return value ? value : def;
}
LayoutManager& LayoutManager::GetInstance() { static LayoutManager manager; return manager; }
LayoutManager::LayoutManager() {}
LayoutManager::~LayoutManager() { ClearCache(); }
void LayoutManager::SetLayoutDirectory(const std::string& directory) { if (layoutDir!=directory) { ClearCache(); layoutDir=directory; } }
void LayoutManager::SetAssetDirectory(const std::string& directory) { if (assetDir!=directory) { ClearCache(); assetDir=directory; } }
UIObject* LayoutManager::GetLayout(const std::string& name) {
    const auto existing=cache.find(name);
    if (existing!=cache.end()) return existing->second.get();
    std::string path=layoutDir.empty() ? name : layoutDir+"/"+name;
    if (path.size()<4 || path.compare(path.size()-4,4,".xml")) path+=".xml";
    LayoutParser parser; parser.SetAssetBasePath(assetDir);
    std::unique_ptr<UIObject> layout=parser.LoadLayout(path);
    if (!layout) { std::fprintf(stderr,"HD_UI: %s\n",parser.GetError().c_str()); return nullptr; }
    UIObject* result=layout.get(); cache.emplace(name,std::move(layout)); return result;
}
std::unique_ptr<UIObject> LayoutManager::TakeLayout(const std::string& name) {
    if (!GetLayout(name)) return nullptr;
    const auto found=cache.find(name);
    std::unique_ptr<UIObject> layout=std::move(found->second);
    cache.erase(found);
    if (Allegro5System::GetInputRoot()==layout.get()) Allegro5System::SetInputRoot(nullptr);
    else layout->ResetInput();
    return layout;
}
void LayoutManager::ClearCache() { cache.clear(); }
} // namespace HDUI
