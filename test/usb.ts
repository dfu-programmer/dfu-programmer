import { describe, expect, test } from "@jest/globals";
import { runDfuTargeted } from "./util/dfu";

/**
 * Basic tests that should work with a sample device connected via USB.
 *
 * No side channel verification with avrdude.
 */

describe("Basic Communication with Hardware", () => {
  test("Command: launch", async () => {
    const res = runDfuTargeted(["launch"]);
    const code = await res.exitCode;
    const { stdout, stderr } = res;

    // The expected error code when no device is connected.
    // TODO: not return?
    if (code === 3) return;

    expect(code).toBe(0);
    expect(stdout).toBe("");
    expect(stderr).toBe("");
  });
});
