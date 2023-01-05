import { describe, expect, test } from "@jest/globals";
import { runDfu } from "./util/dfu";

/**
 * Standalone tests that should work without any hardware connected.
 */

describe("standalone", () => {
  test("it runs without arguments and returns an error code and usage", async () => {
    const res = runDfu();
    expect(await res.exitCode).toBe(2);
    expect(res.stdout).toBe("");
    expect(res.stderr).toMatch(/^dfu-programmer/g);
  });
});
