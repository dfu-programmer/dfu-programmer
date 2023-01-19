import { describe, expect, test } from "@jest/globals";
import { runDfuTargeted } from "./util/dfu";

/**
 * Basic tests that should work with a sample device connected via USB.
 *
 * No side channel verification with avrdude.
 */

describe("Basic Communication with Hardware", () => {
  let testSeparatorDelay: Promise<void>;
  afterEach(() => {
    // Wait 600ms between each test to ensure the hardware is stable
    testSeparatorDelay = new Promise((resolve) => setTimeout(resolve, 600).unref());
  });
  beforeEach(() => testSeparatorDelay);

  test("Command: reset", async () => {
    const res = runDfuTargeted(["reset"]);
    const code = await res.exitCode;
    const { stdout, stderr } = res;

    expect(code).toBe(0);
    expect(stdout).toBe("");
    expect(stderr).toBe("");
  });

  test("Command: launch", async () => {
    const res = runDfuTargeted(["launch"]);
    const code = await res.exitCode;
    const { stdout, stderr } = res;

    expect(code).toBe(0);
    expect(stdout).toBe("");
    expect(stderr).toBe("");
  });
});
