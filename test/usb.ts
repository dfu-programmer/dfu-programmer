import { describe, expect, test } from "@jest/globals";
import { runDfuTargeted } from "./util/dfu";
import { getLock } from "./util/lock";

/**
 * Basic tests that should work with a sample device connected via USB.
 *
 * No side channel verification with avrdude.
 */

let releaseLock: () => Promise<void>;
beforeAll(async () => {
  releaseLock = await getLock();
});
afterAll(async () => {
  await releaseLock();
});

describe("Basic Communication with Hardware", () => {
  test("Command: launch", async () => {
    const res = runDfuTargeted(["launch"]);
    const code = await res.exitCode;
    const { stdout, stderr } = res;

    if (code === 3) {
      // This is the expected error code when no device is connected

      // TODO: Do we really just return here?
      return;
    }

    expect(code).toBe(0);

    // TODO: Verify that the device is in DFU mode
    expect(stdout).toBe("");
    expect(stderr).toMatch(/^dfu-programmer/g);
  });
});
