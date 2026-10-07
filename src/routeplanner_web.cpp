#include "CSVBusSystem.h"
#include "DSVReader.h"
#include "FileDataFactory.h"
#include "GeographicUtils.h"
#include "OpenStreetMap.h"
#include "PredictedSpeedStreetMap.h"
#include "StringUtils.h"
#include "TransportationPlannerConfig.h"
#include "XMLReader.h"
#include "DijkstraTransportationPlanner.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct SRequest {
    std::string DStartLabel;
    std::string DEndLabel;
    double DStartLatitude = 0.0;
    double DStartLongitude = 0.0;
    double DEndLatitude = 0.0;
    double DEndLongitude = 0.0;
    std::string DOptimization;
};

std::string EscapeJSONString(const std::string &value) {
    std::string out;
    out.reserve(value.size());

    for (const char character : value) {
        switch (character) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out.push_back(character);
                break;
        }
    }

    return out;
}

bool ExtractStringField(const std::string &payload, const std::string &key, std::string &value) {
    std::regex pattern("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch matches;

    if (!std::regex_search(payload, matches, pattern)) {
        return false;
    }

    value = matches[1].str();
    return true;
}

bool ExtractDoubleField(const std::string &payload, const std::string &key, double &value) {
    std::regex pattern("\"" + key + "\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?)");
    std::smatch matches;

    if (!std::regex_search(payload, matches, pattern)) {
        return false;
    }

    value = std::stod(matches[1].str());
    return true;
}

bool ParseRequest(const std::string &payload, SRequest &request) {
    return ExtractStringField(payload, "startLabel", request.DStartLabel) &&
           ExtractStringField(payload, "endLabel", request.DEndLabel) &&
           ExtractStringField(payload, "optimization", request.DOptimization) &&
           ExtractDoubleField(payload, "startLatitude", request.DStartLatitude) &&
           ExtractDoubleField(payload, "startLongitude", request.DStartLongitude) &&
           ExtractDoubleField(payload, "endLatitude", request.DEndLatitude) &&
           ExtractDoubleField(payload, "endLongitude", request.DEndLongitude);
}

std::string FormatDistance(double distanceMiles) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << distanceMiles;
    return out.str() + " mi";
}

std::string FormatTimeMinutes(double totalMinutes) {
    auto roundedMinutes = static_cast<long long>(std::llround(totalMinutes));
    if (roundedMinutes < 60) {
        return std::to_string(roundedMinutes) + " min";
    }

    const auto hours = roundedMinutes / 60;
    const auto minutes = roundedMinutes % 60;

    if (minutes == 0) {
        return std::to_string(hours) + " hr";
    }

    return std::to_string(hours) + " hr " + std::to_string(minutes) + " min";
}

std::string ModeToAPIName(CTransportationPlanner::ETransportationMode mode) {
    if (mode == CTransportationPlanner::ETransportationMode::Bike) {
        return "bike";
    }

    if (mode == CTransportationPlanner::ETransportationMode::Bus) {
        return "shuttle";
    }

    return "walk";
}

std::string ModeToDisplayName(CTransportationPlanner::ETransportationMode mode) {
    if (mode == CTransportationPlanner::ETransportationMode::Bike) {
        return "Bike";
    }

    if (mode == CTransportationPlanner::ETransportationMode::Bus) {
        return "Bus";
    }

    return "Walk";
}

double ParseDistanceMiles(const std::string &text) {
    std::smatch matches;

    if (std::regex_search(text, matches, std::regex("([0-9]+(?:\\.[0-9]+)?) mi"))) {
        return std::stod(matches[1].str());
    }

    if (std::regex_search(text, matches, std::regex("([0-9]+(?:\\.[0-9]+)?) ft"))) {
        return std::stod(matches[1].str()) / 5280.0;
    }

    return 0.0;
}

std::string BuildExplanation(const std::string &optimization, const std::string &dominantMode) {
    if (optimization == "fastest") {
        return "Fastest mode leans on " + dominantMode + " segments. It prioritizes low travel time from the real planner graph.";
    }

    return "Shortest mode leans on " + dominantMode + " segments. It keeps the path compact on the real planner graph.";
}

std::string BuildCampusFeel(const std::string &optimization) {
    if (optimization == "fastest") {
        return "This route comes from the C++ planner's fastest-time search over the street and bus network.";
    }

    return "This route comes from the C++ planner's shortest-distance search over the street network.";
}

