#include "json_parser.h"

namespace xdp {
    // JsonParser for reading and writing JSON files
    pt::ptree JsonParser::parse(const std::string& jsonFilePath) {
      // std::string jsonFilePath = "xdp.json";
      pt::ptree jsonTree;
      std::ifstream jsonFile(jsonFilePath);
      if (jsonFile.is_open()) {
        try {
          boost::property_tree::read_json(jsonFile, jsonTree);
        } catch (const pt::json_parser_error& e) {
          xrt_core::message::send(severity_level::warning, "XRT", "Failed to parse xdp.json: " + std::string(e.what()));
        }
      } else {
        xrt_core::message::send(severity_level::info, "XRT", "xdp.json not found, proceeding with default settings.");
      }
      return jsonTree;
    }

    void JsonParser::write(const std::string& filename, const MetricCollection& collection) {
        std::ofstream file(filename);
        if (!file) {
            throw std::runtime_error("Error writing to file: " + filename);
        }

        // Print collection 
        std::cout << "!!! After: Writing MetricCollection to JSON file: " << filename << std::endl;
        // Print all metrics for debugging purposes
        for (const auto& metric : collection.metrics) {
            if (metric) {
                metric->print(); // Call the print method of each metric
            } else {
                xrt_core::message::send(severity_level::warning, "XRT", "Null metric found in collection");
            }
        }
      
        boost::property_tree::ptree pt = collection.toPtree();
        boost::property_tree::write_json(file, pt);
    }
};
