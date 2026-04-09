// Tuner files are always compiled with -DTUNING (see Makefile).
// Self-define here so IntelliSense also sees the tuning-guarded externs.
#ifndef TUNING
#define TUNING
#endif

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
/// Includes the fixed bias (non-tunable contributions like pawn material).
static double traceScore(const eval::Trace& t, const double* params) {
  double score = static_cast<double>(t.bias);
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
// Gradient validation — finite-difference check
// ===========================================================================

// Computes analytical gradient for a single parameter and compares against
// central finite-difference approximation.  Catches chain-rule bugs in the
// gradient computation before wasting time on training.
//
// Reference: https://www.chessprogramming.org/Texel%27s_Tuning_Method

/// Compute the analytical gradient for a single parameter.
static double analyticalGradient(
    const std::vector<eval::TrainingPosition>& data,
    const double* params, double K, int paramIdx) {
  const int N = static_cast<int>(data.size());
  double grad = 0.0;
  for (int i = 0; i < N; ++i) {
    double score = traceScore(data[i].trace, params);
    double sig = sigmoid(score, K);
    double factor = 2.0 * (sig - data[i].result)
                   * sig * (1.0 - sig) * (K * log(10.0) / 400.0);
    for (const auto& e : data[i].trace.entries) {
      if (e.idx == paramIdx)
        grad += factor * e.coeff;
    }
  }
  return grad / N;
}

/// Validate analytical gradients against finite-difference for a sample
/// of random parameters.  Returns true if all pass within tolerance.
static bool validateGradients(
    const std::vector<eval::TrainingPosition>& data,
    const double* params, int nParams, double K) {
  constexpr double EPSILON = 1e-4;
  constexpr double REL_TOL = 1e-3;     // Relative tolerance.
  constexpr int    SAMPLE  = 10;        // Number of params to check.

  std::mt19937 rng(12345);
  std::uniform_int_distribution<int> dist(0, nParams - 1);

  // Copy params for perturbation.
  std::vector<double> paramsCopy(params, params + nParams);

  int failures = 0;
  fprintf(stderr, "Gradient validation (%d random params, eps=%.0e):\n",
          SAMPLE, EPSILON);

  for (int s = 0; s < SAMPLE; ++s) {
    int idx = dist(rng);

    // Analytical.
    double ag = analyticalGradient(data, paramsCopy.data(), K, idx);

    // Numerical (central difference).
    double origVal = paramsCopy[idx];
    paramsCopy[idx] = origVal + EPSILON;
    double errPlus = computeError(data, K, paramsCopy.data());
    paramsCopy[idx] = origVal - EPSILON;
    double errMinus = computeError(data, K, paramsCopy.data());
    paramsCopy[idx] = origVal;  // Restore.
    double ng = (errPlus - errMinus) / (2.0 * EPSILON);

    double absDiff = fabs(ag - ng);
    double denom = fmax(fabs(ag), fabs(ng)) + 1e-12;
    double relErr = absDiff / denom;

    const char* status = (relErr < REL_TOL) ? "OK" : "FAIL";
    if (relErr >= REL_TOL) ++failures;

    fprintf(stderr, "  [%s] param %4d (%-24s): analytical=%+.8e  numerical=%+.8e  relErr=%.2e\n",
            status, idx, eval::tuning::getName(idx), ag, ng, relErr);
  }

  if (failures > 0)
    fprintf(stderr, "WARNING: %d/%d gradient checks FAILED!\n", failures, SAMPLE);
  else
    fprintf(stderr, "All %d gradient checks passed.\n", SAMPLE);

  return failures == 0;
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

/// Collect ordered indices of KD_TABLE_1 .. KD_TABLE_12 for monotonicity
/// enforcement.  Returns indices sorted by table slot (1..12).
/// Reference: https://www.chessprogramming.org/King_Safety#Attacking_King_Zone
static std::vector<int> buildKdTableIndices() {
  int n = eval::tuning::paramCount();
  std::vector<int> indices;
  for (int i = 0; i < n; ++i) {
    if (strncmp(eval::tuning::getName(i), "KD_TABLE_", 9) == 0)
      indices.push_back(i);
  }
  // Indices are already in registry order (slot 1..12), but sort by slot
  // number to be safe.
  std::sort(indices.begin(), indices.end(), [](int a, int b) {
    int slotA = atoi(eval::tuning::getName(a) + 9);
    int slotB = atoi(eval::tuning::getName(b) + 9);
    return slotA < slotB;
  });
  return indices;
}

static void adamOptimize(std::vector<eval::TrainingPosition>& trainSet,
                         const std::vector<eval::TrainingPosition>& testSet,
                         double& K, int maxEpochs) {
  const int N = static_cast<int>(trainSet.size());
  const int nParams = eval::tuning::paramCount();
  const int nThreads = std::max(1,
      static_cast<int>(std::thread::hardware_concurrency()));
  const int chunkSize = (N + nThreads - 1) / nThreads;

  std::vector<bool> isPst = buildPstFlags();
  std::vector<int> kdIndices = buildKdTableIndices();

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

  // --- K recalculation interval ---
  // As parameters drift during training, the optimal sigmoid scaling
  // constant K shifts.  Recalculate every 50 epochs.
  // Reference: https://www.chessprogramming.org/Texel%27s_Tuning_Method
  constexpr int K_RECALC_INTERVAL = 50;

  // --- Early stopping ---
  // Track best test MSE and stop if no improvement for 50 epochs.
  // Saves the best parameters (lowest test MSE).
  // Reference: https://www.chessprogramming.org/Texel%27s_Tuning_Method
  constexpr int PATIENCE = 50;
  double bestTestMSE = 1e30;
  int epochsSinceImprove = 0;
  std::vector<double> bestParams = params;

  for (int epoch = 1; epoch <= maxEpochs; ++epoch) {
    // --- Cosine annealing learning rate schedule ---
    // Gradually reduces LR from ADAM_LR toward a minimum floor, reducing
    // oscillation in later epochs while maintaining aggressive early
    // learning.  The floor at 1% of initial LR prevents late-epoch
    // stagnation — without it, LR drops to ~0.001 by epoch 400 and
    // parameters can barely move in the final 100 epochs.
    // Reference: Loshchilov & Hutter, "SGDR: Stochastic Gradient Descent
    // with Warm Restarts", 2017.
    double lr = ADAM_LR * std::max(0.01, 0.5 * (1.0 + cos(M_PI * epoch / maxEpochs)));

    // --- K recalculation ---
    if (epoch > 1 && (epoch % K_RECALC_INTERVAL) == 0) {
      K = findOptimalK(trainSet, params.data());
      fprintf(stderr, "  Recalculated K = %.6f at epoch %d\n", K, epoch);
    }

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

      params[i] -= lr * mHat / (sqrt(vHat) + ADAM_EPS);
    }

    // --- King danger table monotonicity enforcement ---
    // The king danger table must be non-decreasing: more attackers should
    // never produce less danger.  After each Adam step, clamp any dip up
    // to the previous slot's value.  TABLE[0] = 0 is fixed (not tuned).
    // Reference: https://www.chessprogramming.org/King_Safety#Attacking_King_Zone
    for (size_t k = 1; k < kdIndices.size(); ++k) {
      if (params[kdIndices[k]] < params[kdIndices[k - 1]])
        params[kdIndices[k]] = params[kdIndices[k - 1]];
    }

    // --- Per-epoch bounds clamping ---
    // Without this, parameters drift outside [min, max] during training
    // and only get clamped at the very end.  Combined with min=0 bounds,
    // parameters that oscillate slightly negative accumulate Adam momentum
    // toward 0 and get locked there by final-epoch clamping.  Per-epoch
    // clamping keeps every parameter within its physical bounds at all
    // times, preventing this convergence failure.
    for (int i = 0; i < nParams; ++i) {
      double lo = static_cast<double>(eval::tuning::getMin(i));
      double hi = static_cast<double>(eval::tuning::getMax(i));
      if (params[i] < lo) params[i] = lo;
      if (params[i] > hi) params[i] = hi;
    }

    // ----- Report + early stopping -----
    if (epoch == 1 || epoch % 5 == 0 || epoch == maxEpochs) {
      double trainErr = computeError(trainSet, K, params.data());
      double testErr  = computeError(testSet, K, params.data());
      fprintf(stderr, "Epoch %4d: train MSE = %.10f, test MSE = %.10f  (lr=%.6f)\n",
              epoch, trainErr, testErr, lr);

      if (testErr < bestTestMSE) {
        bestTestMSE = testErr;
        bestParams = params;
        epochsSinceImprove = 0;
      } else {
        epochsSinceImprove += (epoch == 1) ? 1 : 5;
      }
      if (epochsSinceImprove >= PATIENCE) {
        fprintf(stderr, "Early stopping at epoch %d (no improvement for %d epochs)\n",
                epoch, PATIENCE);
        break;
      }
    }
  }

  // Use the best parameters found (lowest test MSE).
  params = bestParams;

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

  // --- Pre-build index maps (one-time O(n) lookups) ---
  // Material MG indices (in MATERIAL[] order: pawn, knight, bishop, rook, queen).
  const char* matMgNames[5] = {"MAT_PAWN_MG", "MAT_KNIGHT_MG", "MAT_BISHOP_MG", "MAT_ROOK_MG", "MAT_QUEEN_MG"};
  int matMgIdx[5];
  for (int i = 0; i < 5; ++i) matMgIdx[i] = eval::findParam(matMgNames[i]);

  // Material EG indices (in MATERIAL_EG[] order: pawn, knight, bishop, rook, queen).
  const char* matEgNames[5] = {"MAT_PAWN_EG", "MAT_KNIGHT_EG", "MAT_BISHOP_EG", "MAT_ROOK_EG", "MAT_QUEEN_EG"};
  int matEgIdx[5];
  for (int i = 0; i < 5; ++i) matEgIdx[i] = eval::findParam(matEgNames[i]);

  // PST indices (12 tables × 64 squares).
  const char* pstNames[12] = {
    "PST_PAWN_MG", "PST_KNIGHT_MG", "PST_BISHOP_MG",
    "PST_ROOK_MG", "PST_QUEEN_MG",  "PST_KING_MG",
    "PST_PAWN_EG", "PST_KNIGHT_EG", "PST_BISHOP_EG",
    "PST_ROOK_EG", "PST_QUEEN_EG",  "PST_KING_EG",
  };
  int pstIdx[12][64];
  for (int tbl = 0; tbl < 12; ++tbl) {
    for (int sq = 0; sq < 64; ++sq) {
      char paramName[32];
      std::snprintf(paramName, sizeof(paramName), "%s_%d", pstNames[tbl], sq);
      pstIdx[tbl][sq] = eval::findParam(paramName);
    }
  }

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

  // --- C++ formatted MATERIAL array (MG) ---
  printf("// --- C++ Material arrays ---\n");
  printf("EVAL_CONST MAT_ELEM MATERIAL[] = {");
  for (int i = 0; i < 5; ++i) {
    if (i > 0) printf(", ");
    printf("%d", eval::tuning::getValue(matMgIdx[i]));
  }
  printf(", 0};\n");

  // --- C++ formatted MATERIAL_EG array ---
  printf("EVAL_CONST MAT_ELEM MATERIAL_EG[] = {");
  for (int i = 0; i < 5; ++i) {
    if (i > 0) printf(", ");
    printf("%d", eval::tuning::getValue(matEgIdx[i]));
  }
  printf(", 0};\n\n");

  // --- C++ formatted PST arrays ---
  for (int tbl = 0; tbl < 12; ++tbl) {
    printf("// --- %s ---\n", pstNames[tbl]);
    printf("EVAL_CONST PST_ELEM %s[64] = {\n", pstNames[tbl]);
    for (int sq = 0; sq < 64; ++sq) {
      int val = (pstIdx[tbl][sq] >= 0) ? eval::tuning::getValue(pstIdx[tbl][sq]) : 0;
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

  // ---- Trace/eval divergence validation ----------------------------------
  // Verify that the trace dot product matches evaluatePosition() for a
  // sample of positions.  Catches any drift between extractTrace() and
  // evaluatePosition() early, before wasting time on optimization.
  {
    int n = eval::tuning::paramCount();
    std::vector<double> params(n);
    for (int i = 0; i < n; ++i)
      params[i] = static_cast<double>(eval::tuning::getValue(i));

    int checked = 0, mismatches = 0;
    // Validate up to 5000 positions (enough to catch systematic errors).
    int sampleSize = std::min(5000, static_cast<int>(rawEntries.size()));
    for (int i = 0; i < sampleSize; ++i) {
      int evalScore = eval::evaluatePosition(rawEntries[i].bb);
      const auto& tp = (i < splitIdx) ? trainSet[i]
                                      : testSet[i - splitIdx];
      double traceVal = traceScore(tp.trace, params.data());
      int tracei = static_cast<int>(std::round(traceVal));
      if (std::abs(evalScore - tracei) > 1) {
        if (mismatches < 10)
          fprintf(stderr,
                  "  MISMATCH pos %d: eval=%d trace=%d (diff=%d)\n",
                  i, evalScore, tracei, evalScore - tracei);
        ++mismatches;
      }
      ++checked;
    }
    if (mismatches > 0)
      fprintf(stderr,
              "WARNING: %d/%d positions have trace/eval divergence!\n",
              mismatches, checked);
    else
      fprintf(stderr, "Trace validation: %d positions OK\n", checked);
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

  // Validate analytical gradients before training.
  validateGradients(trainSet, initParams.data(),
                    eval::tuning::paramCount(), K);

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
