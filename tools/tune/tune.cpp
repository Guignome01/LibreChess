// ---------------------------------------------------------------------------
// Tuning — local search (coordinate descent) optimizer for eval parameters.
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
// The optimizer uses classical Texel tuning: for each parameter, test ±1
// from the current integer value and keep the change only if it reduces
// MSE.  This avoids the float→int quantization noise of gradient descent,
// which was shown to produce ~80 Elo regressions across 7 SPRT runs.
//
// An inverted index maps each parameter to the positions that reference it,
// enabling O(affected positions) per test instead of O(all positions).
//
// Workflow:
//   1. Load corpus (EPD format with c9 "result" opcode)
//   2. Build name→index map from the tuning registry
//   3. Extract traces for all positions (one-time, O(N))
//   4. Find optimal sigmoid scaling constant K (ternary search)
//   5. Local search: test each param ±1, keep MSE-improving changes
//   6. Output C++ code with changed values
//
// After tuning, copy the formatted block from stdout into
// eval/params.h, replacing the existing EVAL_CONST definitions.
// Rebuild the tuner to start the next iteration from updated defaults.
//
// Usage: tune <corpus.epd> [maxPasses=500] [K]
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

// Mobility table sizes — mirrors EVAL_FIXED values in eval/params.h.
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
  std::string fen;  // Retained for trace/eval validation (needs Position).
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
    entries.push_back({pos.bitboards(), result, rec.fen});
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
// Parameter constraints — frozen params, sign bounds, mobility monotonicity
// ===========================================================================

// Sign constraint for scalar parameters.  Penalties must stay ≤ 0, bonuses
// must stay ≥ 0.  Prevents the optimizer from inverting term semantics.
enum class SignConstraint { NONE, NON_NEGATIVE, NON_POSITIVE };

