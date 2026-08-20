#pragma once

// JSONL serialization for scenarios.
//
// One scenario per line, so a suite is diffable, streamable, and readable from
// any language.  The reader ignores unknown keys, which lets `mint` write the
// scenario fields and its difficulty labels into the same object while `solve`
// and any later consumer still load the scenario alone.

#include "scenario.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace drop7::scenario {

inline std::string joinBytes(const std::uint8_t* values, std::size_t count) {
  std::string out;
  out.reserve(count * 3);
  for (std::size_t index = 0; index < count; ++index) {
    if (index != 0) out.push_back(',');
    out += std::to_string(static_cast<int>(values[index]));
  }
  return out;
}

// The scenario's own fields, without the enclosing braces, so a caller can
// append label fields to the same JSON object.
inline std::string scenarioFields(const Scenario& scenario) {
  std::ostringstream out;
  out << "\"schema\":\"drop7-scenario-v1\"";
  out << ",\"id\":\"" << std::string(scenario.id) << "\"";
  out << ",\"board\":[" << joinBytes(scenario.board.data(), kCellCount) << "]";
  out << ",\"latent\":[" << joinBytes(scenario.latent.data(), kCellCount)
      << "]";
  out << ",\"movesRemaining\":" << static_cast<int>(scenario.moves_remaining);
  out << ",\"horizon\":" << static_cast<int>(scenario.horizon);
  out << ",\"discTape\":["
      << joinBytes(scenario.disc_tape.data(), scenario.disc_tape.size()) << "]";
  out << ",\"riseLatent\":[";
  for (std::size_t row = 0; row < scenario.rise_latent.size(); ++row) {
    if (row != 0) out << ',';
    out << '[' << joinBytes(scenario.rise_latent[row].data(), kBoardSize)
        << ']';
  }
  out << ']';
  return out.str();
}

inline std::string serializeScenario(const Scenario& scenario) {
  return "{" + scenarioFields(scenario) + "}";
}

namespace io_detail {

inline std::size_t findKey(const std::string& line, const std::string& key) {
  const std::string needle = "\"" + key + "\"";
  std::size_t at = line.find(needle);
  if (at == std::string::npos) return std::string::npos;
  at = line.find(':', at + needle.size());
  if (at == std::string::npos) return std::string::npos;
  ++at;
  while (at < line.size() &&
         std::isspace(static_cast<unsigned char>(line[at]))) {
    ++at;
  }
  return at;
}

inline bool parseString(const std::string& line, const std::string& key,
                        std::string& out) {
  const std::size_t at = findKey(line, key);
  if (at == std::string::npos || line[at] != '"') return false;
  const std::size_t end = line.find('"', at + 1);
  if (end == std::string::npos) return false;
  out = line.substr(at + 1, end - at - 1);
  return true;
}

inline bool parseInt(const std::string& line, const std::string& key,
                     long long& out) {
  const std::size_t at = findKey(line, key);
  if (at == std::string::npos) return false;
  char* end = nullptr;
  out = std::strtoll(line.c_str() + at, &end, 10);
  return end != line.c_str() + at;
}

// Parses `[a,b,c]` starting at `at`; returns the index just past the ']'.
inline std::size_t parseFlatArray(const std::string& line, std::size_t at,
                                  std::vector<std::uint8_t>& out) {
  out.clear();
  if (at >= line.size() || line[at] != '[') return std::string::npos;
  ++at;
  while (at < line.size()) {
    while (at < line.size() &&
           (std::isspace(static_cast<unsigned char>(line[at])) ||
            line[at] == ',')) {
      ++at;
    }
    if (at < line.size() && line[at] == ']') return at + 1;
    char* end = nullptr;
    const long value = std::strtol(line.c_str() + at, &end, 10);
    if (end == line.c_str() + at) return std::string::npos;
    out.push_back(static_cast<std::uint8_t>(value));
    at = static_cast<std::size_t>(end - line.c_str());
  }
  return std::string::npos;
}

inline bool parseFlatArrayKey(const std::string& line, const std::string& key,
                              std::vector<std::uint8_t>& out) {
  const std::size_t at = findKey(line, key);
  if (at == std::string::npos) return false;
  return parseFlatArray(line, at, out) != std::string::npos;
}

inline bool parseNestedArrayKey(const std::string& line, const std::string& key,
                                std::vector<RiseRow>& out) {
  out.clear();
  std::size_t at = findKey(line, key);
  if (at == std::string::npos || line[at] != '[') return false;
  ++at;
  while (at < line.size()) {
    while (at < line.size() &&
           (std::isspace(static_cast<unsigned char>(line[at])) ||
            line[at] == ',')) {
      ++at;
    }
    if (at < line.size() && line[at] == ']') return true;
    std::vector<std::uint8_t> row;
    at = parseFlatArray(line, at, row);
    if (at == std::string::npos || row.size() != kBoardSize) return false;
    RiseRow fixed{};
    for (int index = 0; index < kBoardSize; ++index) fixed[index] = row[index];
    out.push_back(fixed);
  }
  return false;
}

}  // namespace io_detail

