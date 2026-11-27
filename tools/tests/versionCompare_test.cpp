// Copyright © Charlie Howard 2026 All rights reserved
#include <stdio.h>
#include "versionCompare.h"

static int failures = 0;

static void expectParse(const char* text, bool ok) {
  FwVersion v;
  bool got = versionParse(text, &v);
  if (got != ok) {
    printf("FAIL parse \"%s\": expected %s\n", text, ok ? "ok" : "rejected");
    failures++;
  }
}

static void expectCompare(const char* a, const char* b, int want) {
  FwVersion va, vb;
  if (!versionParse(a, &va) || !versionParse(b, &vb)) {
    printf("FAIL compare %s vs %s: didn't parse\n", a, b);
    failures++;
    return;
  }
  int got = versionCompare(va, vb);
  int back = versionCompare(vb, va);
  if (got != want || back != -want) {
    printf("FAIL compare %s vs %s: got %d and %d, expected %d\n", a, b, got, back, want);
    failures++;
  }
}

static void expectBeta(const char* text, bool beta) {
  FwVersion v;
  if (!versionParse(text, &v) || versionIsBeta(v) != beta) {
    printf("FAIL beta \"%s\": expected %s\n", text, beta ? "beta" : "release");
    failures++;
  }
}

int main() {
  expectParse("1.1.5.0", true);
  expectParse("v1.1.5.0", true);
  expectParse("V1.1.5.0", true);
  expectParse("1.1.5.0b1", true);
  expectParse("1.1.5.0B12", true);
  expectParse("10.20.30.40", true);
  expectParse("", false);
  expectParse("v", false);
  expectParse("1.1.5", false);
  expectParse("1.1.5.0.1", false);
  expectParse("1.1.5.0b", false);
  expectParse("1.1.5.0b0", false);
  expectParse("1.1.5.0-beta", false);
  expectParse("1.1.5.0 ", false);
  expectParse("1..5.0", false);
  expectParse("a.b.c.d", false);
  expectParse("1.1.-5.0", false);
  expectParse("1.1.5.999999", false);
  FwVersion v;
  if (versionParse(nullptr, &v)) {
    printf("FAIL parse null: expected rejected\n");
    failures++;
  }

  expectCompare("1.1.5.0", "1.1.5.0", 0);
  expectCompare("v1.1.5.0", "1.1.5.0", 0);
  expectCompare("1.1.5.0b2", "1.1.5.0b2", 0);
  expectCompare("1.1.5.0", "1.1.4.0", 1);
  expectCompare("1.2.0.0", "1.1.9.9", 1);
  expectCompare("2.0.0.0", "1.9.9.9", 1);
  expectCompare("1.1.5.1", "1.1.5.0", 1);
  expectCompare("1.1.10.0", "1.1.9.0", 1);
  expectCompare("1.1.5.0", "1.1.5.0b1", 1);
  expectCompare("1.1.5.0", "1.1.5.0b99", 1);
  expectCompare("1.1.5.0b10", "1.1.5.0b9", 1);
  expectCompare("1.1.5.0b2", "1.1.5.0b1", 1);
  expectCompare("1.1.6.0b1", "1.1.5.0", 1);
  expectCompare("1.1.5.0b1", "1.1.4.0", 1);

  expectBeta("1.1.5.0", false);
  expectBeta("1.1.5.0b1", true);

  if (failures) {
    printf("%d test(s) failed\n", failures);
    return 1;
  }
  printf("All version tests passed\n");
  return 0;
}
