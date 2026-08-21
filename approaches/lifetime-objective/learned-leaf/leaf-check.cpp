// CHECK-tier gate for the leaf-affordable student: numerical parity between
// PyTorch and the C++ inference path on real corpus states, plus the leaf-cost
// measurement that decides whether the model can play at all.
//
// Same deterministic corpus stride as net-check.cpp so the Python comparator
// reproduces the sample without an extra file.

#include "leafnet.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
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
  if (total < count) throw std::runtime_error("corpus smaller than requested sample");
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
  long benchIterations = 0;
  int threads = 1;
  try {
    for (int index = 1; index + 1 < argc; index += 2) {
      const std::string key = argv[index];
      const std::string value = argv[index + 1];
      if (key == "--model") modelPath = value;
      else if (key == "--states") statesPath = value;
      else if (key == "--out") outputPath = value;
      else if (key == "--count") count = static_cast<std::size_t>(std::stoul(value));
      else if (key == "--bench") benchIterations = std::stol(value);
      else if (key == "--threads") threads = std::stoi(value);
      else throw std::runtime_error("unknown option " + key);
    }
    if (modelPath.empty() || statesPath.empty()) {
      throw std::runtime_error("--model and --states are required");
    }
    LeafNet net(modelPath);
    std::cerr << "leaf model hidden " << net.hidden() << " mid " << net.mid()
              << " outputs " << net.outputs() << " parameters " << net.parameterCount()
              << " fnv1a 0x" << std::hex << net.digest() << std::dec << "\n";

    const std::vector<Record> records = loadStrided(statesPath, count);
    const int outputs = net.hazardHorizon() + 3;
    std::vector<float> results(count * static_cast<std::size_t>(outputs));
    std::vector<float> scratch(static_cast<std::size_t>(net.hidden() + net.mid()));
    LeafOutput out;
    const auto started = std::chrono::steady_clock::now();
    for (std::size_t index = 0; index < records.size(); ++index) {
      net.evaluate(records[index].board, records[index].nextDisc,
                   records[index].movesRemaining, out, scratch.data());
      float* row = results.data() + index * outputs;
      for (int k = 0; k < net.hazardHorizon(); ++k) row[k] = out.hazardLogits[k];
      row[net.hazardHorizon() + 0] = out.lifetimeLog;
      row[net.hazardHorizon() + 1] = out.clears;
      row[net.hazardHorizon() + 2] = out.reveals;
    }
    const double wall = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - started).count();
    std::cerr << "evaluated " << count << " states in " << wall << " s ("
              << (wall / static_cast<double>(count)) * 1e6
              << " us/state, single thread, cold)\n";
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

    if (benchIterations > 0) {
      std::atomic<double> sink{0.0};
      const auto begun = std::chrono::steady_clock::now();
      std::vector<std::thread> pool;
      for (int worker = 0; worker < threads; ++worker) {
        pool.emplace_back([&, worker]() {
          std::vector<float> local(static_cast<std::size_t>(net.hidden() + net.mid()));
          LeafOutput value;
          double total = 0.0;
          for (long iteration = 0; iteration < benchIterations; ++iteration) {
            const Record& record = records[static_cast<std::size_t>(
                (iteration + worker) % static_cast<long>(records.size()))];
            net.evaluate(record.board, record.nextDisc, record.movesRemaining,
                         value, local.data());
            total += value.lifetimeLog;
          }
          double expected = sink.load();
          while (!sink.compare_exchange_weak(expected, expected + total)) {}
        });
      }
      for (std::thread& thread : pool) thread.join();
      const double elapsed = std::chrono::duration<double>(
                                 std::chrono::steady_clock::now() - begun).count();
      const double total = static_cast<double>(benchIterations) * threads;
      std::cout << "bench threads " << threads << " iterations " << benchIterations
                << " wall " << elapsed << " s  "
                << (elapsed / total) * 1e6 << " us/state (aggregate)  "
                << total / elapsed << " states/s  checksum " << sink.load() << "\n";
      std::cout << "single-thread equivalent " << (elapsed * threads / total) * 1e6
                << " us/state\n";
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "leaf-check failed: " << error.what() << "\n";
    return 1;
  }
}
