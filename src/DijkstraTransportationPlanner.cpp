#include "DijkstraTransportationPlanner.h"

#include "BusSystemIndexer.h"
#include "GeographicUtils.h"
#include "StringUtils.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <functional>
#include <iomanip>
#include <limits>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct CDijkstraTransportationPlanner::SImplementation{
    using TNodeID = CTransportationPlanner::TNodeID;
    using TStopID = CBusSystem::TStopID;
    using TMode = CTransportationPlanner::ETransportationMode;

    struct SDirectedEdge{
        TNodeID DDestination = CStreetMap::InvalidNodeID;
        double DDistanceMiles = 0.0;
        double DTimeHours = 0.0;
        TMode DMode = TMode::Walk;
    };

    struct SWayInfo{
        double DDistanceMiles = 0.0;
        double DSpeed = 0.0;
        std::string DName;
    };

    struct SPairHash{
        std::size_t operator()(const std::pair<TNodeID,TNodeID> &v) const noexcept{
            return static_cast<std::size_t>(v.first ^ (v.second << 1));
        }
    };

    std::shared_ptr<SConfiguration> DConfig;
    std::shared_ptr<CStreetMap> DStreetMap;
    std::shared_ptr<CBusSystem> DBusSystem;
    std::vector<std::shared_ptr<CStreetMap::SNode>> DSortedNodes;
    std::vector<TNodeID> DNodeIDs;
    std::unordered_map<TNodeID,std::shared_ptr<CStreetMap::SNode>> DNodeByID;
    std::unordered_map<TNodeID,std::size_t> DNodeIndex;
    std::unordered_map<TNodeID,std::vector<SDirectedEdge>> DShortEdges;
    std::unordered_map<TNodeID,std::vector<SDirectedEdge>> DWalkEdges;
    std::unordered_map<TNodeID,std::vector<SDirectedEdge>> DBikeEdges;
    std::unordered_map<TNodeID,std::vector<SDirectedEdge>> DBusEdges;
    std::unordered_map<TNodeID,std::vector<SDirectedEdge>> DWalkBusEdges;
    std::unordered_map<TNodeID,std::vector<SDirectedEdge>> DWalkBikeEdges;
    std::unordered_map<std::pair<TNodeID,TNodeID>,SWayInfo,SPairHash> DWayInfo;
    std::unordered_map<TNodeID,TStopID> DStopByNode;
    std::unordered_map<std::pair<TNodeID,TNodeID>,std::unordered_set<std::string>,SPairHash> DRouteNames;

    static bool IsOneWay(const std::string &v){
        auto s = StringUtils::Lower(StringUtils::Strip(v));
        return s == "yes" || s == "true" || s == "1";
    }

    static bool BikeAllowed(const std::string &v){
        auto s = StringUtils::Lower(StringUtils::Strip(v));
        return s != "no";
    }

    static double ParseSpeed(const std::string &v, double d){
        std::string s = StringUtils::Lower(StringUtils::Strip(v));
        std::string n;
        for(char c : s){
            if((c >= '0' && c <= '9') || c == '.'){
                n.push_back(c);
            }
            else if(!n.empty()){
                break;
            }
        }
        if(n.empty()){
            return d;
        }
        return std::stod(n);
    }

    void AddEdge(std::unordered_map<TNodeID,std::vector<SDirectedEdge>> &m, TNodeID a, TNodeID b, double dist, double time, TMode mode){
        // just putting an edge in the graph map
        SDirectedEdge e;
        e.DDestination = b;
        e.DDistanceMiles = dist;
        e.DTimeHours = time;
        e.DMode = mode;
        m[a].push_back(e);
    }

    void BuildRoadEdges(){
        if(!DStreetMap){
            return;
        }
        // grabbing speeds from config and then building road edges
        double ws = DConfig ? DConfig->WalkSpeed() : 3.0;
        double bs = DConfig ? DConfig->BikeSpeed() : 8.0;
        double ds = DConfig ? DConfig->DefaultSpeedLimit() : 25.0;
        if(ws <= 0.0){
            ws = 3.0;
        }
        if(bs <= 0.0){
            bs = 8.0;
        }
        if(ds <= 0.0){
            ds = 25.0;
        }
        for(std::size_t i = 0; i < DStreetMap->WayCount(); i++){
            auto way = DStreetMap->WayByIndex(i);
            if(!way){
                continue;
            }
            bool oneway = way->HasAttribute("oneway") && IsOneWay(way->GetAttribute("oneway"));
            bool bikeok = !way->HasAttribute("bicycle") || BikeAllowed(way->GetAttribute("bicycle"));
            std::string name;
            if(way->HasAttribute("name")){
                name = way->GetAttribute("name");
            }
            double speed = ds;
            if(way->HasAttribute("maxspeed")){
                speed = ParseSpeed(way->GetAttribute("maxspeed"), ds);
            }
            else if(way->HasAttribute("maxspeed:predicted")){
                // ML fallback (ml/train.py) for roads with no posted speed tag
                speed = ParseSpeed(way->GetAttribute("maxspeed:predicted"), ds);
            }
            if(speed <= 0.0){
                speed = ds;
            }

            for(std::size_t j = 1; j < way->NodeCount(); j++){
                TNodeID a = way->GetNodeID(j - 1);
                TNodeID b = way->GetNodeID(j);
                auto na = DStreetMap->NodeByID(a);
                auto nb = DStreetMap->NodeByID(b);
                if(!na || !nb){
                    continue;
                }
                double dist = SGeographicUtils::HaversineDistanceInMiles(na->Location(), nb->Location());
                if(dist < 0.0){
                    continue;
                }
                SWayInfo w;
                w.DDistanceMiles = dist;
                w.DSpeed = speed;
                w.DName = name;
                DWayInfo[std::make_pair(a,b)] = w;
                DWayInfo[std::make_pair(b,a)] = w;
                AddEdge(DWalkEdges, a, b, dist, dist / ws, TMode::Walk);
                AddEdge(DWalkEdges, b, a, dist, dist / ws, TMode::Walk);
                if(oneway){
                    AddEdge(DShortEdges, a, b, dist, dist, TMode::Walk);
                }
                else{
                    AddEdge(DShortEdges, a, b, dist, dist, TMode::Walk);
                    AddEdge(DShortEdges, b, a, dist, dist, TMode::Walk);
                }
                if(bikeok){
                    AddEdge(DBikeEdges, a, b, dist, dist / bs, TMode::Bike);
                    if(!oneway){
                        AddEdge(DBikeEdges, b, a, dist, dist / bs, TMode::Bike);
                    }
                }
            }
        }
    }

    void BuildBusEdges(){
        if(!DBusSystem){
            return;
        }
        // bus edges are stop to stop using shortest road path
        double stopt = DConfig ? DConfig->BusStopTime() : 30.0;
        if(stopt < 0.0){
            stopt = 0.0;
        }
        double stoph = stopt / 3600.0;

        for(std::size_t i = 0; i < DBusSystem->RouteCount(); i++){
            auto route = DBusSystem->RouteByIndex(i);
            if(!route){
                continue;
            }
            if(route->StopCount() < 2){
                continue;
            }
            for(std::size_t j = 1; j < route->StopCount(); j++){
                auto sa = DBusSystem->StopByID(route->GetStopID(j - 1));
                auto sb = DBusSystem->StopByID(route->GetStopID(j));
                if(!sa || !sb){
                    continue;
                }
                TNodeID a = sa->NodeID();
                TNodeID b = sb->NodeID();
                DRouteNames[std::make_pair(a,b)].insert(route->Name());

                std::vector<TNodeID> busnodes;
                std::vector<TMode> busmodes;
                double dist = CPathRouter::NoPathExists;
                if(!FindPath(DShortEdges, a, b, busnodes, busmodes, dist)){
                    continue;
                }
                double time = 0.0;
                for(std::size_t k = 1; k < busnodes.size(); k++){
                    auto it = DWayInfo.find(std::make_pair(busnodes[k - 1],busnodes[k]));
                    if(it == DWayInfo.end()){
                        time = CPathRouter::NoPathExists;
                        break;
                    }
                    if(it->second.DSpeed <= 0.0){
                        time = CPathRouter::NoPathExists;
                        break;
                    }
                    time += it->second.DDistanceMiles / it->second.DSpeed;
                }
                if(time == CPathRouter::NoPathExists){
                    continue;
                }
                time += stoph;
                AddEdge(DBusEdges, a, b, dist, time, TMode::Bus);
            }
        }
    }

    bool FindPath(const std::unordered_map<TNodeID,std::vector<SDirectedEdge>> &m, TNodeID src, TNodeID dst, std::vector<TNodeID> &outnodes, std::vector<TMode> &outmodes, double &outcost) const{
        // basic dijkstra on whatever graph map gets passed in
        outnodes.clear();
        outmodes.clear();
        outcost = CPathRouter::NoPathExists;
        auto srcit = DNodeIndex.find(src);
        auto dstit = DNodeIndex.find(dst);
        if(src == dst){
            if(srcit == DNodeIndex.end()){
                return false;
            }
            outnodes.push_back(src);
            outcost = 0.0;
            return true;
        }
        if(srcit == DNodeIndex.end() || dstit == DNodeIndex.end()){
            return false;
        }

        struct SState{
            double d;
            std::size_t n;
            bool operator>(const SState &o) const{
                if(d != o.d){
                    return d > o.d;
                }
                return n > o.n;
            }
        };

        std::vector<double> dist(DNodeIDs.size(), CPathRouter::NoPathExists);
        std::vector<std::size_t> prev(DNodeIDs.size(), std::numeric_limits<std::size_t>::max());
        std::vector<TMode> pmode(DNodeIDs.size(), TMode::Walk);
        auto srcidx = srcit->second;
        auto dstidx = dstit->second;
        dist[srcidx] = 0.0;

        std::priority_queue<SState,std::vector<SState>,std::greater<SState>> pq;
        pq.push({0.0,srcidx});

        while(!pq.empty()){
            auto cur = pq.top();
            pq.pop();
            if(cur.d != dist[cur.n]){
                continue;
            }
            if(cur.n == dstidx){
                break;
            }
            auto it = m.find(DNodeIDs[cur.n]);
            if(it == m.end()){
                continue;
            }
            for(const auto &e : it->second){
                auto nextit = DNodeIndex.find(e.DDestination);
                if(nextit == DNodeIndex.end()){
                    continue;
                }
                auto nextidx = nextit->second;
                double nd = cur.d + e.DTimeHours;
                if((nd < dist[nextidx]) || ((std::fabs(nd - dist[nextidx]) < 1e-9) && (cur.n < prev[nextidx]))){
                    dist[nextidx] = nd;
                    prev[nextidx] = cur.n;
                    pmode[nextidx] = e.DMode;
                    pq.push({nd,nextidx});
                }
            }
        }

        if(dist[dstidx] == CPathRouter::NoPathExists){
            return false;
        }

        std::vector<std::size_t> revnodes;
        std::vector<TMode> revmodes;
        std::size_t cur = dstidx;
        revnodes.push_back(cur);
        while(cur != srcidx){
            auto pit = prev[cur];
            if(pit == std::numeric_limits<std::size_t>::max()){
                return false;
            }
            revmodes.push_back(pmode[cur]);
            cur = pit;
            revnodes.push_back(cur);
        }
        std::reverse(revnodes.begin(), revnodes.end());
        std::reverse(revmodes.begin(), revmodes.end());
        outnodes.reserve(revnodes.size());
        for(auto idx : revnodes){
            outnodes.push_back(DNodeIDs[idx]);
        }
        outmodes = std::move(revmodes);
        outcost = dist[dstidx];
        return true;
    }

    std::vector<CTransportationPlanner::TTripStep> BuildTrip(const std::vector<TNodeID> &nodes, const std::vector<TMode> &modes) const{
        // turning node list into trip steps for planner output
        std::vector<CTransportationPlanner::TTripStep> out;
        if(nodes.empty()){
            return out;
        }
        if(nodes.size() == 1){
            out.push_back(std::make_pair(TMode::Walk, nodes[0]));
            return out;
        }
        TMode first = modes.empty() ? TMode::Walk : modes[0];
        if(first == TMode::Bus){
            out.push_back(std::make_pair(TMode::Walk, nodes[0]));
        }
        else{
            out.push_back(std::make_pair(first, nodes[0]));
        }
        for(std::size_t i = 1; i < nodes.size(); i++){
            TMode m = TMode::Walk;
            if(i - 1 < modes.size()){
                m = modes[i - 1];
            }
            out.push_back(std::make_pair(m, nodes[i]));
        }
        return out;
    }

    std::unordered_map<TNodeID,std::vector<SDirectedEdge>> Merge(const std::unordered_map<TNodeID,std::vector<SDirectedEdge>> &a, const std::unordered_map<TNodeID,std::vector<SDirectedEdge>> &b) const{
        auto out = a;
        for(const auto &p : b){
            auto &v = out[p.first];
            for(const auto &e : p.second){
                v.push_back(e);
            }
        }
        return out;
    }

    static std::string ModeName(TMode m){
        if(m == TMode::Walk){
            return "Walk";
        }
        if(m == TMode::Bike){
            return "Bike";
        }
        return "Bus";
    }

    double Distance(TNodeID a, TNodeID b) const{
        auto it = DWayInfo.find(std::make_pair(a,b));
        if(it != DWayInfo.end()){
            return it->second.DDistanceMiles;
        }
        auto na = DStreetMap ? DStreetMap->NodeByID(a) : nullptr;
        auto nb = DStreetMap ? DStreetMap->NodeByID(b) : nullptr;
        if(!na || !nb){
            return 0.0;
        }
        return SGeographicUtils::HaversineDistanceInMiles(na->Location(), nb->Location());
    }

    std::string WayName(TNodeID a, TNodeID b) const{
        auto it = DWayInfo.find(std::make_pair(a,b));
        if(it == DWayInfo.end()){
            return "";
        }
        return it->second.DName;
    }

    std::string BusRoute(const std::vector<CTransportationPlanner::TTripStep> &path, std::size_t i, std::size_t &j) const{
        j = i;
        if(i + 1 >= path.size()){
            return "";
        }
        auto a = path[i].second;
        auto b = path[i + 1].second;
        auto it = DRouteNames.find(std::make_pair(a,b));
        if(it == DRouteNames.end() || it->second.empty()){
            return "";
        }
        std::string best = "";
        std::size_t bestj = i + 1;
        for(const auto &name : it->second){
            std::size_t cur = i + 1;
            while(cur < path.size() - 1){
                auto x = path[cur].second;
                auto y = path[cur + 1].second;
                auto rit = DRouteNames.find(std::make_pair(x,y));
                if(rit == DRouteNames.end() || rit->second.find(name) == rit->second.end()){
                    break;
                }
                cur++;
            }
            if(best.empty() || cur > bestj || (cur == bestj && name < best)){
                best = name;
                bestj = cur;
            }
        }
        j = bestj;
        return best;
    }

    static std::string DistanceString(double dist){
        std::ostringstream out;
        if(dist < 0.1){
            out << static_cast<long long>(std::llround(dist * 5280.0)) << " ft";
            return out.str();
        }
        if(dist < 1.0){
            out << std::fixed << std::setprecision(2) << dist;
        }
        else{
            out << std::fixed << std::setprecision(1) << dist;
        }
        auto s = out.str();
        while(s.size() > 2 && s.back() == '0'){
            s.pop_back();
        }
        if(!s.empty() && s.back() == '.'){
            s.pop_back();
        }
        return s + " mi";
    }

    bool BuildDescription(const std::vector<CTransportationPlanner::TTripStep> &path, std::vector<std::string> &desc) const{
        // this turns the saved path into the printed directions
        desc.clear();
        if(path.empty()){
            return false;
        }
        auto first = DStreetMap ? DStreetMap->NodeByID(path.front().second) : nullptr;
        auto last = DStreetMap ? DStreetMap->NodeByID(path.back().second) : nullptr;
        if(!first || !last){
            return false;
        }
        desc.push_back("Start at " + SGeographicUtils::ConvertLLToDMS(first->Location()));

        std::size_t i = 0;
        while(i + 1 < path.size()){
            auto mode = path[i + 1].first;
            if(mode == TMode::Bus){
                std::size_t j = i + 1;
                std::string r = BusRoute(path, i, j);
                if(r.empty()){
                    return false;
                }
                auto s1 = DStopByNode.find(path[i].second);
                auto s2 = DStopByNode.find(path[j].second);
                if(s1 == DStopByNode.end() || s2 == DStopByNode.end()){
                    return false;
                }
                desc.push_back("Take Bus " + r + " from stop " + std::to_string(s1->second) + " to stop " + std::to_string(s2->second));
                i = j;
                continue;
            }

            std::size_t j = i + 1;
            double dist = Distance(path[i].second, path[i + 1].second);
            std::string cur = WayName(path[i].second, path[i + 1].second);
            while(j + 1 < path.size() && path[j + 1].first == mode){
                std::string nxt = WayName(path[j].second, path[j + 1].second);
                if(!cur.empty() && nxt != cur){
                    break;
                }
                if(cur.empty() && !nxt.empty()){
                    break;
                }
                dist += Distance(path[j].second, path[j + 1].second);
                if(cur.empty()){
                    cur = nxt;
                }
                j++;
            }
            auto n1 = DStreetMap->NodeByID(path[i].second);
            auto n2 = DStreetMap->NodeByID(path[j].second);
            if(!n1 || !n2){
                return false;
            }
            auto dir = SGeographicUtils::BearingToDirection(SGeographicUtils::CalculateBearing(n1->Location(), n2->Location()));
                std::string target = cur;
                std::string word = "along";
                if(target.empty()){
                    word = "toward";
                    std::size_t k = j;
                    while(k < path.size() - 1){
                        auto name = WayName(path[k].second, path[k + 1].second);
                        if(!name.empty()){
                            target = name;
                            break;
                    }
                    k++;
                }
                if(target.empty()){
                    target = "End";
                }
            }
            std::ostringstream out;
            out << ModeName(mode) << " " << dir << " " << word << " " << target << " for " << DistanceString(dist);
            desc.push_back(out.str());
            i = j;
        }

        desc.push_back("End at " + SGeographicUtils::ConvertLLToDMS(last->Location()));
        return true;
    }

    SImplementation(std::shared_ptr<SConfiguration> config)
        : DConfig(std::move(config)){
        if(DConfig){
            DStreetMap = DConfig->StreetMap();
            DBusSystem = DConfig->BusSystem();
        }

        if(DStreetMap){
            DSortedNodes.reserve(DStreetMap->NodeCount());
            for(std::size_t Index = 0; Index < DStreetMap->NodeCount(); Index++){
                auto Node = DStreetMap->NodeByIndex(Index);
                if(Node){
                    DSortedNodes.push_back(Node);
                    DNodeByID[Node->ID()] = Node;
                    DNodeIDs.push_back(Node->ID());
                }
            }
            std::sort(DSortedNodes.begin(), DSortedNodes.end(), [](const auto &Left, const auto &Right){
                return Left->ID() < Right->ID();
            });
            for(std::size_t i = 0; i < DNodeIDs.size(); i++){
                DNodeIndex[DNodeIDs[i]] = i;
            }
        }
        if(DBusSystem){
            for(std::size_t i = 0; i < DBusSystem->StopCount(); i++){
                auto stop = DBusSystem->StopByIndex(i);
                if(stop){
                    DStopByNode[stop->NodeID()] = stop->ID();
                }
            }
        }
        BuildRoadEdges();
        BuildBusEdges();
        DWalkBusEdges = Merge(DWalkEdges, DBusEdges);
        DWalkBikeEdges = Merge(DWalkEdges, DBikeEdges);
    }

    std::size_t NodeCount() const noexcept{
        return DSortedNodes.size();
    }

    std::shared_ptr<CStreetMap::SNode> SortedNodeByIndex(std::size_t index) const noexcept{
        if(index >= DSortedNodes.size()){
            return nullptr;
        }
        return DSortedNodes[index];
    }
};

