#ifndef PREDICTEDSPEEDSTREETMAP_H
#define PREDICTEDSPEEDSTREETMAP_H

// Decorates a CStreetMap with ML-predicted speed limits.
//
// Ways that have no "maxspeed" tag but do have an entry in the prediction
// table expose an extra attribute, "maxspeed:predicted" (e.g. "30 mph").
// Real OSM maxspeed tags always win; the planner only reads the predicted
// attribute as a fallback before the configured default speed limit.
//
// Predictions come from ml/train.py -> data/speed_predictions.csv with the
// header: way_id,predicted_mph,confidence

#include "DSVReader.h"
#include "StreetMap.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class CPredictedSpeedStreetMap : public CStreetMap{
    public:
        inline static const std::string PredictedSpeedKey = "maxspeed:predicted";
        using TPredictions = std::unordered_map<TWayID, double>;

    private:
        struct SPredictedWay : public SWay{
            std::shared_ptr<SWay> DWay;
            std::string DSpeed;
            bool DActive;

            SPredictedWay(std::shared_ptr<SWay> way, double mph)
                : DWay(std::move(way)), DActive(DWay && !DWay->HasAttribute("maxspeed")){
                DSpeed = std::to_string(static_cast<int>(mph + 0.5)) + " mph";
            }
            TWayID ID() const noexcept override{ return DWay->ID(); }
            std::size_t NodeCount() const noexcept override{ return DWay->NodeCount(); }
            TNodeID GetNodeID(std::size_t index) const noexcept override{ return DWay->GetNodeID(index); }
            std::size_t AttributeCount() const noexcept override{
                return DWay->AttributeCount() + (DActive ? 1 : 0);
            }
            std::string GetAttributeKey(std::size_t index) const noexcept override{
                if(DActive && index == DWay->AttributeCount()){
                    return PredictedSpeedKey;
                }
                return DWay->GetAttributeKey(index);
            }
            bool HasAttribute(const std::string &key) const noexcept override{
                return (DActive && key == PredictedSpeedKey) || DWay->HasAttribute(key);
            }
            std::string GetAttribute(const std::string &key) const noexcept override{
                if(DActive && key == PredictedSpeedKey){
                    return DSpeed;
                }
                return DWay->GetAttribute(key);
            }
        };

        std::shared_ptr<CStreetMap> DMap;
        TPredictions DPredictions;

        std::shared_ptr<SWay> Wrap(std::shared_ptr<SWay> way) const noexcept{
            if(!way){
                return way;
            }
            auto it = DPredictions.find(way->ID());
            if(it == DPredictions.end() || way->HasAttribute("maxspeed")){
                return way;
            }
            return std::make_shared<SPredictedWay>(way, it->second);
        }

    public:
        CPredictedSpeedStreetMap(std::shared_ptr<CStreetMap> map, TPredictions predictions)
            : DMap(std::move(map)), DPredictions(std::move(predictions)){}

        // Reads way_id,predicted_mph[,confidence] rows; skips the header and bad rows.
        static TPredictions LoadPredictions(std::shared_ptr<CDSVReader> reader){
            TPredictions out;
            if(!reader){
                return out;
            }
            std::vector<std::string> row;
            while(!reader->End()){
                if(!reader->ReadRow(row) || row.size() < 2){
                    continue;
                }
                try{
                    std::size_t used = 0;
                    TWayID id = std::stoull(row[0], &used);
                    if(used != row[0].size()){
                        continue;
                    }
                    double mph = std::stod(row[1]);
                    if(mph > 0.0){
                        out[id] = mph;
                    }
                }
                catch(...){
                    continue; // header or malformed row
                }
            }
            return out;
        }

        std::size_t PredictionCount() const noexcept{ return DPredictions.size(); }

        std::size_t NodeCount() const noexcept override{ return DMap->NodeCount(); }
        std::size_t WayCount() const noexcept override{ return DMap->WayCount(); }
        std::shared_ptr<SNode> NodeByIndex(std::size_t index) const noexcept override{ return DMap->NodeByIndex(index); }
        std::shared_ptr<SNode> NodeByID(TNodeID id) const noexcept override{ return DMap->NodeByID(id); }
        std::shared_ptr<SWay> WayByIndex(std::size_t index) const noexcept override{ return Wrap(DMap->WayByIndex(index)); }
        std::shared_ptr<SWay> WayByID(TWayID id) const noexcept override{ return Wrap(DMap->WayByID(id)); }
};

#endif
