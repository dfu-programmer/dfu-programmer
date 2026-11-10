import { describe, test } from "node:test";
import assert from "node:assert/strict";
import { runDfu } from "./util/dfu.ts";

/**
 * Standalone tests that should work without any hardware connected.
 */

describe("standalone", () => {
  test("it runs without arguments and returns an error code and usage", async () => {
    const res = runDfu();
    assert.equal(await res.exitCode, 2);
    const { stdout, stderr } = res;

    // No output on stdout
    assert.equal(stdout, "");

    // Starts with "dfu-programmer <version>"
    assert.match(stderr, /^dfu-programmer/g);
  });

  test("it prints usage with --help", async () => {
    const res = runDfu(["--help"]);
    assert.equal(await res.exitCode, 0);
    const { stdout, stderr } = res;

    // No output on stdout
    assert.equal(stdout, "");

    // Starts with "dfu-programmer <version>"
    assert.match(stderr, /^dfu-programmer/g);

    // Contains the usage
    assert.match(stderr, /^Usage: dfu-programmer target\[:usb-bus,usb-addr\] command \[options\] \[global-options\] \[file\|data\]/m);

    // Contains the global options
    assert.match(stderr, /^global-options:/m);

    // Contains the command summary
    assert.match(stderr, /^command summary:/m);

    // Contains the additional details
    assert.match(stderr, /^additional details:/m);

    // ...
  });
});