CDijkstraTransportationPlanner::CDijkstraTransportationPlanner(std::shared_ptr<SConfiguration> config){
    DImplementation = std::make_unique<SImplementation>(std::move(config));
}

CDijkstraTransportationPlanner::~CDijkstraTransportationPlanner() = default;

std::size_t CDijkstraTransportationPlanner::NodeCount() const noexcept{
    return DImplementation->NodeCount();
}

std::shared_ptr<CStreetMap::SNode> CDijkstraTransportationPlanner::SortedNodeByIndex(std::size_t index) const noexcept{
    return DImplementation->SortedNodeByIndex(index);
}

double CDijkstraTransportationPlanner::FindShortestPath(TNodeID src, TNodeID dest, std::vector<TNodeID> &path){
    path.clear();
    std::vector<ETransportationMode> modes;
    double cost = CPathRouter::NoPathExists;
    if(!DImplementation->FindPath(DImplementation->DShortEdges, src, dest, path, modes, cost)){
        return CPathRouter::NoPathExists;
    }
    return cost;
}

double CDijkstraTransportationPlanner::FindFastestPath(TNodeID src, TNodeID dest, std::vector<TTripStep> &path){
    path.clear();
    std::vector<TNodeID> n1;
    std::vector<ETransportationMode> m1;
    double c1 = CPathRouter::NoPathExists;
    bool ok1 = DImplementation->FindPath(DImplementation->DWalkBusEdges, src, dest, n1, m1, c1);

    std::vector<TNodeID> n2;
    std::vector<ETransportationMode> m2;
    double c2 = CPathRouter::NoPathExists;
    bool ok2 = DImplementation->FindPath(DImplementation->DWalkBikeEdges, src, dest, n2, m2, c2);

    if(!ok1 && !ok2){
        return CPathRouter::NoPathExists;
    }
    if(ok1 && (!ok2 || c1 <= c2)){
        path = DImplementation->BuildTrip(n1, m1);
        return c1;
    }
    path = DImplementation->BuildTrip(n2, m2);
    return c2;
}

bool CDijkstraTransportationPlanner::GetPathDescription(const std::vector<TTripStep> &path, std::vector<std::string> &desc) const{
    return DImplementation->BuildDescription(path, desc);
}
