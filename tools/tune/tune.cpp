// ---------------------------------------------------------------------------
// Tuning — Adam gradient-descent optimizer for evaluation parameters.
//
// Uses precomputed Traces (sparse feature vectors) extracted by mirroring
// the logic of evaluatePosition() from evaluation.cpp.  Each trace records
// the coefficients for every nonzero tunable parameter in one position.
// The score is the dot product: score = Σ θ[i] × trace[i].coeff
//
// All parameters are linear in the evaluation function.  King danger table
// entries are tuned directly (weights are constexpr), so analytical
// gradients are exact.
//
// Workflow:
//   1. Load corpus (EPD format with c9 "result" opcode)
//   2. Build name→index map from the tuning registry
//   3. Extract traces for all positions (one-time, O(N))
//   4. Find optimal sigmoid scaling constant K (ternary search)
//   5. Adam gradient descent with float accumulators (~500 epochs)
//   6. Round final params to int, clamp to bounds, output C++ code
//
// After tuning, copy the C++ output (printed to stdout) into
// evaluation.cpp, replacing the existing EVAL_CONST definitions.
// Rebuild the tuner to start the next iteration from updated defaults.
//
// Usage: tune <corpus.epd> [epochs=500]
//
// Reference: https://www.chessprogramming.org/Texel%27s_Tuning_Method
// ---------------------------------------------------------------------------

// Ensure TUNING is defined — the Makefile passes -DTUNING, but the IDE
// IntelliSense may not.  This lets the IDE resolve all #ifdef TUNING types.
#ifndef TUNING
#define TUNING
#endif

#include "attacks.h"
#include "epd.h"
#include "evaluation.h"
#include "position.h"
#include "trace.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <numeric>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace LibreChess;

// ===========================================================================
// Constants
// ===========================================================================

// Adam hyperparameters.
static constexpr double ADAM_LR    = 0.1;
static constexpr double ADAM_BETA1 = 0.9;
static constexpr double ADAM_BETA2 = 0.999;
static constexpr double ADAM_EPS   = 1e-8;

// L2 regularization strength for PST parameters.
static constexpr double L2_LAMBDA = 1e-7;

// ===========================================================================
// Corpus loading — EPD format with c9 "result" opcode
// ===========================================================================

struct RawEntry {
  BitboardSet bb;
  double result;
};

/// Parse the game result from a c9 operand string.
/// Supports standard PGN format ("1-0", "0-1", "1/2-1/2") and decimal
/// format ("1.0", "0.5", "0.0").  Quoted variants are also accepted.
/// Returns -1.0 if the format is not recognized.
static double parseResult(const std::string& s) {
  // Strip surrounding quotes if present.
  std::string r = s;
  if (r.size() >= 2 && r.front() == '"' && r.back() == '"')
    r = r.substr(1, r.size() - 2);

  if (r == "1-0" || r == "1.0") return 1.0;
  if (r == "0-1" || r == "0.0") return 0.0;
  if (r == "1/2-1/2" || r == "0.5") return 0.5;
  return -1.0;
}

static bool loadCorpus(const char* filename, std::vector<RawEntry>& entries) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    fprintf(stderr, "Error: cannot open %s\n", filename);
    return false;
  }

  std::string line;
  while (std::getline(file, line)) {
    if (line.empty()) continue;

    EPDRecord rec = epd::parseEPDLine(line);
    if (rec.fen.empty()) continue;

    const EPDOperation* c9 = rec.findOperation("c9");
    if (!c9 || c9->operandCount == 0) continue;

    double result = parseResult(c9->operands[0]);
    if (result < 0.0) continue;

    Position pos;
    if (!pos.loadFEN(rec.fen + " 0 1")) continue;
    entries.push_back({pos.bitboards(), result});
  }

  fprintf(stderr, "Loaded %d positions from %s\n",
          static_cast<int>(entries.size()), filename);
  return !entries.empty();
}

// ===========================================================================
// Sigmoid and error computation
// ===========================================================================

static double sigmoid(double score, double K) {
  return 1.0 / (1.0 + pow(10.0, -K * score / 400.0));
}

/// Compute the score from a trace using float parameter values.
static double traceScore(const eval::Trace& t, const double* params) {
  double score = 0.0;
  for (const auto& e : t.entries)
    score += params[e.idx] * static_cast<double>(e.coeff);
  return score;
}

