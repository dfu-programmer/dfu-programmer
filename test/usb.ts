import { describe, test, beforeEach, afterEach } from "node:test";
import assert from "node:assert/strict";
import { runDfuTargeted } from "./util/dfu.ts";

// We currently use a consistent line ending for all platforms
const EOL = "\n";

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
    const run = runDfuTargeted(["reset"]);
    const exitCode = await run.exitCode;
    const { stdout, stderr } = run;

    assert.equal(exitCode, 0);
    assert.equal(stdout, "");
    assert.equal(stderr, "");
  });

  test("Command: launch", async () => {
    const run = runDfuTargeted(["launch"]);
    const exitCode = await run.exitCode;
    const { stdout, stderr } = run;

    assert.equal(exitCode, 0);
    assert.equal(stdout, "");
    assert.equal(stderr, "");
  });

  test("Device missing", async () => {
    {
      // MAYBE: Use reset.sh script instead of this block
      const run = runDfuTargeted(["reset"]);
      const exitCode = await run.exitCode;
      const { stdout, stderr } = run;

      assert.equal(exitCode, 0);
      assert.equal(stdout, "");
      assert.equal(stderr, "");
    }

    // Run immediately after reset command.
    // TODO: Hold the device in reset for a while to ensure it's not detected.
    {
      const run = runDfuTargeted(["launch"]);
      const exitCode = await run.exitCode;
      const { stdout, stderr } = run;

      assert.equal(exitCode, 3);
      assert.equal(stdout, "");
      assert.equal(stderr, `dfu-programmer: no device present.${EOL}`);
    }
  });

  test("Read flash to stdout after erase", async () => {
    {
      const run = runDfuTargeted(["erase", "--force"]);
      const exitCode = await run.exitCode;
      const { stdout, stderr } = run;

      assert.equal(exitCode, 0);
      assert.equal(stdout, "");
      assert.equal(stderr, `Erasing flash...  Success${EOL}Checking memory from 0x0 to 0xFFF...  Empty.${EOL}`);
    }
    {
      const run = runDfuTargeted(["read"]);
      const exitCode = await run.exitCode;
      const { stdout, stderr } = run;

      assert.equal(exitCode, 0);
      assert.equal(stdout, `:00000001FF${EOL}`);
      assert.equal(stderr, 
        `Reading 0x1000 bytes...${EOL}Success${EOL}Memory is blank, returning a single blank page.${EOL}Use --force to return the entire memory regardless.${EOL}Dumping 0x80 bytes from address offset 0x0.${EOL}`
      );
    }
  });
});
