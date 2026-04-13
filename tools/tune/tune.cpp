// ---------------------------------------------------------------------------
// Tuning — Adam gradient-descent optimizer for evaluation parameters.
//
// Uses precomputed Traces (sparse feature vectors) extracted by mirroring
// the logic of evaluatePosition() from evaluation.cpp.  Each trace records
// the coefficients for every nonzero tunable parameter in one position.
// The score is the dot product: score = Σ θ[i] × trace[i].coeff
//
// All parameters are linear in the evaluation function.  Non-linear
// dependencies (e.g. safe check → KING_SAFETY_TABLE S-curve) are linearized
// at the operating point during trace extraction, so gradients are approximate
// but accurate near current values.
//
// Workflow:
//   1. Load corpus (EPD format with c9 "result" opcode)
//   2. Build name→index map from the tuning registry
//   3. Extract traces for all positions (one-time, O(N))
//   4. Find optimal sigmoid scaling constant K (ternary search)
//   5. Adam gradient descent with float accumulators (~500 epochs)
//   6. Round final params to int, output C++ code
//
// After tuning, copy the formatted block from stdout into
// eval_params.h, replacing the existing EVAL_CONST definitions.
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
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace LibreChess;

// ===========================================================================
// Constants
// ===========================================================================

// Adam hyperparameters.
// LR 0.001 is conservative for ~960 chess eval parameters.  Higher rates
// cause too many parameters to shift at once, disrupting search-eval synergy
// even when individual values look reasonable.
static constexpr double ADAM_LR    = 0.005;
static constexpr double ADAM_BETA1 = 0.9;
static constexpr double ADAM_BETA2 = 0.999;
static constexpr double ADAM_EPS   = 1e-8;

