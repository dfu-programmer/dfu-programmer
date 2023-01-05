import { run } from "./run";
import { join } from "path";

import { log } from "console";

const bin = process.env.DFU ?? join(__dirname, "../../", "src/dfu-programmer");
const target = process.env.TARGET ?? "atmega8u2";

// TODO: Use jest's logging to record this?
// log('Using dfu-programmer binary:', bin);
// log('Using target:', target);

export function runDfu(args: string[] = []) {
  return run(bin, args);
}

export function runDfuTargeted(args: string[] = []) {
  return runDfu([target, ...args]);
}
