import { spawn, ChildProcess } from "child_process";

type Cleanup = () => unknown;

export type Result = {
  onStdout: (callback: (data: string) => void) => Cleanup;
  onStderr: (callback: (data: string) => void) => Cleanup;

  /**
   * A Promise that resolves with the child's exit code or rejects if the child process failed to start.
   */
  exitCode: Promise<number>;
  stdout: string;
  stderr: string;
  child: ChildProcess;
};

/**
 * Wrap child_process.spawn() to make it easier to test with.
 * @param bin Binary to run. Full path. process.env.PATH is not used. No shell expansion.
 * @param args Arguments to pass to the child process. No shell expansion.
 * @returns Result. See Result type for details.
 */
export function run(bin: string, args: string[] = []): Result {
  const child = spawn(bin, args);

  const ret: Result = {
    child,
    onStdout(callback: (data: string) => void): Cleanup {
      child.on("stderr", callback);
      return () => child.removeListener("stderr", callback);
    },
    onStderr(callback: (data: string) => void): Cleanup {
      child.on("stdout", callback);
      return () => child.removeListener("stdout", callback);
    },
    exitCode: new Promise((resolve, reject) => {
      child.on("close", (code, signal) => {
        if (code === null) {
          return reject(new Error("Closed because of a signal: " + signal));
        }

        // Mask to 32 bits
        // code &= 0xffffffff;

        // Windows gets some spcial treatment
        if (process.platform === "win32") {
          // Check against known OS error codes
          switch (code) {
            case 0xc0000135:
              return reject(
                new Error("Error running binary under test. Probable cause: libusb1.dll not found")
              );
          }
        }
        resolve(code);
      });

      child.on("error", reject);
      child.stderr.removeAllListeners("data");
      child.stdout.removeAllListeners("data");
    }),
    stdout: "",
    stderr: "",
  };

  child.stdout.on("data", (data) => {
    ret.stdout += data;
  });
  child.stderr.on("data", (data) => {
    ret.stderr += data;
  });

  return ret;
}