/// MSE over a set of positions using trace-based scores.
static double computeError(const std::vector<eval::TrainingPosition>& positions,
                           double K, const double* params) {
  const int N = static_cast<int>(positions.size());
  const int nThreads = std::max(1,
      static_cast<int>(std::thread::hardware_concurrency()));
  const int chunkSize = (N + nThreads - 1) / nThreads;

  std::vector<double> partialErrors(nThreads, 0.0);

  auto worker = [&](int threadIdx) {
    int start = threadIdx * chunkSize;
    int end = std::min(start + chunkSize, N);
    double localError = 0.0;
    for (int i = start; i < end; ++i) {
      double score = traceScore(positions[i].trace, params);
      double predicted = sigmoid(score, K);
      double diff = positions[i].result - predicted;
      localError += diff * diff;
    }
    partialErrors[threadIdx] = localError;
  };

  std::vector<std::thread> threads;
  for (int ti = 0; ti < nThreads; ++ti) threads.emplace_back(worker, ti);
  for (auto& th : threads) th.join();

  double total = 0.0;
  for (double e : partialErrors) total += e;
  return total / N;
}

// ===========================================================================
// Find optimal K — ternary search for sigmoid scaling constant
// ===========================================================================

static double findOptimalK(const std::vector<eval::TrainingPosition>& positions,
                           const double* params) {
  double lo = 0.1, hi = 3.0;
  for (int iter = 0; iter < 50; ++iter) {
    double mid1 = lo + (hi - lo) / 3.0;
    double mid2 = hi - (hi - lo) / 3.0;
    double err1 = computeError(positions, mid1, params);
    double err2 = computeError(positions, mid2, params);
    if (err1 < err2) hi = mid2;
    else lo = mid1;
  }
  return (lo + hi) / 2.0;
}

// ===========================================================================
// Adam gradient descent
// ===========================================================================

/// Identify which param indices are PST entries (for L2 regularization).
static std::vector<bool> buildPstFlags() {
  int n = eval::tuning::paramCount();
  std::vector<bool> isPst(n, false);
  for (int i = 0; i < n; ++i) {
    if (strncmp(eval::tuning::getName(i), "PST_", 4) == 0)
      isPst[i] = true;
  }
  return isPst;
}

static void adamOptimize(std::vector<eval::TrainingPosition>& trainSet,
                         const std::vector<eval::TrainingPosition>& testSet,
                         double K, int maxEpochs) {
  const int N = static_cast<int>(trainSet.size());
  const int nParams = eval::tuning::paramCount();
  const int nThreads = std::max(1,
      static_cast<int>(std::thread::hardware_concurrency()));
  const int chunkSize = (N + nThreads - 1) / nThreads;

  std::vector<bool> isPst = buildPstFlags();

  // Float accumulators — accumulate Adam updates in double precision.
  // Parameters are only rounded to int at the very end after all epochs,
  // preventing sub-integer gradients from being destroyed by per-epoch
  // rounding.
  std::vector<double> params(nParams);
  for (int i = 0; i < nParams; ++i)
    params[i] = static_cast<double>(eval::tuning::getValue(i));

  // Adam state.
  std::vector<double> m(nParams, 0.0);
  std::vector<double> v(nParams, 0.0);

  // Per-thread gradient accumulators.
  std::vector<std::vector<double>> threadGrads(nThreads,
      std::vector<double>(nParams, 0.0));

  for (int epoch = 1; epoch <= maxEpochs; ++epoch) {
    // ----- Compute gradient (multi-threaded) -----
    for (auto& g : threadGrads) std::fill(g.begin(), g.end(), 0.0);

    auto worker = [&](int threadIdx) {
      int start = threadIdx * chunkSize;
      int end = std::min(start + chunkSize, N);
      auto& grad = threadGrads[threadIdx];

      for (int i = start; i < end; ++i) {
        double score = traceScore(trainSet[i].trace, params.data());
        double sig = sigmoid(score, K);
        double factor = 2.0 * (sig - trainSet[i].result)
                       * sig * (1.0 - sig) * (K * log(10.0) / 400.0);
        for (const auto& e : trainSet[i].trace.entries)
          grad[e.idx] += factor * e.coeff;
      }
    };

    std::vector<std::thread> threads;
    for (int ti = 0; ti < nThreads; ++ti) threads.emplace_back(worker, ti);
    for (auto& th : threads) th.join();

    // Merge per-thread gradients.
    std::vector<double> gradient(nParams, 0.0);
    for (int ti = 0; ti < nThreads; ++ti)
      for (int i = 0; i < nParams; ++i) gradient[i] += threadGrads[ti][i];
    for (int i = 0; i < nParams; ++i) gradient[i] /= N;

    // L2 regularization for PSTs.
    for (int i = 0; i < nParams; ++i) {
      if (isPst[i])
        gradient[i] += 2.0 * L2_LAMBDA * params[i];
    }

    // ----- Adam update (float accumulators, no rounding) -----
    double beta1t = pow(ADAM_BETA1, epoch);
    double beta2t = pow(ADAM_BETA2, epoch);

    for (int i = 0; i < nParams; ++i) {
      m[i] = ADAM_BETA1 * m[i] + (1.0 - ADAM_BETA1) * gradient[i];
      v[i] = ADAM_BETA2 * v[i] + (1.0 - ADAM_BETA2) * gradient[i] * gradient[i];

      double mHat = m[i] / (1.0 - beta1t);
      double vHat = v[i] / (1.0 - beta2t);

      params[i] -= ADAM_LR * mHat / (sqrt(vHat) + ADAM_EPS);
    }

    // ----- Report -----
    if (epoch == 1 || epoch % 10 == 0 || epoch == maxEpochs) {
      double trainErr = computeError(trainSet, K, params.data());
      double testErr  = computeError(testSet, K, params.data());
      fprintf(stderr, "Epoch %4d: train MSE = %.10f, test MSE = %.10f\n",
              epoch, trainErr, testErr);
    }
  }

  // ----- Finalize: round to int, clamp to bounds, write to registry -----
  for (int i = 0; i < nParams; ++i) {
    int rounded = static_cast<int>(std::round(params[i]));
    rounded = std::max(rounded, eval::tuning::getMin(i));
    rounded = std::min(rounded, eval::tuning::getMax(i));
    eval::tuning::setValue(i, rounded);
  }
}

