import { run } from "./run.ts";
import { join } from "node:path";


const bin = process.env.DFU ?? join(import.meta.dirname, "../../", "src/dfu-programmer");
const target = process.env.TARGET ?? "atmega8u2";


/**
 * Run the dfu-programmer binary with custom arguments.
 */
export function runDfu(args: string[] = []) {
  return run(bin, args);
}

/**
 * Run the dfu-programmer binary with custom arguments and the local target pre-pended.
 */
export function runDfuTargeted(args: string[] = []) {
  return runDfu([target, ...args]);
}
