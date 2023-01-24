import { describe, expect, test } from "@jest/globals";
import { runDfuTargeted } from "./util/dfu";

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

    expect(exitCode).toBe(0);
    expect(stdout).toBe("");
    expect(stderr).toBe("");
  });

  test("Command: launch", async () => {
    const run = runDfuTargeted(["launch"]);
    const exitCode = await run.exitCode;
    const { stdout, stderr } = run;

    expect(exitCode).toBe(0);
    expect(stdout).toBe("");
    expect(stderr).toBe("");
  });

  test("Device missing", async () => {
    {
      // MAYBE: Use reset.sh script instead of this block
      const run = runDfuTargeted(["reset"]);
      const exitCode = await run.exitCode;
      const { stdout, stderr } = run;

      expect(exitCode).toBe(0);
      expect(stdout).toBe("");
      expect(stderr).toBe("");
    }

    // Run immediately after reset command.
    // TODO: Hold the device in reset for a while to ensure it's not detected.
    {
      const run = runDfuTargeted(["launch"]);
      const exitCode = await run.exitCode;
      const { stdout, stderr } = run;

      expect(exitCode).toBe(3);
      expect(stdout).toBe("");
      expect(stderr).toBe(`dfu-programmer: no device present.${EOL}`);
    }
  });

  test("Read flash to stdout after erase", async () => {
    {
      const run = runDfuTargeted(["erase", "--force"]);
      const exitCode = await run.exitCode;
      const { stdout, stderr } = run;

      expect(exitCode).toBe(0);
      expect(stdout).toBe("");
      expect(stderr).toBe(`Erasing flash...  Success${EOL}Checking memory from 0x0 to 0xFFF...  Empty.${EOL}`);
    }
    {
      const run = runDfuTargeted(["read"]);
      const exitCode = await run.exitCode;
      const { stdout, stderr } = run;

      expect(exitCode).toBe(0);
      expect(stdout).toBe(`:00000001FF${EOL}`);
      expect(stderr).toBe(
        `Reading 0x1000 bytes...${EOL}Success${EOL}Memory is blank, returning a single blank page.${EOL}Use --force to return the entire memory regardless.${EOL}Dumping 0x80 bytes from address offset 0x0.${EOL}`
      );
    }
  });

  test("Read flash to stdout after reset", async () => {
    {
      const run = runDfuTargeted(["reset"]);
      const exitCode = await run.exitCode;
      const { stdout, stderr } = run;

      expect(exitCode).toBe(0);
      expect(stdout).toBe("");
      expect(stderr).toBe("");
    }
    {
      const run = runDfuTargeted(["read"]);
      const exitCode = await run.exitCode;
      const { stdout, stderr } = run;

      const bytes = 0x1000;
      const dump = 0x80;
      const offset = 0;

      expect(exitCode).toBe(0);
      expect(stdout).toBe(`:00000001FF${EOL}`);
      expect(stderr).toBe(
        "" +
          `Reading 0x${bytes.toString(16)} bytes...${EOL}` +
          `Success${EOL}` +
          `Memory is blank, returning a single blank page.${EOL}` +
          `Use --force to return the entire memory regardless.${EOL}` +
          `Dumping 0x${dump.toString(16)} bytes from address offset 0x${offset.toString(16)}.${EOL}`
      );
    }
  });
});
