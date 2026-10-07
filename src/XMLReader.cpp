#include "XMLReader.h"
#include <string>
#include <vector>
#include <expat.h>

struct CXMLReader::SImplementation{

    std::shared_ptr<CDataSource> source;
    std::string text;
    std::vector<SXMLEntity> entities;
    int index;
    XML_Parser DParser = nullptr;

    SImplementation(std::shared_ptr<CDataSource> src){

        source = src;
        index = 0;

        if(source){
            char ch;
            while(source->Get(ch)){
                text.push_back(ch);
            }
        }

        std::string cleaned = text;

        if(cleaned.rfind("<?xml",0) == 0){
            size_t endPos = cleaned.find("?>");

            if(endPos != std::string::npos){
                cleaned = cleaned.substr(endPos + 2);
                size_t firstChar = cleaned.find_first_not_of(" \t\n\r");

                if(firstChar != std::string::npos){
                    cleaned = cleaned.substr(firstChar);
                }
            }
        }

        text = cleaned;

        std::string wrapped = "<root>" + cleaned + "</root>";

        XML_Parser parser = XML_ParserCreate(NULL);
        DParser = parser;
        XML_SetUserData(parser,this);
        XML_SetElementHandler(parser,startHandler,endHandler);
        XML_SetCharacterDataHandler(parser,charHandler);
        XML_Parse(parser,wrapped.c_str(),(int)wrapped.size(),1);
        XML_ParserFree(parser);
        DParser = nullptr;
    }

    static void startHandler(void *data,const XML_Char *name,const XML_Char **atts){

        SImplementation *self = (SImplementation*)data;

        SXMLEntity entity;
        entity.DType = SXMLEntity::EType::StartElement;
        entity.DNameData = std::string(name);

        int i=0;
        while(atts && atts[i]){
            std::string key = atts[i];
            std::string value = "";
            if(atts[i+1]) value = atts[i+1];
            entity.DAttributes.push_back(std::make_pair(key,value));
            i+=2;
        }

        self->entities.push_back(entity);
    }

    static void endHandler(void *data,const XML_Char *name){
        
        SImplementation *self = (SImplementation*)data;

        // Expat reports an empty-element tag (<tag/>) as a start immediately
        // followed by an end that consumes zero bytes. Fold that pair into a
        // single CompleteElement instead of rescanning the source text.
        if(self->DParser && XML_GetCurrentByteCount(self->DParser) == 0 &&
           !self->entities.empty() &&
           self->entities.back().DType == SXMLEntity::EType::StartElement &&
           self->entities.back().DNameData == name){
            self->entities.back().DType = SXMLEntity::EType::CompleteElement;
            return;
        }

        SXMLEntity entity;
        entity.DType = SXMLEntity::EType::EndElement;
        entity.DNameData = std::string(name);

        self->entities.push_back(entity);
    }

    static void charHandler(void *data,const XML_Char *s,int len){

        SImplementation *self = (SImplementation*)data;

        SXMLEntity entity;
        entity.DType = SXMLEntity::EType::CharData;
        entity.DNameData = std::string(s,s+len);

        self->entities.push_back(entity);
    }

    bool End() const{

        return index >= (int)entities.size();
    }

    bool ReadEntity(SXMLEntity &entity,bool skipcdata){

        while(index < (int)entities.size()){

            SXMLEntity current = entities[index];
            index++;

            if(current.DNameData == "root"){
                if(current.DType == SXMLEntity::EType::StartElement) continue;
                if(current.DType == SXMLEntity::EType::EndElement) continue;
            }

            if(skipcdata && current.DType == SXMLEntity::EType::CharData){
                continue;
            }

            entity = current;
            return true;
        }

        return false;
    }
};

CXMLReader::CXMLReader(std::shared_ptr<CDataSource> src){

    DImplementation = std::make_unique<SImplementation>(src);
}

CXMLReader::~CXMLReader(){
}

bool CXMLReader::End() const{

    return DImplementation->End();
}

bool CXMLReader::ReadEntity(SXMLEntity &entity,bool skipcdata){
    
    return DImplementation->ReadEntity(entity,skipcdata);
}