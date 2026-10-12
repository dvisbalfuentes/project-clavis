#include "engine/classifier.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

int main() {
  clavis::engine::PhysiologicalClassifier classifier;
  using clavis::engine::ClassificationStatus;

  // Missing and invalid metrics must not be classified or enter RR history.
  auto result = classifier.process(std::nullopt, std::nullopt);
  assert(result.status == ClassificationStatus::insufficient_data);
  result = classifier.process(-1.0, 60.0);
  assert(result.status == ClassificationStatus::insufficient_data);
  result = classifier.process(1.0, std::numeric_limits<double>::quiet_NaN());
  assert(result.status == ClassificationStatus::insufficient_data);

  // Threshold comparisons are strict: exactly 50 and 100 BPM are normal for
  // their respective rules.
  result = classifier.process(1.2, 50.0);
  assert(result.status == ClassificationStatus::available);
  assert(!result.bradycardia && !result.tachycardia);
  assert(result.rr_intervals_used == 1);
  result = classifier.process(0.6, 100.0);
  assert(!result.bradycardia && !result.tachycardia);
  result = classifier.process(1.25, 49.999);
  assert(result.bradycardia && !result.tachycardia);
  result = classifier.process(0.59, 100.001);
  assert(!result.bradycardia && result.tachycardia);

  // A reset removes prior RR history and restores irregularity startup.
  classifier.reset();
  result = classifier.process(1.0, 60.0);
  assert(result.status == ClassificationStatus::available);
  assert(!result.irregular_rr.has_value());
  assert(result.rr_intervals_used == 1);

  // Four regular one-second intervals have CV=0 and are not irregular.
  result = classifier.process(1.0, 60.0);
  result = classifier.process(1.0, 60.0);
  result = classifier.process(1.0, 60.0);
  assert(result.irregular_rr.has_value() && !*result.irregular_rr);
  assert(*result.rr_coefficient_of_variation == 0.0);

  // A variable RR sequence exceeds the default 10% CV rule. Heart-rate and
  // irregularity flags are independent and can coexist.
  classifier.reset();
  result = classifier.process(0.50, 120.0);
  result = classifier.process(1.40, 42.857);
  result = classifier.process(0.60, 100.0);
  result = classifier.process(1.50, 40.0);
  assert(result.irregular_rr.has_value() && *result.irregular_rr);
  assert(result.bradycardia && !result.tachycardia);
  result = classifier.process(0.50, 120.0);
  assert(result.irregular_rr.has_value() && *result.irregular_rr);
  assert(!result.bradycardia && result.tachycardia);

  classifier.reset();
  result = classifier.process(1.0, 60.0);
  assert(!result.irregular_rr.has_value());
  assert(result.rr_intervals_used == 1);

  std::cout << "classifier_test passed\n";
}