std::string BuildGeometryJSON(std::shared_ptr<CStreetMap> map,
                              const std::vector<CTransportationPlanner::TTripStep> &tripSteps) {
    std::ostringstream geometryJSON;
    geometryJSON << "{\"type\":\"LineString\",\"coordinates\":[";

    bool wroteCoordinate = false;
    for (const auto &tripStep : tripSteps) {
        auto node = map->NodeByID(tripStep.second);
        if (!node) {
            continue;
        }

        if (wroteCoordinate) {
            geometryJSON << ",";
        }

        geometryJSON << "["
                     << std::fixed << std::setprecision(6) << node->Location().DLongitude << ","
                     << std::fixed << std::setprecision(6) << node->Location().DLatitude << "]";
        wroteCoordinate = true;
    }

    geometryJSON << "]}";
    return geometryJSON.str();
}

void WriteErrorJSON(const std::string &code, const std::string &message) {
    std::cout << "{\"error\":{\"code\":\"" << EscapeJSONString(code) << "\",\"message\":\""
              << EscapeJSONString(message) << "\"}}";
}

int Main(const std::string &dataDirectory, bool usePredictedSpeeds) {
    std::ostringstream buffer;
    buffer << std::cin.rdbuf();
    const auto payload = buffer.str();

    SRequest request;
    if (!ParseRequest(payload, request)) {
        WriteErrorJSON("INVALID_REQUEST", "Expected start/end labels, coordinates, and optimization.");
        return 1;
    }

    auto dataFactory = std::make_shared<CFileDataFactory>(dataDirectory);
    auto xml = std::make_shared<CXMLReader>(dataFactory->CreateSource("davis.osm"));
    auto stopcsv = std::make_shared<CDSVReader>(dataFactory->CreateSource("stops.csv"), ',');
    auto routecsv = std::make_shared<CDSVReader>(dataFactory->CreateSource("routes.csv"), ',');

    std::shared_ptr<CStreetMap> map = std::make_shared<COpenStreetMap>(xml);

    // ML fallback speeds for roads without a maxspeed tag (see ml/train.py).
    const auto predictionsPath = std::filesystem::path(dataDirectory) / "speed_predictions.csv";
    if (usePredictedSpeeds && std::filesystem::exists(predictionsPath)) {
        auto predictionReader = std::make_shared<CDSVReader>(dataFactory->CreateSource("speed_predictions.csv"), ',');
        map = std::make_shared<CPredictedSpeedStreetMap>(map, CPredictedSpeedStreetMap::LoadPredictions(predictionReader));
    }
    auto bus = std::make_shared<CCSVBusSystem>(stopcsv, routecsv);
    auto config = std::make_shared<STransportationPlannerConfig>(map, bus);
    CDijkstraTransportationPlanner planner(config);

    auto findNearestNode = [&](double latitude, double longitude) {
        CStreetMap::TNodeID bestNode = CStreetMap::InvalidNodeID;
        double bestDistance = std::numeric_limits<double>::max();
        const CStreetMap::SLocation target(latitude, longitude);

        for (std::size_t index = 0; index < map->NodeCount(); index++) {
            auto node = map->NodeByIndex(index);
            if (!node) {
                continue;
            }

            const auto distance = SGeographicUtils::HaversineDistanceInMiles(target, node->Location());
            if (distance < bestDistance) {
                bestDistance = distance;
                bestNode = node->ID();
            }
        }

        return bestNode;
    };

    const auto startNode = findNearestNode(request.DStartLatitude, request.DStartLongitude);
    const auto endNode = findNearestNode(request.DEndLatitude, request.DEndLongitude);

    if (startNode == CStreetMap::InvalidNodeID || endNode == CStreetMap::InvalidNodeID) {
        WriteErrorJSON("INVALID_LOCATION", "Unable to resolve the requested start or end location.");
        return 1;
    }

    std::vector<CTransportationPlanner::TTripStep> tripSteps;
    double totalDistanceMiles = 0.0;
    double totalTimeHours = CPathRouter::NoPathExists;

    if (request.DOptimization == "shortest") {
        std::vector<CTransportationPlanner::TNodeID> shortestPath;
        totalDistanceMiles = planner.FindShortestPath(startNode, endNode, shortestPath);

        if (totalDistanceMiles == CPathRouter::NoPathExists) {
          WriteErrorJSON("ROUTE_NOT_FOUND", "No path found by the C++ shortest-path planner.");
          return 1;
        }

        for (const auto nodeId : shortestPath) {
            tripSteps.push_back(std::make_pair(CTransportationPlanner::ETransportationMode::Walk, nodeId));
        }
    }
    else if (request.DOptimization == "fastest") {
        totalTimeHours = planner.FindFastestPath(startNode, endNode, tripSteps);

        if (totalTimeHours == CPathRouter::NoPathExists) {
            WriteErrorJSON("ROUTE_NOT_FOUND", "No path found by the C++ fastest-path planner.");
            return 1;
        }
    }
    else {
        WriteErrorJSON("INVALID_OPTIMIZATION", "Optimization must be shortest or fastest.");
        return 1;
    }

    if (tripSteps.empty()) {
        WriteErrorJSON("ROUTE_NOT_FOUND", "The C++ planner returned an empty path.");
        return 1;
    }

    double computedPathDistanceMiles = 0.0;
    for (std::size_t index = 1; index < tripSteps.size(); index++) {
        auto fromNode = map->NodeByID(tripSteps[index - 1].second);
        auto toNode = map->NodeByID(tripSteps[index].second);
        if (!fromNode || !toNode) {
            continue;
        }
        computedPathDistanceMiles += SGeographicUtils::HaversineDistanceInMiles(fromNode->Location(), toNode->Location());
    }

    totalDistanceMiles = computedPathDistanceMiles;
    if (request.DOptimization == "shortest") {
        totalTimeHours = totalDistanceMiles / config->WalkSpeed();
    }

    double walkDistance = 0.0;
    double bikeDistance = 0.0;
    double shuttleDistance = 0.0;
    double walkMinutes = 0.0;
    double bikeMinutes = 0.0;
    double shuttleMinutes = 0.0;
    std::size_t walkSteps = 0;
    std::size_t bikeSteps = 0;
    std::size_t shuttleSteps = 0;

    for (std::size_t index = 1; index < tripSteps.size(); index++) {
        auto fromNode = map->NodeByID(tripSteps[index - 1].second);
        auto toNode = map->NodeByID(tripSteps[index].second);

        if (!fromNode || !toNode) {
            continue;
        }

        const auto segmentDistance = SGeographicUtils::HaversineDistanceInMiles(fromNode->Location(), toNode->Location());
        const auto segmentMode = ModeToAPIName(tripSteps[index].first);
        double speed = config->WalkSpeed();

        if (segmentMode == "bike") {
            speed = config->BikeSpeed();
            bikeDistance += segmentDistance;
        }
        else if (segmentMode == "shuttle") {
            speed = config->DefaultSpeedLimit();
            shuttleDistance += segmentDistance;
        }
        else {
            walkDistance += segmentDistance;
        }

        const auto segmentMinutes = speed > 0.0 ? (segmentDistance / speed) * 60.0 : 0.0;
        if (segmentMode == "bike") {
            bikeMinutes += segmentMinutes;
        }
        else if (segmentMode == "shuttle") {
            shuttleMinutes += segmentMinutes;
        }
        else {
            walkMinutes += segmentMinutes;
        }
    }

    std::vector<std::string> descriptions;
    if (!planner.GetPathDescription(tripSteps, descriptions)) {
        WriteErrorJSON("DESCRIPTION_ERROR", "The C++ planner could not describe the route.");
        return 1;
    }

    std::vector<std::string> usableDescriptions;
    for (std::size_t index = 0; index < descriptions.size(); index++) {
        if (index == 0 || index + 1 == descriptions.size()) {
            continue;
        }
        usableDescriptions.push_back(descriptions[index]);
    }

    std::vector<std::string> stepModes;
    std::ostringstream stepsJSON;
    stepsJSON << "[";

    for (std::size_t index = 0; index < usableDescriptions.size(); index++) {
        const auto &instruction = usableDescriptions[index];
        std::string mode = "walk";

        if (instruction.rfind("Bike ", 0) == 0) {
            mode = "bike";
        }
        else if (instruction.rfind("Take Bus ", 0) == 0) {
            mode = "shuttle";
        }

        stepModes.push_back(mode);

        const double distanceMiles = ParseDistanceMiles(instruction);
        std::string distanceText;
        std::string timeText;

        if (distanceMiles > 0.0) {
            distanceText = FormatDistance(distanceMiles);
            double speed = config->WalkSpeed();
            if (mode == "bike") {
                speed = config->BikeSpeed();
            }
            else if (mode == "shuttle") {
                speed = config->DefaultSpeedLimit();
            }

            const double minutes = speed > 0.0 ? (distanceMiles / speed) * 60.0 : 0.0;
            if (minutes > 0.0) {
                timeText = FormatTimeMinutes(minutes);
            }
        }

        if (mode == "bike") {
            bikeSteps++;
        }
        else if (mode == "shuttle") {
            shuttleSteps++;
        }
        else {
            walkSteps++;
        }

        if (index > 0) {
            stepsJSON << ",";
        }

        stepsJSON << "{"
                  << "\"index\":" << (index + 1) << ","
                  << "\"instruction\":\"" << EscapeJSONString(instruction) << "\","
                  << "\"mode\":\"" << EscapeJSONString(mode) << "\","
                  << "\"distance\":\"" << EscapeJSONString(distanceText) << "\","
                  << "\"time\":\"" << EscapeJSONString(timeText) << "\""
                  << "}";
    }

    stepsJSON << "]";

    std::string dominantMode = "walk";
    if (!stepModes.empty()) {
        std::size_t walkCount = 0;
        std::size_t bikeCount = 0;
        std::size_t shuttleCount = 0;

        for (const auto &mode : stepModes) {
            if (mode == "bike") {
                bikeCount++;
            }
            else if (mode == "shuttle") {
                shuttleCount++;
            }
            else {
                walkCount++;
            }
        }

        if (shuttleCount >= bikeCount && shuttleCount >= walkCount) {
            dominantMode = "shuttle";
        }
        else if (bikeCount >= walkCount) {
            dominantMode = "bike";
        }
    }

    const double totalMinutes = totalTimeHours * 60.0;
    const auto geometryJSON = BuildGeometryJSON(map, tripSteps);

    std::ostringstream breakdownJSON;
    breakdownJSON << "[";
    bool wroteBreakdown = false;

    const auto appendBreakdown = [&](const std::string &mode,
                                     const std::string &label,
                                     std::size_t stepCount,
                                     double distanceMiles,
                                     double timeMinutes) {
        if (!stepCount && distanceMiles <= 0.0 && timeMinutes <= 0.0) {
            return;
        }

        if (wroteBreakdown) {
            breakdownJSON << ",";
        }

        breakdownJSON << "{"
                      << "\"mode\":\"" << EscapeJSONString(mode) << "\","
                      << "\"label\":\"" << EscapeJSONString(label) << "\","
                      << "\"stepCount\":" << stepCount << ","
                      << "\"distance\":\"" << EscapeJSONString(FormatDistance(distanceMiles)) << "\","
                      << "\"time\":\"" << EscapeJSONString(FormatTimeMinutes(timeMinutes)) << "\","
                      << "\"rawDistance\":" << std::fixed << std::setprecision(2) << distanceMiles << ","
                      << "\"rawTime\":" << std::llround(timeMinutes)
                      << "}";
        wroteBreakdown = true;
    };

    appendBreakdown("walk", "Walk", walkSteps, walkDistance, walkMinutes);
    appendBreakdown("bike", "Bike", bikeSteps, bikeDistance, bikeMinutes);
    appendBreakdown("shuttle", "Shuttle", shuttleSteps, shuttleDistance, shuttleMinutes);
    breakdownJSON << "]";

    std::cout << "{"
              << "\"engine\":\"cpp\","
              << "\"summary\":\"" << EscapeJSONString(request.DStartLabel + " to " + request.DEndLabel) << "\","
              << "\"optimization\":\"" << EscapeJSONString(request.DOptimization) << "\","
              << "\"modePreference\":\"any\","
              << "\"totals\":{"
              << "\"distance\":\"" << EscapeJSONString(FormatDistance(totalDistanceMiles)) << "\","
              << "\"time\":\"" << EscapeJSONString(FormatTimeMinutes(totalMinutes)) << "\","
              << "\"rawDistance\":" << std::fixed << std::setprecision(2) << totalDistanceMiles << ","
              << "\"rawTime\":" << std::llround(totalMinutes)
              << "},"
              << "\"geometry\":" << geometryJSON << ","
              << "\"steps\":" << stepsJSON.str() << ","
              << "\"breakdown\":" << breakdownJSON.str() << ","
              << "\"explanation\":\"" << EscapeJSONString(BuildExplanation(request.DOptimization, dominantMode)) << "\","
              << "\"highlights\":{"
              << "\"dominantMode\":\"" << EscapeJSONString(dominantMode) << "\","
              << "\"stepCount\":" << usableDescriptions.size() << ","
              << "\"tradeoffLabel\":\"" << EscapeJSONString(request.DOptimization == "fastest" ? "Saves time" : "Cuts distance") << "\","
              << "\"campusFeel\":\"" << EscapeJSONString(BuildCampusFeel(request.DOptimization)) << "\""
              << "}"
              << "}";

    return 0;
}

}  // namespace

int main(int argc, char *argv[]) {
    std::string dataDirectory = "./data";
    bool usePredictedSpeeds = true;

    for (int index = 1; index < argc; index++) {
        const std::string argument(argv[index]);
        if (argument.rfind("--data=", 0) == 0) {
            dataDirectory = argument.substr(7);
        }
        else if (argument == "--no-predicted-speeds") {
            usePredictedSpeeds = false;
        }
    }

    return Main(dataDirectory, usePredictedSpeeds);
}