inline bool deserializeScenario(const std::string& line, Scenario& scenario,
                                std::string& error) {
  std::string schema;
  if (!io_detail::parseString(line, "schema", schema) ||
      schema != "drop7-scenario-v1") {
    error = "missing or unknown schema";
    return false;
  }
  std::string id;
  if (!io_detail::parseString(line, "id", id) || id.size() != 16) {
    error = "missing or malformed id";
    return false;
  }
  std::vector<std::uint8_t> board;
  std::vector<std::uint8_t> latent;
  std::vector<std::uint8_t> tape;
  if (!io_detail::parseFlatArrayKey(line, "board", board) ||
      board.size() != kCellCount) {
    error = "board must hold 49 values";
    return false;
  }
  if (!io_detail::parseFlatArrayKey(line, "latent", latent) ||
      latent.size() != kCellCount) {
    error = "latent must hold 49 values";
    return false;
  }
  if (!io_detail::parseFlatArrayKey(line, "discTape", tape)) {
    error = "discTape missing";
    return false;
  }
  long long moves_remaining = 0;
  long long horizon = 0;
  if (!io_detail::parseInt(line, "movesRemaining", moves_remaining) ||
      !io_detail::parseInt(line, "horizon", horizon)) {
    error = "movesRemaining or horizon missing";
    return false;
  }
  std::vector<RiseRow> rises;
  if (!io_detail::parseNestedArrayKey(line, "riseLatent", rises)) {
    error = "riseLatent missing or malformed";
    return false;
  }
  scenario = Scenario{};
  for (int index = 0; index < kCellCount; ++index) {
    scenario.board[index] = board[index];
    scenario.latent[index] = latent[index];
  }
  scenario.moves_remaining = static_cast<std::uint8_t>(moves_remaining);
  scenario.horizon = static_cast<std::uint8_t>(horizon);
  scenario.disc_tape = tape;
  scenario.rise_latent = rises;
  for (std::size_t index = 0; index < id.size(); ++index) {
    scenario.id[index] = id[index];
  }
  scenario.id[16] = '\0';
  if (!scenarioIdMatches(scenario)) {
    error = "content hash does not match the stored id";
    return false;
  }
  return true;
}

inline bool loadScenarioFile(const std::string& path,
                             std::vector<Scenario>& out, std::string& error) {
  std::ifstream input(path);
  if (!input) {
    error = "cannot open " + path;
    return false;
  }
  std::string line;
  int line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    if (line.find_first_not_of(" \t\r\n") == std::string::npos) continue;
    Scenario scenario;
    std::string line_error;
    if (!deserializeScenario(line, scenario, line_error)) {
      error = path + ":" + std::to_string(line_number) + ": " + line_error;
      return false;
    }
    out.push_back(scenario);
  }
  return true;
}

}  // namespace drop7::scenario
