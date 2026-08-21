// CHECK-tier gate for the exported network: numerical parity between the
// PyTorch model and the dependency-free C++ inference path, plus an honest
// measurement of what one inference costs.
//
// Parity is a gate, not a formality: if the heads disagree beyond the stated
// tolerance the learned leaf is a different function from the trained model
// and nothing downstream means anything.
//
// State selection is a deterministic stride over the corpus so that the Python
// side reproduces the identical sample without any extra file.

#include "net.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <atomic>
#include <thread>
#include <vector>

namespace {

constexpr std::size_t kRecordBytes = 72;
constexpr std::size_t kCellCount = 49;

struct Record {
  std::uint8_t board[kCellCount];
  std::uint8_t nextDisc;
  std::uint8_t movesRemaining;
};

std::vector<Record> loadStrided(const std::string& path, std::size_t count) {
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) throw std::runtime_error("cannot open " + path);
  std::fseek(file, 0, SEEK_END);
  const std::size_t total = static_cast<std::size_t>(std::ftell(file)) / kRecordBytes;
  if (total < count) throw std::runtime_error("corpus has fewer records than requested");
  const std::size_t stride = total / count;
  std::vector<Record> records(count);
  std::vector<std::uint8_t> buffer(kRecordBytes);
  for (std::size_t index = 0; index < count; ++index) {
    std::fseek(file, static_cast<long>(index * stride * kRecordBytes), SEEK_SET);
    if (std::fread(buffer.data(), 1, kRecordBytes, file) != kRecordBytes) {
      std::fclose(file);
      throw std::runtime_error("short read on corpus");
    }
    std::memcpy(records[index].board, buffer.data(), kCellCount);
    records[index].nextDisc = buffer[kCellCount];
    records[index].movesRemaining = buffer[kCellCount + 1];
  }
  std::fclose(file);
  return records;
}

}  // namespace

int main(int argc, char** argv) {
  using namespace drop7::leaf;
  std::string modelPath, statesPath, outputPath;
  std::size_t count = 4096;
  int benchIterations = 0;
  int threads = 1;
  try {
    for (int index = 1; index + 1 < argc; index += 2) {
      const std::string key = argv[index];
      const std::string value = argv[index + 1];
      if (key == "--model") modelPath = value;
      else if (key == "--states") statesPath = value;
      else if (key == "--out") outputPath = value;
      else if (key == "--count") count = static_cast<std::size_t>(std::stoul(value));
      else if (key == "--bench") benchIterations = std::stoi(value);
      else if (key == "--threads") threads = std::stoi(value);
      else throw std::runtime_error("unknown option " + key);
    }
    if (modelPath.empty()) throw std::runtime_error("--model is required");

    SurvivalNet net(modelPath);
    std::cerr << "model planes " << net.planes() << " channels " << net.channels()
              << " blocks " << net.blocks() << " horizon " << net.hazardHorizon()
              << " parameters " << net.parameterCount() << " fnv1a 0x" << std::hex
              << net.digest() << std::dec << "\n";

    if (!statesPath.empty()) {
      const std::vector<Record> records = loadStrided(statesPath, count);
      const int outputs = net.hazardHorizon() + 3;
      std::vector<float> results(count * static_cast<std::size_t>(outputs));
      Evaluator evaluator(net);
      const auto started = std::chrono::steady_clock::now();
      for (std::size_t index = 0; index < records.size(); ++index) {
        net.encode(records[index].board, records[index].nextDisc,
                   records[index].movesRemaining, evaluator.input());
        const NetOutput& out = evaluator.forward();
        float* row = results.data() + index * outputs;
        for (int k = 0; k < net.hazardHorizon(); ++k) row[k] = out.hazardLogits[k];
        row[net.hazardHorizon() + 0] = out.lifetimeLog;
        row[net.hazardHorizon() + 1] = out.clears;
        row[net.hazardHorizon() + 2] = out.reveals;
      }
      const double wall = std::chrono::duration<double>(
                              std::chrono::steady_clock::now() - started).count();
      std::cerr << "evaluated " << count << " states in " << wall << " s ("
                << (wall / static_cast<double>(count)) * 1e6 << " us/state, single thread)\n";
      if (!outputPath.empty()) {
        std::FILE* file = std::fopen(outputPath.c_str(), "wb");
        if (file == nullptr) throw std::runtime_error("cannot write " + outputPath);
        const std::uint32_t header[2] = {static_cast<std::uint32_t>(count),
                                         static_cast<std::uint32_t>(outputs)};
        std::fwrite(header, sizeof(std::uint32_t), 2, file);
        std::fwrite(results.data(), sizeof(float), results.size(), file);
        std::fclose(file);
        std::cerr << "wrote " << outputPath << "\n";
      }
    }

    if (benchIterations > 0) {
      // Throughput at the thread count the search would actually use.
      std::vector<Record> records = loadStrided(
          statesPath.empty() ? std::string("") : statesPath,
          std::min<std::size_t>(count, 1024));
      std::atomic<double> worst{0.0};
      const auto started = std::chrono::steady_clock::now();
      std::vector<std::thread> pool;
      for (int worker = 0; worker < threads; ++worker) {
        pool.emplace_back([&, worker]() {
          Evaluator evaluator(net);
          double sink = 0.0;
          for (int iteration = 0; iteration < benchIterations; ++iteration) {
            const Record& record = records[static_cast<std::size_t>(
                (iteration + worker) % static_cast<int>(records.size()))];
            net.encode(record.board, record.nextDisc, record.movesRemaining,
                       evaluator.input());
            sink += evaluator.forward().lifetimeLog;
          }
          double expected = worst.load();
          while (!worst.compare_exchange_weak(expected, expected + sink)) {}
        });
      }
      for (std::thread& thread : pool) thread.join();
      const double wall = std::chrono::duration<double>(
                              std::chrono::steady_clock::now() - started).count();
      const double total = static_cast<double>(benchIterations) * threads;
      std::cout << "bench threads " << threads << " iterations " << benchIterations
                << " wall " << wall << " s  " << (wall / total) * 1e6
                << " us/state (aggregate)  " << total / wall << " states/s\n";
      std::cout << "checksum " << worst.load() << "\n";
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "net-check failed: " << error.what() << "\n";
    return 1;
  }
}