// Mobility table sizes — mirrors EVAL_FIXED values in eval_params.h.
// (EVAL_FIXED = const under TUNING → internal linkage, not accessible here.)
static constexpr int MOB_KNIGHT_SIZE = 9;
static constexpr int MOB_BISHOP_SIZE = 14;
static constexpr int MOB_ROOK_SIZE   = 15;
static constexpr int MOB_QUEEN_SIZE  = 28;



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
/// OCB scaling is applied post-hoc to match evaluatePosition()'s order:
/// taper first, then scale by 3/4.  Pass applyOCB=false for integer-exact
/// validation (which applies *3/4 as a separate integer step).
static double traceScore(const eval::Trace& t, const double* params,
                         bool applyOCB = true) {
  double score = t.bias;
  for (const auto& e : t.entries)
    score += params[e.idx] * e.coeff;
  if (applyOCB && t.hasOCB) score *= 0.75;
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
// Standard ML technique ("gradient checking") — not part of the Texel method
// itself, but essential for verifying chain-rule correctness in the gradient.

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
    if (data[i].trace.hasOCB) factor *= 0.75;
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
// Parameter constraints — frozen params, sign bounds, mobility monotonicity
// ===========================================================================

// Sign constraint for scalar parameters.  Penalties must stay ≤ 0, bonuses
// must stay ≥ 0.  Prevents the optimizer from inverting term semantics.
enum class SignConstraint { NONE, NON_NEGATIVE, NON_POSITIVE };

/// Build a boolean mask identifying frozen (non-tunable) parameters.
/// Material values are frozen because search pruning margins (futility,
/// delta, razoring, SEE thresholds) are calibrated to specific piece values.
/// Tuning material requires co-tuning all search constants — separate task.
/// PSTs are frozen because they interact deeply with search pruning and are
/// already well-calibrated (Michniewski); tuning 752 PST entries at once
/// causes too much collective disruption.
static std::vector<bool> buildFrozenMask() {
  int n = eval::tuning::paramCount();
  std::vector<bool> frozen(n, false);
  for (int i = 0; i < n; ++i) {
    const char* name = eval::tuning::getName(i);
    if (strncmp(name, "MAT_", 4) == 0 || strncmp(name, "PST_", 4) == 0)
      frozen[i] = true;
  }
  return frozen;
}

/// Build sign constraints from parameter names.  Uses the same name-matching
/// pattern as the registry itself — robust against reordering.
static std::vector<SignConstraint> buildSignConstraints() {
  int n = eval::tuning::paramCount();
  std::vector<SignConstraint> constraints(n, SignConstraint::NONE);

  for (int i = 0; i < n; ++i) {
    const char* name = eval::tuning::getName(i);

    // --- Non-positive (penalties / costs ≤ 0) ---
    if (strstr(name, "PENALTY"))            { constraints[i] = SignConstraint::NON_POSITIVE; continue; }
    if (strstr(name, "BAD_BISHOP"))         { constraints[i] = SignConstraint::NON_POSITIVE; continue; }
    if (strcmp(name, "SHIELD_OPEN_FILE") == 0) { constraints[i] = SignConstraint::NON_POSITIVE; continue; }
    if (strcmp(name, "SHIELD_RANK_3") == 0) { constraints[i] = SignConstraint::NON_POSITIVE; continue; }
    if (strcmp(name, "PASSER_OWN_KING_DIST_EG") == 0)  { constraints[i] = SignConstraint::NON_POSITIVE; continue; }
    if (strcmp(name, "ROOK_BEHIND_ENEMY_EG") == 0)      { constraints[i] = SignConstraint::NON_POSITIVE; continue; }

    // --- Non-negative (bonuses / rewards ≥ 0) ---
    if (strstr(name, "BONUS"))              { constraints[i] = SignConstraint::NON_NEGATIVE; continue; }
    if (strstr(name, "OUTPOST"))            { constraints[i] = SignConstraint::NON_NEGATIVE; continue; }
    if (strstr(name, "BISHOP_PAIR"))        { constraints[i] = SignConstraint::NON_NEGATIVE; continue; }
    if (strstr(name, "PROTECTED_PASSER"))   { constraints[i] = SignConstraint::NON_NEGATIVE; continue; }
    if (strstr(name, "THREAT"))             { constraints[i] = SignConstraint::NON_NEGATIVE; continue; }
    if (strstr(name, "ROOK_OPEN_FILE"))     { constraints[i] = SignConstraint::NON_NEGATIVE; continue; }
    if (strstr(name, "ROOK_SEMI_OPEN"))     { constraints[i] = SignConstraint::NON_NEGATIVE; continue; }
    if (strstr(name, "ROOK_7TH"))           { constraints[i] = SignConstraint::NON_NEGATIVE; continue; }
    if (strstr(name, "SAFE_CHECK"))         { constraints[i] = SignConstraint::NON_NEGATIVE; continue; }
    if (strstr(name, "PASSED_R"))           { constraints[i] = SignConstraint::NON_NEGATIVE; continue; }
    if (strstr(name, "CONNECTED_R"))        { constraints[i] = SignConstraint::NON_NEGATIVE; continue; }
    if (strstr(name, "CANDIDATE_R"))        { constraints[i] = SignConstraint::NON_NEGATIVE; continue; }
    if (strcmp(name, "PASSER_ENEMY_KING_DIST_EG") == 0) { constraints[i] = SignConstraint::NON_NEGATIVE; continue; }
    if (strcmp(name, "ROOK_BEHIND_OWN_EG") == 0)        { constraints[i] = SignConstraint::NON_NEGATIVE; continue; }
    if (strcmp(name, "SPACE_WEIGHT") == 0)               { constraints[i] = SignConstraint::NON_NEGATIVE; continue; }
    if (strcmp(name, "SHIELD_RANK_0") == 0) { constraints[i] = SignConstraint::NON_NEGATIVE; continue; }
    if (strcmp(name, "SHIELD_RANK_1") == 0) { constraints[i] = SignConstraint::NON_NEGATIVE; continue; }

    // Everything else (PSTs, mobility, SHIELD_RANK_2, PAWN_STORM) → NONE.
  }
  return constraints;
}

/// A contiguous group of mobility table entries (start index + count).
struct MobilityGroup {
  int start;  ///< First param index in this group.
  int count;  ///< Number of entries.
};

/// Discover mobility groups from the registry.  Each group is a contiguous
/// run of parameters whose names share a common prefix (e.g. "MOB_KNIGHT_MG").
static std::vector<MobilityGroup> buildMobilityGroups() {
  int n = eval::tuning::paramCount();
  std::vector<MobilityGroup> groups;

  int i = 0;
  while (i < n) {
    const char* name = eval::tuning::getName(i);
    if (strncmp(name, "MOB_", 4) != 0) { ++i; continue; }

    // Extract the group prefix (everything before the last '_' + digit).
    // E.g. "MOB_KNIGHT_MG_0" → prefix "MOB_KNIGHT_MG_".
    std::string prefix(name);
    auto lastUnderscore = prefix.rfind('_');
    if (lastUnderscore != std::string::npos)
      prefix = prefix.substr(0, lastUnderscore + 1);

    // Count contiguous entries with the same prefix.
    int start = i;
    while (i < n && strncmp(eval::tuning::getName(i), prefix.c_str(),
                            prefix.size()) == 0)
      ++i;
    groups.push_back({start, i - start});
  }
  return groups;
}

/// Enforce monotonically non-decreasing values in a contiguous parameter
/// range using the pool-adjacent-violators (PAV) isotonic regression.
/// This is the L2-optimal projection onto the monotone cone.
/// Reference: https://en.wikipedia.org/wiki/Isotonic_regression
static void enforceMonotonicity(double* params, int start, int count) {
  if (count <= 1) return;

  // Pool-adjacent-violators: left-to-right pass.
  // Each "block" is a range whose values have been averaged.
  struct Block { int begin; int end; double sum; };
  std::vector<Block> blocks;
  blocks.reserve(count);

  for (int i = 0; i < count; ++i) {
    blocks.push_back({i, i + 1, params[start + i]});
    // Merge backward while the new block violates monotonicity.
    while (blocks.size() >= 2) {
      auto& last = blocks[blocks.size() - 1];
      auto& prev = blocks[blocks.size() - 2];
      double avgLast = last.sum / (last.end - last.begin);
      double avgPrev = prev.sum / (prev.end - prev.begin);
      if (avgPrev <= avgLast) break;  // Monotone — done.
      // Merge: absorb last into prev.
      prev.end = last.end;
      prev.sum += last.sum;
      blocks.pop_back();
    }
  }

  // Write averaged values back.
  for (const auto& blk : blocks) {
    double avg = blk.sum / (blk.end - blk.begin);
    for (int i = blk.begin; i < blk.end; ++i)
      params[start + i] = avg;
  }
}

// ===========================================================================
// Adam gradient descent
// ===========================================================================

static void adamOptimize(std::vector<eval::TrainingPosition>& trainSet,
                         const std::vector<eval::TrainingPosition>& testSet,
                         double K, int maxEpochs) {
  const int N = static_cast<int>(trainSet.size());
  const int nParams = eval::tuning::paramCount();
  const int nThreads = std::max(1,
      static_cast<int>(std::thread::hardware_concurrency()));
  const int chunkSize = (N + nThreads - 1) / nThreads;

  std::vector<bool> frozen = buildFrozenMask();
  std::vector<SignConstraint> signConstraints = buildSignConstraints();
  std::vector<MobilityGroup> mobilityGroups = buildMobilityGroups();

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

  // --- Early stopping ---
  // Track best test MSE and stop if no improvement for 50 epochs.
  // Saves the best parameters (lowest test MSE).
  // Standard ML technique — prevents overfitting on ~960 parameters.
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
        if (trainSet[i].trace.hasOCB) factor *= 0.75;
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

    // Zero gradients for frozen parameters (material values).
    for (int i = 0; i < nParams; ++i) {
      if (frozen[i]) gradient[i] = 0.0;
    }

    // ----- Adam update (float accumulators, no rounding) -----
    double beta1t = pow(ADAM_BETA1, epoch);
    double beta2t = pow(ADAM_BETA2, epoch);

    for (int i = 0; i < nParams; ++i) {
      if (frozen[i]) continue;  // Skip frozen — keeps m/v at zero.

      m[i] = ADAM_BETA1 * m[i] + (1.0 - ADAM_BETA1) * gradient[i];
      v[i] = ADAM_BETA2 * v[i] + (1.0 - ADAM_BETA2) * gradient[i] * gradient[i];

      double mHat = m[i] / (1.0 - beta1t);
      double vHat = v[i] / (1.0 - beta2t);

      params[i] -= lr * mHat / (sqrt(vHat) + ADAM_EPS);
    }

    // ----- Sign constraint clamping -----
    // Penalties must stay ≤ 0, bonuses must stay ≥ 0.  Applied after Adam
    // update to prevent the optimizer from inverting term semantics.
    for (int i = 0; i < nParams; ++i) {
      if (signConstraints[i] == SignConstraint::NON_NEGATIVE)
        params[i] = std::max(0.0, params[i]);
      else if (signConstraints[i] == SignConstraint::NON_POSITIVE)
        params[i] = std::min(0.0, params[i]);
    }

    // ----- Mobility monotonicity projection -----
    // More squares = better → enforce non-decreasing mobility tables.
    // Uses pool-adjacent-violators (L2-optimal isotonic regression).
    for (const auto& grp : mobilityGroups)
      enforceMonotonicity(params.data(), grp.start, grp.count);

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

  // ----- Finalize: round to int, write to registry -----
  for (int i = 0; i < nParams; ++i) {
    int rounded = static_cast<int>(std::round(params[i]));
    eval::tuning::setValue(i, rounded);
  }
}

// ---------------------------------------------------------------------------
// Output helpers — format tuned values for copy-paste into eval_params.h.
// ---------------------------------------------------------------------------

/// Print a 64-element PST array formatted as 8 rows of 8 values.
static void printPST(const char* name, const int* data) {
  printf("EVAL_CONST PST_ELEM %s[64] = {\n", name);
  for (int sq = 0; sq < 64; ++sq) {
    if (sq % 8 == 0) printf("    ");
    printf("%4d", data[sq]);
    if (sq < 63) printf(",");
    if (sq % 8 == 7) printf("\n");
  }
  printf("};\n");
}

/// Print a flat int array on a single line (e.g. MATERIAL, mobility tables).
static void printArray(const char* type, const char* name,
                       const int* data, int size) {
  printf("EVAL_CONST %s %s[] = {", type, name);
  for (int i = 0; i < size; ++i) {
    if (i > 0) printf(", ");
    printf("%d", data[i]);
  }
  printf("};\n");
}

/// Print changed parameters summary and eval_params.h copy-paste block.
static void printResults() {
  using namespace eval;
  int n = tuning::paramCount();

  // --- Changed values ---
  printf("\n// --- Changed parameter values ---\n\n");
  int changed = 0;
  for (int i = 0; i < n; ++i) {
    int val = tuning::getValue(i);
    int def = tuning::getDefault(i);
    if (val != def) {
      printf("%-28s = %5d  (was %d)\n", tuning::getName(i), val, def);
      ++changed;
    }
  }
  printf("\n// %d parameters changed out of %d total.\n", changed, n);

  // =====================================================================
  // Copy-paste block — replace values between the namespace opening
  // and namespace closing in eval_params.h.
  // =====================================================================
  printf("\n// ===========================================================================\n");
  printf("// Copy-paste block for eval_params.h\n");
  printf("// ===========================================================================\n\n");

  // --- Material ---
  printArray("MAT_ELEM", "MATERIAL", MATERIAL, 6);
  printArray("MAT_ELEM", "MATERIAL_EG", MATERIAL_EG, 6);

  // --- PSTs ---

  printf("\n// --- Midgame PSTs ---\n\n");
  printPST("PST_PAWN_MG", PST_PAWN_MG);     printf("\n");
  printPST("PST_KNIGHT_MG", PST_KNIGHT_MG);  printf("\n");
  printPST("PST_BISHOP_MG", PST_BISHOP_MG);  printf("\n");
  printPST("PST_ROOK_MG", PST_ROOK_MG);      printf("\n");
  printPST("PST_QUEEN_MG", PST_QUEEN_MG);    printf("\n");
  printPST("PST_KING_MG", PST_KING_MG);

  printf("\n// --- Endgame PSTs ---\n\n");
  printPST("PST_PAWN_EG", PST_PAWN_EG);      printf("\n");
  printPST("PST_KNIGHT_EG", PST_KNIGHT_EG);  printf("\n");
  printPST("PST_BISHOP_EG", PST_BISHOP_EG);  printf("\n");
  printPST("PST_ROOK_EG", PST_ROOK_EG);      printf("\n");
  printPST("PST_QUEEN_EG", PST_QUEEN_EG);    printf("\n");
  printPST("PST_KING_EG", PST_KING_EG);

  // --- Pawn structure ---
  printf("\n");
  printArray("int", "PASSED_RANK_BONUS_MG", PASSED_RANK_BONUS_MG, 8);
  printArray("int", "PASSED_RANK_BONUS_EG", PASSED_RANK_BONUS_EG, 8);
  printf("EVAL_CONST int ISOLATED_PENALTY_MG = %3d;\n", ISOLATED_PENALTY_MG);
  printf("EVAL_CONST int ISOLATED_PENALTY_EG = %3d;\n", ISOLATED_PENALTY_EG);
  printf("EVAL_CONST int DOUBLED_PENALTY_EG  = %3d;\n", DOUBLED_PENALTY_EG);
  printf("EVAL_CONST int BACKWARD_PENALTY_MG = %3d;\n", BACKWARD_PENALTY_MG);
  printf("EVAL_CONST int BACKWARD_PENALTY_EG = %3d;\n", BACKWARD_PENALTY_EG);
  printArray("int", "CONNECTED_BONUS_MG", CONNECTED_BONUS_MG, 8);
  printArray("int", "CONNECTED_BONUS_EG", CONNECTED_BONUS_EG, 8);
  printArray("int", "CANDIDATE_PASSED_MG", CANDIDATE_PASSED_MG, 8);
  printArray("int", "CANDIDATE_PASSED_EG", CANDIDATE_PASSED_EG, 8);
  printf("EVAL_CONST int PROTECTED_PASSER_MG = %3d;\n", PROTECTED_PASSER_MG);
  printf("EVAL_CONST int PROTECTED_PASSER_EG = %3d;\n", PROTECTED_PASSER_EG);
  printf("EVAL_CONST int PASSER_OWN_KING_DIST_EG   = %3d;\n", PASSER_OWN_KING_DIST_EG);
  printf("EVAL_CONST int PASSER_ENEMY_KING_DIST_EG =  %3d;\n", PASSER_ENEMY_KING_DIST_EG);

  // --- Piece bonuses ---
  printf("\n");
  printf("EVAL_CONST int BISHOP_PAIR_MG = %3d;\n", BISHOP_PAIR_MG);
  printf("EVAL_CONST int BISHOP_PAIR_EG = %3d;\n", BISHOP_PAIR_EG);
  printf("EVAL_CONST int BAD_BISHOP_MG  = %3d;\n", BAD_BISHOP_MG);
  printf("EVAL_CONST int BAD_BISHOP_EG  = %3d;\n", BAD_BISHOP_EG);
  printf("EVAL_CONST int OUTPOST_BONUS_MG = %3d;\n", OUTPOST_BONUS_MG);
  printf("EVAL_CONST int OUTPOST_BONUS_EG = %3d;\n", OUTPOST_BONUS_EG);
  printf("EVAL_CONST int BISHOP_OUTPOST_MG = %3d;\n", BISHOP_OUTPOST_MG);
  printf("EVAL_CONST int BISHOP_OUTPOST_EG = %3d;\n", BISHOP_OUTPOST_EG);

  // --- Rook bonuses ---
  printf("\n");
  printf("EVAL_CONST int ROOK_OPEN_FILE_MG      = %3d;\n", ROOK_OPEN_FILE_MG);
  printf("EVAL_CONST int ROOK_OPEN_FILE_EG      = %3d;\n", ROOK_OPEN_FILE_EG);
  printf("EVAL_CONST int ROOK_SEMI_OPEN_FILE_MG = %3d;\n", ROOK_SEMI_OPEN_FILE_MG);
  printf("EVAL_CONST int ROOK_SEMI_OPEN_FILE_EG = %3d;\n", ROOK_SEMI_OPEN_FILE_EG);
  printf("EVAL_CONST int ROOK_7TH_MG = %3d;\n", ROOK_7TH_MG);
  printf("EVAL_CONST int ROOK_7TH_EG = %3d;\n", ROOK_7TH_EG);
  printf("EVAL_CONST int ROOK_BEHIND_OWN_PASSER_EG  = %3d;\n", ROOK_BEHIND_OWN_PASSER_EG);
  printf("EVAL_CONST int ROOK_BEHIND_ENEMY_PASSER_EG = %3d;\n", ROOK_BEHIND_ENEMY_PASSER_EG);

  // --- Mobility ---
  printf("\n");
  printArray("int", "MOB_KNIGHT_MG", MOB_KNIGHT_MG, MOB_KNIGHT_SIZE);
  printArray("int", "MOB_KNIGHT_EG", MOB_KNIGHT_EG, MOB_KNIGHT_SIZE); printf("\n");
  printArray("int", "MOB_BISHOP_MG", MOB_BISHOP_MG, MOB_BISHOP_SIZE);
  printArray("int", "MOB_BISHOP_EG", MOB_BISHOP_EG, MOB_BISHOP_SIZE); printf("\n");
  printArray("int", "MOB_ROOK_MG", MOB_ROOK_MG, MOB_ROOK_SIZE);
  printArray("int", "MOB_ROOK_EG", MOB_ROOK_EG, MOB_ROOK_SIZE); printf("\n");
  printArray("int", "MOB_QUEEN_MG", MOB_QUEEN_MG, MOB_QUEEN_SIZE);
  printArray("int", "MOB_QUEEN_EG", MOB_QUEEN_EG, MOB_QUEEN_SIZE);

  // --- King safety ---
  printf("\n");
  printArray("int", "SHIELD_RANK", SHIELD_RANK, 4);
  printf("EVAL_CONST int SHIELD_OPEN_FILE     = %3d;\n", SHIELD_OPEN_FILE);
  printArray("int", "PAWN_STORM", PAWN_STORM, 8);
  printf("EVAL_CONST int SAFE_CHECK_KNIGHT = %3d;\n", SAFE_CHECK_KNIGHT);
  printf("EVAL_CONST int SAFE_CHECK_BISHOP = %3d;\n", SAFE_CHECK_BISHOP);
  printf("EVAL_CONST int SAFE_CHECK_ROOK   = %3d;\n", SAFE_CHECK_ROOK);
  printf("EVAL_CONST int SAFE_CHECK_QUEEN  = %3d;\n", SAFE_CHECK_QUEEN);

  // --- Threats ---
  printf("\n");
  printf("EVAL_CONST int THREAT_BY_PAWN_MG  = %3d;\n", THREAT_BY_PAWN_MG);
  printf("EVAL_CONST int THREAT_BY_PAWN_EG  = %3d;\n", THREAT_BY_PAWN_EG);
  printf("EVAL_CONST int THREAT_BY_MINOR_MG = %3d;\n", THREAT_BY_MINOR_MG);
  printf("EVAL_CONST int THREAT_BY_MINOR_EG = %3d;\n", THREAT_BY_MINOR_EG);
  printf("EVAL_CONST int THREAT_BY_ROOK_MG  = %3d;\n", THREAT_BY_ROOK_MG);
  printf("EVAL_CONST int THREAT_BY_ROOK_EG  = %3d;\n", THREAT_BY_ROOK_EG);
  printf("EVAL_CONST int THREAT_HANGING_MG  = %3d;\n", THREAT_HANGING_MG);
  printf("EVAL_CONST int THREAT_HANGING_EG  = %3d;\n", THREAT_HANGING_EG);

  // --- Space ---
  printf("\n");
  printf("EVAL_CONST int SPACE_WEIGHT = %3d;\n", SPACE_WEIGHT);
}

// ===========================================================================
// Entry point
// ===========================================================================

int main(int argc, char* argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: tune <corpus.epd> [epochs=500] [K]\n");
    fprintf(stderr, "  K: sigmoid scaling constant (if omitted, found via ternary search)\n");
    return 1;
  }

  int maxEpochs = 500;
  if (argc >= 3) maxEpochs = std::atoi(argv[2]);
  if (maxEpochs < 1) maxEpochs = 500;

  double providedK = -1.0;
  if (argc >= 4) providedK = std::atof(argv[3]);

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
      // Reconstruct the integer eval from the trace, matching the eval's
      // truncation order: (1) taper via rounding to nearest integer
      // (double precision is close enough that round() gives the correct
      // integer from the distributed sum), (2) OCB *3/4 as integer.
      double rawVal = traceScore(tp.trace, params.data(), false);
      int tracei = static_cast<int>(std::lround(rawVal));
      if (tp.trace.hasOCB) tracei = tracei * 3 / 4;
      if (std::abs(evalScore - tracei) > 4) {
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

  // Find optimal K (or use provided value).
  // Initialize float param snapshot from registry defaults for K search.
  std::vector<double> initParams(eval::tuning::paramCount());
  for (int i = 0; i < eval::tuning::paramCount(); ++i)
    initParams[i] = static_cast<double>(eval::tuning::getValue(i));
  double K;
  if (providedK > 0.0) {
    K = providedK;
    fprintf(stderr, "Using provided K = %.6f\n", K);
  } else {
    fprintf(stderr, "Finding optimal K...\n");
    K = findOptimalK(trainSet, initParams.data());
    fprintf(stderr, "Optimal K = %.6f\n", K);
  }

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
  printResults();

  return 0;
}
