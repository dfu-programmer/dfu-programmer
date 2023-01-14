import { open, readFile, writeFile, unlink } from "fs/promises";
import { join } from "path";
import { tmpdir } from "os";

/**
 * A function that will unlock the lock.
 * @returns A Promise that resolves when the lock is released.
 */
export type Unlock = () => Promise<void>;

/**
 * Get a lock to prevent multiple tests from running at the same time.
 * Will wait for any existing locks to be released.
 *
 * Will also remove any stale locks.
 *
 * @returns A Promise that resolves, when the lock is acquired, to a function that will unlock the lock.
 */
export async function getLock(): Promise<Unlock> {
  const lockFile = join(tmpdir(), "dfu-programmer-test.lock");

  async function cleanup() {
    await unlink(lockFile).then(
      () => {
        // console.log("Removed lock file");
      },
      (e) => {
        console.error("Failed to remove lock file:");
        console.error(e);
      }
    );
  }

  let tryCount = 0;
  while (true) {
    try {
      const fd = await open(lockFile, "wx");

      // Successfully got the lock
      // console.log("Got lock");

      await writeFile(fd, process.pid.toString());

      await fd.close();

      return cleanup;
    } catch (e) {
      if (e.code !== "EEXIST") throw e;
      // Lock file already exists
    }

    const pid = parseInt(await readFile(lockFile, "utf8"), 10);

    if (isNaN(pid)) {
      await cleanup();
      continue;
    }

    try {
      // console.log("Checking if the PID is still alive");
      process.kill(pid, 0);

      // PID is still alive
    } catch (e) {
      if (e.code !== "ESRCH") throw e;

      // console.log("PID is not alive");

      if (!tryCount) {
        // console.log("Removing lock file and trying again");
        await cleanup();
      }
    }

    if (!tryCount) console.log("Waiting for lock to be released...");

    tryCount++;

    await new Promise((resolve) => setTimeout(resolve, 1000));
  }
}