// ===========================================================================
// Results output
// ===========================================================================

/// Write tune.txt — machine-readable log of all tuned parameter values.
static void writeTunedValues(const char* filename) {
  FILE* f = fopen(filename, "w");
  if (!f) {
    fprintf(stderr, "Error: cannot write %s\n", filename);
    return;
  }
  int n = eval::tuning::paramCount();
  for (int i = 0; i < n; ++i)
    fprintf(f, "%s=%d\n", eval::tuning::getName(i), eval::tuning::getValue(i));
  fclose(f);
  fprintf(stderr, "Wrote %s (%d params)\n", filename, n);
}

/// Print changed parameters and C++-formatted output for copy-paste.
static void printResults() {
  int n = eval::tuning::paramCount();

  // --- Changed values ---
  printf("\n// --- Changed parameter values ---\n\n");
  int changed = 0;
  for (int i = 0; i < n; ++i) {
    int val = eval::tuning::getValue(i);
    int def = eval::tuning::getDefault(i);
    if (val != def) {
      printf("%-28s = %5d  (was %d)\n",
             eval::tuning::getName(i), val, def);
      ++changed;
    }
  }
  printf("\n// %d parameters changed out of %d total.\n\n", changed, n);

  // --- C++ formatted MATERIAL array ---
  printf("// --- C++ Material array ---\n");
  printf("EVAL_CONST int MATERIAL[] = {100");
  for (int i = 0; i < n; ++i) {
    const char* name = eval::tuning::getName(i);
    if (strcmp(name, "MAT_KNIGHT") == 0) printf(", %d", eval::tuning::getValue(i));
    if (strcmp(name, "MAT_BISHOP") == 0) printf(", %d", eval::tuning::getValue(i));
    if (strcmp(name, "MAT_ROOK") == 0)   printf(", %d", eval::tuning::getValue(i));
    if (strcmp(name, "MAT_QUEEN") == 0)  printf(", %d", eval::tuning::getValue(i));
  }
  printf(", 0};\n\n");

  // --- C++ formatted PST arrays ---
  const char* pstNames[12] = {
    "PST_PAWN_MG", "PST_KNIGHT_MG", "PST_BISHOP_MG",
    "PST_ROOK_MG", "PST_QUEEN_MG",  "PST_KING_MG",
    "PST_PAWN_EG", "PST_KNIGHT_EG", "PST_BISHOP_EG",
    "PST_ROOK_EG", "PST_QUEEN_EG",  "PST_KING_EG",
  };

  for (int tbl = 0; tbl < 12; ++tbl) {
    printf("// --- %s ---\n", pstNames[tbl]);
    printf("EVAL_CONST int %s[64] = {\n", pstNames[tbl]);
    for (int sq = 0; sq < 64; ++sq) {
      char paramName[32];
      std::snprintf(paramName, sizeof(paramName), "%s_%d", pstNames[tbl], sq);
      int idx = eval::findParam(paramName);
      int val = (idx >= 0) ? eval::tuning::getValue(idx) : 0;
      if (sq % 8 == 0) printf("  ");
      printf("%4d", val);
      if (sq < 63) printf(",");
      if (sq % 8 == 7) printf("\n");
    }
    printf("};\n\n");
  }

  // --- C++ formatted scalar constants ---
  printf("// --- Scalar constants ---\n");
  for (int i = 0; i < n; ++i) {
    const char* name = eval::tuning::getName(i);
    if (strncmp(name, "PST_", 4) == 0) continue;
    if (strncmp(name, "MAT_", 4) == 0) continue;
    printf("EVAL_CONST int %-28s = %d;\n", name, eval::tuning::getValue(i));
  }
}

