# Test Utilities

These are utilities that are used by the tests.
They are not tests.

## [`dfu.ts`](dfu.ts)

A wrapper around the `dfu-programmer` executable.

Same as `run`, but with a few extra options.

```typescript
/**
 * Run the dfu-programmer binary with custom arguments.
 */
function runDfu(args: string[] = []);
/**
 * Run the dfu-programmer binary with custom arguments and the local target pre-pended.
 */
function runDfuTargeted(args: string[] = []);
```

## [`run.ts`](run.ts)

A wrapper around the Node's `child_process.spawn` function that returns a `Result` object.
Used by the `runDfu` function.

```typescript
/**
 * Wrap child_process.spawn() to make it easier to test with.
 * @param bin Binary to run. Full path. process.env.PATH is not used. No shell expansion.
 * @param args Arguments to pass to the child process. No shell expansion.
 * @returns Result. See Result type for details.
 */
function run(bin: string, args: string[] = []): Result;

type Result = {
  /**
   * A Promise that resolves with the child's exit code
   *
   * Rejects if the child process failed to start
   */
  exitCode: Promise<number>;

  /**
   * The stdout output from the child process
   *
   * This is only populated if the child process exits with a zero exit code
   */
  stdout: string;
  /**
   * The stderr output from the child process
   *
   * This is only populated if the child process exits with a non-zero exit code
   */
  stderr: string;

  /**
   * Get notified when the child process writes to stdout
   */
  onStdout: (callback: (data: string) => void) => Cleanup;
  /**
   * Get notified when the child process writes to stderr
   */
  onStderr: (callback: (data: string) => void) => Cleanup;

  /**
   * Direct access to the child process
   */
  child: ChildProcess;
};
```

## TODO

Add utilities for:

 - [ ] Simple Reset of device when desired
 - [ ] Running AVRDUDE
   - [ ] Reading flash with AVRDUDE to verify bytes written by dfu-programmer
   - [ ] Writing flash with AVRDUDE and verifying bytes written with dfu-programmer
   - [ ] Re-flashing bootloader
 - [ ] `.hex` files
   - [ ] Comparing with mask
   - [ ] Generating files for testing