export async function delay(ms: number, unref = false) {
  return new Promise((resolve) => {
    const timeout = setTimeout(resolve, ms);
    if (unref) timeout.unref();
  });
}
