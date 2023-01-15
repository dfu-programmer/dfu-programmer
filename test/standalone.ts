import { describe, expect, test } from "@jest/globals";
import { runDfu } from "./util/dfu";

/**
 * Standalone tests that should work without any hardware connected.
 */

describe("standalone", () => {
  test("it runs without arguments and returns an error code and usage", async () => {
    const res = runDfu();
    expect(await res.exitCode).toBe(2);
    const { stdout, stderr } = res;

    // No output on stdout
    expect(stdout).toBe("");

    // Starts with "dfu-programmer <version>"
    expect(stderr).toMatch(/^dfu-programmer/g);
  });

  test("it prints usage with --help", async () => {
    const res = runDfu(["--help"]);
    expect(await res.exitCode).toBe(0);
    const { stdout, stderr } = res;

    // No output on stdout
    expect(stdout).toBe("");

    // Starts with "dfu-programmer <version>"
    expect(stderr).toMatch(/^dfu-programmer/g);

    // Contains the usage
    expect(stderr).toMatch(/^Usage: dfu-programmer target[:usb-bus,usb-addr] command [options] [global-options] [file|data]/m);

    // Contains the global options
    expect(stderr).toMatch(/^global-options:/m);

    // Contains the command summary
    expect(stderr).toMatch(/^command summary:/m);

    // Contains the additional details
    expect(stderr).toMatch(/^additional details:/m);

    // ...
  });
});