// ===========================================================================
// Entry point
// ===========================================================================

int main(int argc, char* argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: tune <corpus.epd> [epochs=500]\n");
    return 1;
  }

  int maxEpochs = 500;
  if (argc >= 3) maxEpochs = std::atoi(argv[2]);
  if (maxEpochs < 1) maxEpochs = 500;

  // Initialize eval infrastructure.
  eval::initPawnMasks();
  attacks::init();

  // Build name→index map for trace extraction.
  eval::buildParamMap();

  fprintf(stderr, "Registry: %d tunable parameters\n",
          eval::tuning::paramCount());

  // Load corpus.
  std::vector<RawEntry> rawEntries;
  if (!loadCorpus(argv[1], rawEntries)) return 1;

  // Shuffle and split 80/20.
  std::mt19937 rng(42);
  std::shuffle(rawEntries.begin(), rawEntries.end(), rng);
  int splitIdx = static_cast<int>(rawEntries.size() * 0.8);

  fprintf(stderr, "Extracting traces for %d positions...\n",
          static_cast<int>(rawEntries.size()));

  // Extract traces.
  std::vector<eval::TrainingPosition> trainSet, testSet;
  trainSet.reserve(splitIdx);
  testSet.reserve(rawEntries.size() - splitIdx);

  for (int i = 0; i < static_cast<int>(rawEntries.size()); ++i) {
    eval::TrainingPosition tp;
    tp.trace = eval::extractTrace(rawEntries[i].bb);
    tp.result = rawEntries[i].result;
    if (i < splitIdx) trainSet.push_back(std::move(tp));
    else              testSet.push_back(std::move(tp));
  }
  rawEntries.clear();
  rawEntries.shrink_to_fit();

  fprintf(stderr, "Split: %d train, %d test\n",
          static_cast<int>(trainSet.size()),
          static_cast<int>(testSet.size()));

  // Find optimal K.
  fprintf(stderr, "Finding optimal K...\n");
  // Initialize float param snapshot from registry defaults for K search.
  std::vector<double> initParams(eval::tuning::paramCount());
  for (int i = 0; i < eval::tuning::paramCount(); ++i)
    initParams[i] = static_cast<double>(eval::tuning::getValue(i));
  double K = findOptimalK(trainSet, initParams.data());
  fprintf(stderr, "Optimal K = %.6f\n", K);

  double initTrainErr = computeError(trainSet, K, initParams.data());
  double initTestErr  = computeError(testSet, K, initParams.data());
  fprintf(stderr, "Initial: train MSE = %.10f, test MSE = %.10f\n",
          initTrainErr, initTestErr);

  // Adam optimization.
  fprintf(stderr, "Starting Adam (%d params, %d epochs, %d threads)...\n",
          eval::tuning::paramCount(), maxEpochs,
          static_cast<int>(std::thread::hardware_concurrency()));
  adamOptimize(trainSet, testSet, K, maxEpochs);

  // Final error (using integer-rounded values now in the registry).
  std::vector<double> finalParams(eval::tuning::paramCount());
  for (int i = 0; i < eval::tuning::paramCount(); ++i)
    finalParams[i] = static_cast<double>(eval::tuning::getValue(i));
  double finalTrainErr = computeError(trainSet, K, finalParams.data());
  double finalTestErr  = computeError(testSet, K, finalParams.data());
  fprintf(stderr, "\nFinal: train MSE = %.10f (improvement: %.6f)\n",
          finalTrainErr, initTrainErr - finalTrainErr);
  fprintf(stderr, "Final: test  MSE = %.10f (improvement: %.6f)\n",
          finalTestErr, initTestErr - finalTestErr);

  // Write results.
  writeTunedValues("tune.txt");
  printResults();

  return 0;
}