/// Build a boolean mask identifying frozen (non-tunable) parameters.
///
/// Frozen categories:
///   MAT_*          — search pruning margins calibrated to piece values.
///   PST_*          — 752 entries would dominate the gradient.
///   MOB_*          — mobility tables are search-guidance terms: the tuner
///                    flattens them (reducing MSE) but this destroys the
///                    gradients the search relies on to distinguish good
///                    moves.  Queen mobility was flattened to 5-7 identical
///                    values, rook low-mobility became indistinguishable.
///   SHIELD_*       — pawn shield terms interact non-linearly with king
///                    safety table; Texel tuning can't capture this.
///   PAWN_STORM_*   — same: tuner pushed storm values positive (welcoming
///                    enemy pawn advances toward the castled king).
///   SAFE_CHECK_*   — feed into the nonlinear KING_SAFETY_TABLE S-curve;
///                    linearized trace coefficients are too approximate.
///   SPACE_WEIGHT   — tuner reduced it 60% (5→2), nearly eliminating space
///                    evaluation — a pure search-guidance term.
///
/// Only pawn structure scalars, piece bonuses, threats, and rook bonuses
/// remain tunable (~50 parameters with clear eval-to-outcome correlation).
static std::vector<bool> buildFrozenMask() {
  int n = eval::tuning::paramCount();
  std::vector<bool> frozen(n, false);
  for (int i = 0; i < n; ++i) {
    const char* name = eval::tuning::getName(i);
    if (strncmp(name, "MAT_", 4) == 0 ||
        strncmp(name, "PST_", 4) == 0 ||
        strncmp(name, "MOB_", 4) == 0 ||
        strncmp(name, "SHIELD_", 7) == 0 ||
        strncmp(name, "PAWN_STORM", 10) == 0 ||
        strncmp(name, "SAFE_CHECK", 10) == 0 ||
        strcmp(name, "SPACE_WEIGHT") == 0)
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

// ===========================================================================
// Local search — sequential coordinate descent with integer ±1 perturbation
// ===========================================================================

// Classical Texel tuning: for each parameter in turn, test ±1 and keep the
// change if it reduces MSE.  Unlike batch approaches that compute all deltas
// simultaneously, each change is applied immediately so subsequent tests see
// up-to-date scores.  This avoids the cumulative drift that caused -269 Elo
// in the batch variant (BISHOP_PAIR 30→108 over 500 batch passes).
//
// MAX_DELTA caps how far any parameter can move from its default value.
// Without this, coordinate descent diverges: MSE improves but the resulting
// values are chess-nonsensical (-308 Elo at MAX_DELTA=∞).
//
// Search-guidance parameters (mobility, king safety, space) are frozen —
// they look like noise to position-level MSE optimization but the search
// depends on their gradients.  Only ~50 scalars with clear static-eval-to-
// outcome correlation remain tunable (pawn structure, piece bonuses, threats,
// rook bonuses).
//
// For each unfrozen parameter, the delta computation is parallelized across
// positions (scanning all positions' trace entries for references).
// This is O(N × avg_entries) per param test, with threading.
//
// Reference: https://www.chessprogramming.org/Texel%27s_Tuning_Method

/// Maximum drift from default per parameter.  Prevents coordinate descent
/// from diverging into chess-nonsensical local minima.
static constexpr int MAX_DELTA = 3;

static void localSearch(std::vector<eval::TrainingPosition>& trainSet,
                        const std::vector<eval::TrainingPosition>& testSet,
                        double K, int maxPasses) {
  const int N = static_cast<int>(trainSet.size());
  const int nParams = eval::tuning::paramCount();
  const int nThreads = std::max(1,
      static_cast<int>(std::thread::hardware_concurrency()));
  const int chunkSize = (N + nThreads - 1) / nThreads;

  std::vector<bool> frozen = buildFrozenMask();
  std::vector<SignConstraint> signConstraints = buildSignConstraints();
  std::vector<MobilityGroup> mobilityGroups = buildMobilityGroups();

  // Current integer parameters.
  std::vector<double> params(nParams);
  for (int i = 0; i < nParams; ++i)
    params[i] = static_cast<double>(eval::tuning::getValue(i));

  int unfrozen = static_cast<int>(
      std::count(frozen.begin(), frozen.end(), false));

  // ----- Precompute base scores and sigmoid values (multithreaded) -----
  std::vector<double> rawScores(N);
  std::vector<double> sigValues(N);

  auto recomputeAll = [&]() -> double {
    std::vector<double> partialErr(nThreads, 0.0);
    auto worker = [&](int ti) {
      int start = ti * chunkSize;
      int end = std::min(start + chunkSize, N);
      double localErr = 0.0;
      for (int j = start; j < end; ++j) {
        double raw = trainSet[j].trace.bias;
        for (const auto& e : trainSet[j].trace.entries)
          raw += params[e.idx] * e.coeff;
        rawScores[j] = raw;
        double s = trainSet[j].trace.hasOCB ? raw * 0.75 : raw;
        sigValues[j] = sigmoid(s, K);
        double diff = trainSet[j].result - sigValues[j];
        localErr += diff * diff;
      }
      partialErr[ti] = localErr;
    };
    std::vector<std::thread> threads;
    for (int ti = 0; ti < nThreads; ++ti)
      threads.emplace_back(worker, ti);
    for (auto& th : threads) th.join();
    double total = 0.0;
    for (double e : partialErr) total += e;
    return total / N;
  };

  double baseMSE = recomputeAll();
  fprintf(stderr, "Local search: initial MSE = %.10f (%d unfrozen params, "
          "MAX_DELTA=%d)\n", baseMSE, unfrozen, MAX_DELTA);

  // ----- Test a single param ±delta: compute MSE change (multithreaded) -----
  // Scans all positions for references to paramIdx, computes the MSE delta
  // if that param were changed by `delta`.  Returns unnormalized sum of
  // squared-error changes (caller divides by N).
  auto computeParamDelta = [&](int paramIdx, double delta) -> double {
    std::vector<double> partials(nThreads, 0.0);
    auto worker = [&](int ti) {
      int start = ti * chunkSize;
      int end = std::min(start + chunkSize, N);
      double local = 0.0;
      for (int j = start; j < end; ++j) {
        for (const auto& e : trainSet[j].trace.entries) {
          if (e.idx == paramIdx) {
            double newRaw = rawScores[j] + delta * e.coeff;
            double newFinal = trainSet[j].trace.hasOCB
                ? newRaw * 0.75 : newRaw;
            double newSig = sigmoid(newFinal, K);
            double oldDiff = trainSet[j].result - sigValues[j];
            double newDiff = trainSet[j].result - newSig;
            local += newDiff * newDiff - oldDiff * oldDiff;
            break;  // Each param appears at most once per trace.
          }
        }
      }
      partials[ti] = local;
    };
    std::vector<std::thread> threads;
    for (int ti = 0; ti < nThreads; ++ti)
      threads.emplace_back(worker, ti);
    for (auto& th : threads) th.join();
    double total = 0.0;
    for (double d : partials) total += d;
    return total / N;
  };

  // ----- Apply a param change: update rawScores/sigValues (multithreaded) -----
  auto applyParamChange = [&](int paramIdx, double delta) {
    auto worker = [&](int ti) {
      int start = ti * chunkSize;
      int end = std::min(start + chunkSize, N);
      for (int j = start; j < end; ++j) {
        for (const auto& e : trainSet[j].trace.entries) {
          if (e.idx == paramIdx) {
            rawScores[j] += delta * e.coeff;
            double s = trainSet[j].trace.hasOCB
                ? rawScores[j] * 0.75 : rawScores[j];
            sigValues[j] = sigmoid(s, K);
            break;
          }
        }
      }
    };
    std::vector<std::thread> threads;
    for (int ti = 0; ti < nThreads; ++ti)
      threads.emplace_back(worker, ti);
    for (auto& th : threads) th.join();
  };

  // Convergence: stop when no changes or MSE improvement is negligible.
  // Relative threshold avoids oscillation (params flipping back and forth).
  static constexpr double MIN_RELATIVE_IMPROVEMENT = 1e-8;
  double bestMSESoFar = baseMSE;

  for (int pass = 1; pass <= maxPasses; ++pass) {
    double startMSE = baseMSE;
    int changes = 0;

    // ----- Sequential: test each param with up-to-date scores -----
    for (int i = 0; i < nParams; ++i) {
      if (frozen[i]) continue;

      double origVal = params[i];
      int defaultVal = eval::tuning::getDefault(i);
      double bestMSEDelta = 0;  // Must be < 0 to accept.
      double bestDelta = 0;

      for (double delta : {1.0, -1.0}) {
        double newVal = origVal + delta;

        // MAX_DELTA constraint: limit drift from default.
        if (std::abs(newVal - defaultVal) > MAX_DELTA) continue;

        // Sign constraints.
        if (signConstraints[i] == SignConstraint::NON_POSITIVE && newVal > 0)
          continue;
        if (signConstraints[i] == SignConstraint::NON_NEGATIVE && newVal < 0)
          continue;

        double mseDelta = computeParamDelta(i, delta);
        if (mseDelta < bestMSEDelta) {
          bestMSEDelta = mseDelta;
          bestDelta = delta;
        }
      }

      // Accept the best improving change and immediately update scores.
      if (bestDelta != 0) {
        params[i] = origVal + bestDelta;
        baseMSE += bestMSEDelta;
        ++changes;
        applyParamChange(i, bestDelta);
      }
    }

    // Enforce mobility monotonicity after each full pass.
    bool monoViolated = false;
    for (const auto& grp : mobilityGroups) {
      for (int j = grp.start + 1; j < grp.start + grp.count; ++j) {
        if (params[j] < params[j - 1]) {
          params[j] = params[j - 1];
          monoViolated = true;
        }
      }
    }
    if (monoViolated) baseMSE = recomputeAll();

    double testMSE = computeError(testSet, K, params.data());
    fprintf(stderr, "Pass %3d: %3d changes, train MSE = %.10f, "
            "test MSE = %.10f\n", pass, changes, baseMSE, testMSE);

    if (changes == 0) {
      fprintf(stderr, "Converged (no changes) after %d passes.\n", pass);
      break;
    }

    // Check relative improvement.  Catches oscillation where changes != 0
    // but net MSE barely moves (params flipping back and forth).
    double relImprovement = (startMSE - baseMSE) / startMSE;
    if (relImprovement < MIN_RELATIVE_IMPROVEMENT) {
      fprintf(stderr, "Converged (relative improvement %.2e < %.2e) "
              "after %d passes.\n", relImprovement,
              MIN_RELATIVE_IMPROVEMENT, pass);
      break;
    }

    bestMSESoFar = std::min(bestMSESoFar, baseMSE);
  }

  // Write final integer values to registry.
  for (int i = 0; i < nParams; ++i)
    eval::tuning::setValue(i, static_cast<int>(std::round(params[i])));
}

// ---------------------------------------------------------------------------
// Output helpers — format tuned values for copy-paste into eval/params.h.
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

/// Print changed parameters summary and eval/params.h copy-paste block.
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
  // and namespace closing in eval/params.h.
  // =====================================================================
  printf("\n// ===========================================================================\n");
  printf("// Copy-paste block for eval/params.h\n");
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
      Position pos;
      if (!pos.loadFEN(rawEntries[i].fen + " 0 1")) continue;
      int evalScore = eval::evaluatePosition(pos);
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

  double initTrainErr = computeError(trainSet, K, initParams.data());
  double initTestErr  = computeError(testSet, K, initParams.data());
  fprintf(stderr, "Initial: train MSE = %.10f, test MSE = %.10f\n",
          initTrainErr, initTestErr);

  // Local search optimization (classical Texel method).
  fprintf(stderr, "Starting local search (%d params, max %d passes)...\n",
          eval::tuning::paramCount(), maxEpochs);
  localSearch(trainSet, testSet, K, maxEpochs);

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
