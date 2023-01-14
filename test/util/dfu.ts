import { run } from "./run";
import { join } from "path";

import { log } from "console";

const bin = process.env.DFU ?? join(__dirname, "../../", "src/dfu-programmer");
const target = process.env.TARGET ?? "atmega8u2";

// TODO: Use jest's logging to print this once somehow?
// log('Using dfu-programmer binary:', bin);
// log('Using target:', target);

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
